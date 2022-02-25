// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>

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

static char const* getKey()
{
    return _("Port Forwarding (UPnP)");
}

enum tr_upnp_state
{
    TR_UPNP_IDLE,
    TR_UPNP_ERR,
    TR_UPNP_DISCOVER,
    TR_UPNP_MAP,
    TR_UPNP_UNMAP
};

static tr_port_forwarding portFwdState(tr_upnp_state upnp_state, bool is_mapped)
{
    switch (upnp_state)
    {
    case TR_UPNP_DISCOVER:
        return TR_PORT_UNMAPPED;

    case TR_UPNP_MAP:
        return TR_PORT_MAPPING;

    case TR_UPNP_UNMAP:
        return TR_PORT_UNMAPPING;

    case TR_UPNP_IDLE:
        return is_mapped ? TR_PORT_MAPPED : TR_PORT_UNMAPPED;

    default:
        return TR_PORT_ERROR;
    }
}

struct tr_upnp
{
    ~tr_upnp()
    {
        TR_ASSERT(!isMapped);
        TR_ASSERT(state == TR_UPNP_IDLE || state == TR_UPNP_ERR || state == TR_UPNP_DISCOVER);

        FreeUPNPUrls(&urls);
    }

    bool hasDiscovered = false;
    struct UPNPUrls urls = {};
    struct IGDdatas data = {};
    int port = -1;
    char lanaddr[16] = {};
    bool isMapped = false;
    tr_upnp_state state = TR_UPNP_DISCOVER;
};

/**
***
**/

tr_upnp* tr_upnpInit()
{
    return new tr_upnp();
}

void tr_upnpClose(tr_upnp* handle)
{
    delete handle;
}

/**
***  Wrappers for miniupnpc functions
**/

static struct UPNPDev* tr_upnpDiscover(int msec, char const* bindaddr)
{
    UPNPDev* ret = nullptr;
    auto have_err = bool{};

#if (MINIUPNPC_API_VERSION >= 8) /* adds ipv6 and error args */
    int err = UPNPDISCOVER_SUCCESS;

#if (MINIUPNPC_API_VERSION >= 14) /* adds ttl */
    ret = upnpDiscover(msec, bindaddr, nullptr, 0, 0, 2, &err);
#else
    ret = upnpDiscover(msec, bindaddr, nullptr, 0, 0, &err);
#endif

    have_err = err != UPNPDISCOVER_SUCCESS;
#else
    ret = upnpDiscover(msec, bindaddr, nullptr, 0);
    have_err = ret == nullptr;
#endif

    if (have_err)
    {
        tr_logAddNamedDbg(getKey(), "upnpDiscover failed (errno %d - %s)", errno, tr_strerror(errno));
    }

    return ret;
}

static int tr_upnpGetSpecificPortMappingEntry(tr_upnp* handle, char const* proto)
{
    char intClient[16];
    char intPort[16];
    char portStr[16];

    *intClient = '\0';
    *intPort = '\0';

    tr_snprintf(portStr, sizeof(portStr), "%d", handle->port);

#if (MINIUPNPC_API_VERSION >= 10) /* adds remoteHost arg */
    int const err = UPNP_GetSpecificPortMappingEntry(
        handle->urls.controlURL,
        handle->data.first.servicetype,
        portStr,
        proto,
        nullptr /*remoteHost*/,
        intClient,
        intPort,
        nullptr /*desc*/,
        nullptr /*enabled*/,
        nullptr /*duration*/);
#elif (MINIUPNPC_API_VERSION >= 8) /* adds desc, enabled and leaseDuration args */
    int const err = UPNP_GetSpecificPortMappingEntry(
        handle->urls.controlURL,
        handle->data.first.servicetype,
        portStr,
        proto,
        intClient,
        intPort,
        nullptr /*desc*/,
        nullptr /*enabled*/,
        nullptr /*duration*/);
#else
    int const err = UPNP_GetSpecificPortMappingEntry(
        handle->urls.controlURL,
        handle->data.first.servicetype,
        portStr,
        proto,
        intClient,
        intPort);
#endif

    return err;
}

