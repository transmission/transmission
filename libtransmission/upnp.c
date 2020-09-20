/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>

#ifdef SYSTEM_MINIUPNP
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#else
#include <miniupnp/miniupnpc.h>
#include <miniupnp/upnpcommands.h>
#endif

#include "transmission.h"
#include "log.h"
#include "port-forwarding.h"
#include "session.h"
#include "tr-assert.h"
#include "upnp.h"
#include "utils.h"

static char const* getKey(void)
{
    return _("Port Forwarding (UPnP)");
}

typedef enum
{
    TR_UPNP_IDLE,
    TR_UPNP_ERR,
    TR_UPNP_DISCOVER,
    TR_UPNP_MAP,
    TR_UPNP_UNMAP
}
tr_upnp_state;

struct tr_upnp
{
    bool hasDiscovered;
    struct UPNPUrls urls;
    struct IGDdatas data;
    int port;
    char lanaddr[16];
    bool isMapped;
    tr_upnp_state state;
};

/**
***
**/

tr_upnp* tr_upnpInit(void)
{
    tr_upnp* ret = tr_new0(tr_upnp, 1);

    ret->state = TR_UPNP_DISCOVER;
    ret->port = -1;
    return ret;
}

void tr_upnpClose(tr_upnp* handle)
{
    TR_ASSERT(!handle->isMapped);
    TR_ASSERT(handle->state == TR_UPNP_IDLE || handle->state == TR_UPNP_ERR || handle->state == TR_UPNP_DISCOVER);

    if (handle->hasDiscovered)
    {
        FreeUPNPUrls(&handle->urls);
    }

    tr_free(handle);
}

/**
***  Wrappers for miniupnpc functions
**/

static struct UPNPDev* tr_upnpDiscover(int msec)
{
    struct UPNPDev* ret;
    bool have_err;

#if (MINIUPNPC_API_VERSION >= 8) /* adds ipv6 and error args */
    int err = UPNPDISCOVER_SUCCESS;

#if (MINIUPNPC_API_VERSION >= 14) /* adds ttl */
    ret = upnpDiscover(msec, NULL, NULL, 0, 0, 2, &err);
#else
    ret = upnpDiscover(msec, NULL, NULL, 0, 0, &err);
#endif

    have_err = err != UPNPDISCOVER_SUCCESS;
#else
    ret = upnpDiscover(msec, NULL, NULL, 0);
    have_err = ret == NULL;
#endif

    if (have_err)
    {
        tr_logAddNamedDbg(getKey(), "upnpDiscover failed (errno %d - %s)", errno, tr_strerror(errno));
    }

    return ret;
}

static int tr_upnpGetSpecificPortMappingEntry(tr_upnp* handle, char const* proto)
{
    int err;
    char intClient[16];
    char intPort[16];
    char portStr[16];

    *intClient = '\0';
    *intPort = '\0';

    tr_snprintf(portStr, sizeof(portStr), "%d", (int)handle->port);

#if (MINIUPNPC_API_VERSION >= 10) /* adds remoteHost arg */
    err = UPNP_GetSpecificPortMappingEntry(handle->urls.controlURL, handle->data.first.servicetype, portStr, proto,
        NULL /*remoteHost*/, intClient, intPort, NULL /*desc*/, NULL /*enabled*/, NULL /*duration*/);
#elif (MINIUPNPC_API_VERSION >= 8) /* adds desc, enabled and leaseDuration args */
    err = UPNP_GetSpecificPortMappingEntry(handle->urls.controlURL, handle->data.first.servicetype, portStr, proto, intClient,
        intPort, NULL /*desc*/, NULL /*enabled*/, NULL /*duration*/);
#else
    err = UPNP_GetSpecificPortMappingEntry(handle->urls.controlURL, handle->data.first.servicetype, portStr, proto, intClient,
        intPort);
#endif

    return err;
}

static int tr_upnpAddPortMapping(tr_upnp const* handle, char const* proto, tr_port port, char const* desc)
{
    int err;
    int const old_errno = errno;
    char portStr[16];
    errno = 0;

    tr_snprintf(portStr, sizeof(portStr), "%d", (int)port);

#if (MINIUPNPC_API_VERSION >= 8)
    err = UPNP_AddPortMapping(handle->urls.controlURL, handle->data.first.servicetype, portStr, portStr, handle->lanaddr, desc,
        proto, NULL, NULL);
#else
    err = UPNP_AddPortMapping(handle->urls.controlURL, handle->data.first.servicetype, portStr, portStr, handle->lanaddr, desc,
        proto, NULL);
#endif

    if (err != 0)
    {
        tr_logAddNamedDbg(getKey(), "%s Port forwarding failed with error %d (errno %d - %s)", proto, err, errno,
            tr_strerror(errno));
    }

    errno = old_errno;
    return err;
}

