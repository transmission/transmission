// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <fmt/core.h>

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

enum class UpnpState
{
    IDLE,
    FAILED,
    WILL_DISCOVER, // next action is upnpDiscover()
    DISCOVERING, // currently making blocking upnpDiscover() call in a worker thread
    WILL_MAP, // next action is UPNP_AddPortMapping()
    WILL_UNMAP // next action is UPNP_DeletePortMapping()
};

constexpr tr_port_forwarding portFwdState(UpnpState upnp_state, bool is_mapped)
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
    tr_upnp() = default;
    tr_upnp(tr_upnp&&) = delete;
    tr_upnp(tr_upnp const&) = delete;
    tr_upnp& operator=(tr_upnp&&) = delete;
    tr_upnp& operator=(tr_upnp const&) = delete;

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
    tr_port port;
    std::string lanaddr;
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
        tr_logAddDebug(fmt::format("upnpDiscover failed: {} ({})", tr_strerror(errno), errno));
    }

    return ret;
}

static int tr_upnpGetSpecificPortMappingEntry(tr_upnp* handle, char const* proto)
{
    auto int_client = std::array<char, 16>{};
    auto int_port = std::array<char, 16>{};

    auto const port_str = fmt::format(FMT_STRING("{:d}"), handle->port.host());

#if (MINIUPNPC_API_VERSION >= 10) /* adds remoteHost arg */
    int const err = UPNP_GetSpecificPortMappingEntry(
        handle->urls.controlURL,
        handle->data.first.servicetype,
        port_str.c_str(),
        proto,
        nullptr /*remoteHost*/,
        std::data(int_client),
        std::data(int_port),
        nullptr /*desc*/,
        nullptr /*enabled*/,
        nullptr /*duration*/);
#elif (MINIUPNPC_API_VERSION >= 8) /* adds desc, enabled and leaseDuration args */
    int const err = UPNP_GetSpecificPortMappingEntry(
        handle->urls.controlURL,
        handle->data.first.servicetype,
        port_str.c_str(),
        proto,
        std::data(int_client),
        std::data(int_port),
        nullptr /*desc*/,
        nullptr /*enabled*/,
        nullptr /*duration*/);
#else
    int const err = UPNP_GetSpecificPortMappingEntry(
        handle->urls.controlURL,
        handle->data.first.servicetype,
        port_str.c_str(),
        proto,
        std::data(int_client),
        std::data(int_port));
#endif

    return err;
}

static int tr_upnpAddPortMapping(tr_upnp const* handle, char const* proto, tr_port port, char const* desc)
{
    int const old_errno = errno;
    errno = 0;

    auto const port_str = fmt::format(FMT_STRING("{:d}"), port.host());

#if (MINIUPNPC_API_VERSION >= 8)
    int const err = UPNP_AddPortMapping(
        handle->urls.controlURL,
        handle->data.first.servicetype,
        port_str.c_str(),
        port_str.c_str(),
        handle->lanaddr.c_str(),
        desc,
        proto,
        nullptr,
        nullptr);
#else
    int const err = UPNP_AddPortMapping(
        handle->urls.controlURL,
        handle->data.first.servicetype,
        port_str.c_str(),
        port_str.c_str(),
        handle->lanaddr.c_str(),
        desc,
        proto,
        nullptr);
#endif

    if (err != 0)
    {
        tr_logAddDebug(fmt::format("{} Port forwarding failed with error {}: {} ({})", proto, err, tr_strerror(errno), errno));
    }

    errno = old_errno;
    return err;
}

