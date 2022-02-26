// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <future>
#include <mutex>
#include <thread>

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

namespace
{

char constexpr Key[] = "Port Forwarding (UPnP)";

enum class UpnpState
{
    IDLE,
    FAILED,
    WILL_DISCOVER, // next action is upnpDiscover()
    DISCOVERING, // currently making blocking upnpDiscover() call in a worker thread
    WILL_MAP, // next action is UPNP_AddPortMapping()
    WILL_UNMAP // next action is UPNP_DeletePortMapping()
};

tr_port_forwarding portFwdState(UpnpState upnp_state, bool is_mapped)
{
    switch (upnp_state)
    {
    case UpnpState::WILL_DISCOVER:
    case UpnpState::DISCOVERING:
        return TR_PORT_UNMAPPED;

    case UpnpState::WILL_MAP:
        return TR_PORT_MAPPING;

    case UpnpState::WILL_UNMAP:
        return TR_PORT_UNMAPPING;

    case UpnpState::IDLE:
        return is_mapped ? TR_PORT_MAPPED : TR_PORT_UNMAPPED;

    default: // UpnpState::FAILED:
        return TR_PORT_ERROR;
    }
}

} // namespace

struct tr_upnp
{
    ~tr_upnp()
    {
        TR_ASSERT(!isMapped);
        TR_ASSERT(
            state == UpnpState::IDLE || state == UpnpState::FAILED || state == UpnpState::WILL_DISCOVER ||
            state == UpnpState::DISCOVERING);

        FreeUPNPUrls(&urls);
    }

    bool hasDiscovered = false;
    UPNPUrls urls = {};
    IGDdatas data = {};
    int port = -1;
    char lanaddr[16] = {};
    bool isMapped = false;
    UpnpState state = UpnpState::WILL_DISCOVER;

    // Used to return the results of upnpDiscover() from a worker thread
    // to be processed without blocking in tr_upnpPulse().
    // This will be pending while the state is UpnpState::DISCOVERING.
    std::optional<std::future<UPNPDev*>> discover_future;
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
        tr_logAddNamedDbg(Key, "upnpDiscover failed (errno %d - %s)", errno, tr_strerror(errno));
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
            Key,
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

static auto* discoverThreadfunc(char* bindaddr)
{
    auto* const ret = tr_upnpDiscover(2000, bindaddr);
    tr_free(bindaddr);
    return ret;
}

template<typename T>
static bool isFutureReady(std::future<T> const& future)
{
    return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

tr_port_forwarding tr_upnpPulse(tr_upnp* handle, tr_port port, bool isEnabled, bool doPortCheck, char const* bindaddr)
{
    if (isEnabled && handle->state == UpnpState::WILL_DISCOVER)
    {
        TR_ASSERT(!handle->discover_future);

        auto task = std::packaged_task<UPNPDev*(char*)>{ discoverThreadfunc };
        handle->discover_future = task.get_future();
        handle->state = UpnpState::DISCOVERING;

        std::thread(std::move(task), tr_strdup(bindaddr)).detach();
    }

    if (isEnabled && handle->state == UpnpState::DISCOVERING && handle->discover_future &&
        isFutureReady(*handle->discover_future))
    {
        auto* const devlist = handle->discover_future->get();
        handle->discover_future.reset();

        FreeUPNPUrls(&handle->urls);
        if (UPNP_GetValidIGD(devlist, &handle->urls, &handle->data, handle->lanaddr, sizeof(handle->lanaddr)) ==
            UPNP_IGD_VALID_CONNECTED)
        {
            tr_logAddNamedInfo(Key, _("Found Internet Gateway Device \"%s\""), handle->urls.controlURL);
            tr_logAddNamedInfo(Key, _("Local Address is \"%s\""), handle->lanaddr);
            handle->state = UpnpState::IDLE;
            handle->hasDiscovered = true;
        }
        else
        {
            handle->state = UpnpState::FAILED;
            tr_logAddNamedDbg(Key, "UPNP_GetValidIGD failed (errno %d - %s)", errno, tr_strerror(errno));
            tr_logAddNamedDbg(Key, "If your router supports UPnP, please make sure UPnP is enabled!");
        }

        freeUPNPDevlist(devlist);
    }

    if ((handle->state == UpnpState::IDLE) && (handle->isMapped) && (!isEnabled || handle->port != port))
    {
        handle->state = UpnpState::WILL_UNMAP;
    }

    if (isEnabled && handle->isMapped && doPortCheck &&
        ((tr_upnpGetSpecificPortMappingEntry(handle, "TCP") != UPNPCOMMAND_SUCCESS) ||
         (tr_upnpGetSpecificPortMappingEntry(handle, "UDP") != UPNPCOMMAND_SUCCESS)))
    {
        tr_logAddNamedInfo(Key, _("Port %d isn't forwarded"), handle->port);
        handle->isMapped = false;
    }

    if (handle->state == UpnpState::WILL_UNMAP)
    {
        tr_upnpDeletePortMapping(handle, "TCP", handle->port);
        tr_upnpDeletePortMapping(handle, "UDP", handle->port);

        tr_logAddNamedInfo(
            Key,
            _("Stopping port forwarding through \"%s\", service \"%s\""),
            handle->urls.controlURL,
            handle->data.first.servicetype);

        handle->isMapped = false;
        handle->state = UpnpState::IDLE;
        handle->port = -1;
    }

    if ((handle->state == UpnpState::IDLE) && isEnabled && !handle->isMapped)
    {
        handle->state = UpnpState::WILL_MAP;
    }

    if (handle->state == UpnpState::WILL_MAP)
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
            Key,
            _("Port forwarding through \"%s\", service \"%s\". (local address: %s:%d)"),
            handle->urls.controlURL,
            handle->data.first.servicetype,
            handle->lanaddr,
            port);

        if (handle->isMapped)
        {
            tr_logAddNamedInfo(Key, "%s", _("Port forwarding successful!"));
            handle->port = port;
            handle->state = UpnpState::IDLE;
        }
        else
        {
            tr_logAddNamedDbg(Key, "If your router supports UPnP, please make sure UPnP is enabled!");
            handle->port = -1;
            handle->state = UpnpState::FAILED;
        }
    }

    return portFwdState(handle->state, handle->isMapped);
}