static void tr_upnpDeletePortMapping(tr_upnp const* handle, char const* proto, tr_port port)
{
    char portStr[16];

    tr_snprintf(portStr, sizeof(portStr), "%d", (int)port);

    UPNP_DeletePortMapping(handle->urls.controlURL, handle->data.first.servicetype, portStr, proto, NULL);
}

/**
***
**/

enum
{
    UPNP_IGD_NONE = 0,
    UPNP_IGD_VALID_CONNECTED = 1,
    UPNP_IGD_VALID_NOT_CONNECTED = 2,
    UPNP_IGD_INVALID = 3
};

int tr_upnpPulse(tr_upnp* handle, int port, bool isEnabled, bool doPortCheck)
{
    int ret;

    if (isEnabled && handle->state == TR_UPNP_DISCOVER)
    {
        struct UPNPDev* devlist;

        devlist = tr_upnpDiscover(2000);

        errno = 0;

        if (UPNP_GetValidIGD(devlist, &handle->urls, &handle->data, handle->lanaddr,
            sizeof(handle->lanaddr)) == UPNP_IGD_VALID_CONNECTED)
        {
            tr_logAddNamedInfo(getKey(), _("Found Internet Gateway Device \"%s\""), handle->urls.controlURL);
            tr_logAddNamedInfo(getKey(), _("Local Address is \"%s\""), handle->lanaddr);
            handle->state = TR_UPNP_IDLE;
            handle->hasDiscovered = true;
        }
        else
        {
            handle->state = TR_UPNP_ERR;
            tr_logAddNamedDbg(getKey(), "UPNP_GetValidIGD failed (errno %d - %s)", errno, tr_strerror(errno));
            tr_logAddNamedDbg(getKey(), "If your router supports UPnP, please make sure UPnP is enabled!");
        }

        freeUPNPDevlist(devlist);
    }

    if (handle->state == TR_UPNP_IDLE)
    {
        if (handle->isMapped && (!isEnabled || handle->port != port))
        {
            handle->state = TR_UPNP_UNMAP;
        }
    }

    if (isEnabled && handle->isMapped && doPortCheck)
    {
        if (tr_upnpGetSpecificPortMappingEntry(handle, "TCP") != UPNPCOMMAND_SUCCESS ||
            tr_upnpGetSpecificPortMappingEntry(handle, "UDP") != UPNPCOMMAND_SUCCESS)
        {
            tr_logAddNamedInfo(getKey(), _("Port %d isn't forwarded"), handle->port);
            handle->isMapped = false;
        }
    }

    if (handle->state == TR_UPNP_UNMAP)
    {
        tr_upnpDeletePortMapping(handle, "TCP", handle->port);
        tr_upnpDeletePortMapping(handle, "UDP", handle->port);

        tr_logAddNamedInfo(getKey(), _("Stopping port forwarding through \"%s\", service \"%s\""), handle->urls.controlURL,
            handle->data.first.servicetype);

        handle->isMapped = false;
        handle->state = TR_UPNP_IDLE;
        handle->port = -1;
    }

    if (handle->state == TR_UPNP_IDLE)
    {
        if (isEnabled && !handle->isMapped)
        {
            handle->state = TR_UPNP_MAP;
        }
    }

    if (handle->state == TR_UPNP_MAP)
    {
        int err_tcp = -1;
        int err_udp = -1;
        errno = 0;

        if (handle->urls.controlURL == NULL)
        {
            handle->isMapped = false;
        }
        else
        {
            char desc[64];
            tr_snprintf(desc, sizeof(desc), "%s at %d", TR_NAME, port);

            err_tcp = tr_upnpAddPortMapping(handle, "TCP", port, desc);
            err_udp = tr_upnpAddPortMapping(handle, "UDP", port, desc);

            handle->isMapped = err_tcp == 0 || err_udp == 0;
        }

        tr_logAddNamedInfo(getKey(), _("Port forwarding through \"%s\", service \"%s\". (local address: %s:%d)"),
            handle->urls.controlURL, handle->data.first.servicetype, handle->lanaddr, port);

        if (handle->isMapped)
        {
            tr_logAddNamedInfo(getKey(), "%s", _("Port forwarding successful!"));
            handle->port = port;
            handle->state = TR_UPNP_IDLE;
        }
        else
        {
            tr_logAddNamedDbg(getKey(), "If your router supports UPnP, please make sure UPnP is enabled!");
            handle->port = -1;
            handle->state = TR_UPNP_ERR;
        }
    }

    switch (handle->state)
    {
    case TR_UPNP_DISCOVER:
        ret = TR_PORT_UNMAPPED;
        break;

    case TR_UPNP_MAP:
        ret = TR_PORT_MAPPING;
        break;

    case TR_UPNP_UNMAP:
        ret = TR_PORT_UNMAPPING;
        break;

    case TR_UPNP_IDLE:
        ret = handle->isMapped ? TR_PORT_MAPPED : TR_PORT_UNMAPPED;
        break;

    default:
        ret = TR_PORT_ERROR;
        break;
    }

    return ret;
}