static void tr_upnpDeletePortMapping(tr_upnp const* handle, char const* proto, tr_port port)
{
    auto const port_str = fmt::format(FMT_STRING("{:d}"), port.host());

    UPNP_DeletePortMapping(handle->urls.controlURL, handle->data.first.servicetype, port_str.c_str(), proto, nullptr);
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

static auto* discoverThreadfunc(std::string bindaddr) // NOLINT performance-unnecessary-value-param
{
    return tr_upnpDiscover(2000, bindaddr.c_str());
}

template<typename T>
static bool isFutureReady(std::future<T> const& future)
{
    return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

tr_port_forwarding tr_upnpPulse(tr_upnp* handle, tr_port port, bool isEnabled, bool doPortCheck, std::string bindaddr)
{
    if (isEnabled && handle->state == UpnpState::WILL_DISCOVER)
    {
        TR_ASSERT(!handle->discover_future);

        auto task = std::packaged_task<UPNPDev*(std::string)>{ discoverThreadfunc };
        handle->discover_future = task.get_future();
        handle->state = UpnpState::DISCOVERING;

        std::thread(std::move(task), std::move(bindaddr)).detach();
    }

    if (isEnabled && handle->state == UpnpState::DISCOVERING && handle->discover_future &&
        isFutureReady(*handle->discover_future))
    {
        auto* const devlist = handle->discover_future->get();
        handle->discover_future.reset();

        FreeUPNPUrls(&handle->urls);
        auto lanaddr = std::array<char, TR_ADDRSTRLEN>{};
        if (UPNP_GetValidIGD(devlist, &handle->urls, &handle->data, std::data(lanaddr), std::size(lanaddr)) ==
            UPNP_IGD_VALID_CONNECTED)
        {
            tr_logAddInfo(fmt::format(_("Found Internet Gateway Device '{url}'"), fmt::arg("url", handle->urls.controlURL)));
            tr_logAddInfo(fmt::format(_("Local Address is '{address}'"), fmt::arg("address", std::data(handle->lanaddr))));
            handle->state = UpnpState::IDLE;
            handle->hasDiscovered = true;
            handle->lanaddr = std::data(lanaddr);
        }
        else
        {
            handle->state = UpnpState::FAILED;
            tr_logAddDebug(fmt::format("UPNP_GetValidIGD failed: {} ({})", tr_strerror(errno), errno));
            tr_logAddDebug("If your router supports UPnP, please make sure UPnP is enabled!");
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
        tr_logAddInfo(fmt::format(_("Port {port} is not forwarded"), fmt::arg("port", handle->port.host())));
        handle->isMapped = false;
    }

    if (handle->state == UpnpState::WILL_UNMAP)
    {
        tr_upnpDeletePortMapping(handle, "TCP", handle->port);
        tr_upnpDeletePortMapping(handle, "UDP", handle->port);

        tr_logAddInfo(fmt::format(
            _("Stopping port forwarding through '{url}', service '{type}'"),
            fmt::arg("url", handle->urls.controlURL),
            fmt::arg("type", handle->data.first.servicetype)));

        handle->isMapped = false;
        handle->state = UpnpState::IDLE;
        handle->port = {};
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
            auto const desc = fmt::format(FMT_STRING("{:s} at {:d}"), TR_NAME, port.host());
            int const err_tcp = tr_upnpAddPortMapping(handle, "TCP", port, desc.c_str());
            int const err_udp = tr_upnpAddPortMapping(handle, "UDP", port, desc.c_str());

            handle->isMapped = err_tcp == 0 || err_udp == 0;
        }

        tr_logAddDebug(fmt::format(
            _("Port forwarding through '{url}', service '{type}'. (local address: {address}:{port})"),
            fmt::arg("url", handle->urls.controlURL),
            fmt::arg("type", handle->data.first.servicetype),
            fmt::arg("address", handle->lanaddr),
            fmt::arg("port", port.host())));

        if (handle->isMapped)
        {
            tr_logAddInfo(fmt::format(_("Port {port} is forwarded"), fmt::arg("port", port.host())));
            handle->port = port;
            handle->state = UpnpState::IDLE;
        }
        else
        {
            tr_logAddInfo(_("If your router supports UPnP, please make sure UPnP is enabled!"));
            handle->port = {};
            handle->state = UpnpState::FAILED;
        }
    }

    return portFwdState(handle->state, handle->isMapped);
}