static int tr_upnpAddPortMapping(tr_upnp const* handle, char const* proto, tr_port port, char const* desc)
{
    int const old_errno = errno;
    char portStr[16];
    errno = 0;

    tr_snprintf(portStr, sizeof(portStr), "%d", (int)port);

#if (MINIUPNPC_API_VERSION >= 8)
    int err = UPNP_AddPortMapping(
        handle->urls.controlURL,
        handle->data.first.servicetype,
        portStr,
        portStr,
        handle->lanaddr,
        desc,
        proto,
        nullptr,
        nullptr);
#else
    int err = UPNP_AddPortMapping(
        handle->urls.controlURL,
        handle->data.first.servicetype,
        portStr,
        portStr,
        handle->lanaddr,
        desc,
        proto,
        nullptr);
#endif

    if (err != 0)
    {
        tr_logAddNamedDbg(
            getKey(),
            "%s Port forwarding failed with error %d (errno %d - %s)",
            proto,
            err,
            errno,
            tr_strerror(errno));
    }

    errno = old_errno;
    return err;
}

static void tr_upnpDeletePortMapping(tr_upnp const* handle, char const* proto, tr_port port)
{
    char portStr[16];

    tr_snprintf(portStr, sizeof(portStr), "%d", (int)port);

    UPNP_DeletePortMapping(handle->urls.controlURL, handle->data.first.servicetype, portStr, proto, nullptr);
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

tr_port_forwarding tr_upnpPulse(tr_upnp* handle, tr_port port, bool isEnabled, bool doPortCheck, char const* bindaddr)
{
    if (isEnabled && handle->state == TR_UPNP_DISCOVER)
    {
        auto* const devlist = tr_upnpDiscover(2000, bindaddr);
        errno = 0;

        FreeUPNPUrls(&handle->urls);
        if (UPNP_GetValidIGD(devlist, &handle->urls, &handle->data, handle->lanaddr, sizeof(handle->lanaddr)) ==
            UPNP_IGD_VALID_CONNECTED)
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

    if ((handle->state == TR_UPNP_IDLE) && (handle->isMapped) && (!isEnabled || handle->port != port))
    {
        handle->state = TR_UPNP_UNMAP;
    }

    if (isEnabled && handle->isMapped && doPortCheck &&
        ((tr_upnpGetSpecificPortMappingEntry(handle, "TCP") != UPNPCOMMAND_SUCCESS) ||
         (tr_upnpGetSpecificPortMappingEntry(handle, "UDP") != UPNPCOMMAND_SUCCESS)))
    {
        tr_logAddNamedInfo(getKey(), _("Port %d isn't forwarded"), handle->port);
        handle->isMapped = false;
    }

    if (handle->state == TR_UPNP_UNMAP)
    {
        tr_upnpDeletePortMapping(handle, "TCP", handle->port);
        tr_upnpDeletePortMapping(handle, "UDP", handle->port);

        tr_logAddNamedInfo(
            getKey(),
            _("Stopping port forwarding through \"%s\", service \"%s\""),
            handle->urls.controlURL,
            handle->data.first.servicetype);

        handle->isMapped = false;
        handle->state = TR_UPNP_IDLE;
        handle->port = -1;
    }

    if ((handle->state == TR_UPNP_IDLE) && isEnabled && !handle->isMapped)
    {
        handle->state = TR_UPNP_MAP;
    }

    if (handle->state == TR_UPNP_MAP)
    {
        errno = 0;

        if (handle->urls.controlURL == nullptr)
        {
            handle->isMapped = false;
        }
        else
        {
            char desc[64];
            tr_snprintf(desc, sizeof(desc), "%s at %d", TR_NAME, port);

            int const err_tcp = tr_upnpAddPortMapping(handle, "TCP", port, desc);
            int const err_udp = tr_upnpAddPortMapping(handle, "UDP", port, desc);

            handle->isMapped = err_tcp == 0 || err_udp == 0;
        }

        tr_logAddNamedInfo(
            getKey(),
            _("Port forwarding through \"%s\", service \"%s\". (local address: %s:%d)"),
            handle->urls.controlURL,
            handle->data.first.servicetype,
            handle->lanaddr,
            port);

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

    return portFwdState(handle->state, handle->isMapped);
}
