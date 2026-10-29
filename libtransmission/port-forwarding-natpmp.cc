// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <string>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h> // AF_INET
#endif

#include <event2/util.h> /* evutil_inet_ntop() */

#include <fmt/format.h>

#define ENABLE_STRNATPMPERR
#include "natpmp.h"

#define LIBTRANSMISSION_PORT_FORWARDING_MODULE

#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/port-forwarding-natpmp.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/utils.h"

namespace
{
[[nodiscard]] std::string ipv4_str(void const* addr)
{
    auto str = std::array<char, 128>{};
    evutil_inet_ntop(AF_INET, addr, std::data(str), std::size(str));
    return std::data(str);
}
} // namespace

std::string tr_natpmp::gatewayStr() const
{
    return ipv4_str(&natpmp_.gateway);
}

void tr_natpmp::onFailure(char const* func, int ret)
{
    auto const err = errno;
    state_ = State::Err;

    tr_logAddDebug(
        fmt::format(
            "{} failed. Natpmp returned {} ({}); errno is {} ({})",
            func,
            ret,
            strnatpmperr(ret),
            err,
            tr_strerror(err)));

    if (is_failing_)
    {
        return;
    }

    is_failing_ = true;
    tr_logAddWarn(
        fmt::format(
            fmt::runtime(_("NAT-PMP gateway '{gateway}' failed: {error} ({errmsg} ({errno}))")),
            fmt::arg("gateway", gatewayStr()),
            fmt::arg("error", strnatpmperr(ret)),
            fmt::arg("errmsg", tr_strerror(err)),
            fmt::arg("errno", err)));
}

bool tr_natpmp::canSendCommand() const
{
    return tr_time() >= command_time_;
}

void tr_natpmp::setCommandTime()
{
    command_time_ = tr_time() + CommandWaitSecs;
}

tr_natpmp::PulseResult tr_natpmp::pulse(tr_port local_port, bool is_enabled)
{
    if (is_enabled && state_ == State::Discover)
    {
        has_discovered_ = true;
        setCommandTime();

        if (auto const val = gateway_ ? initnatpmp(&natpmp_, 1, gateway_->addr.addr4.s_addr) : initnatpmp(&natpmp_, 0, 0);
            val < 0)
        {
            onFailure("initnatpmp", val);
        }
        else
        {
            tr_logAddInfo(
                fmt::format(
                    fmt::runtime(
                        gateway_ ? _("Using NAT-PMP gateway '{gateway}' from natpmp_gateway_ipv4") :
                                   _("Using NAT-PMP gateway '{gateway}' from the default route")),
                    fmt::arg("gateway", gatewayStr())));

            tr_logAddDebug(fmt::format("Asking NAT-PMP gateway '{}' for its public address", gatewayStr()));
            if (auto const sent = sendpublicaddressrequest(&natpmp_); sent < 0)
            {
                onFailure("sendpublicaddressrequest", sent);
            }
            else
            {
                state_ = State::RecvPub;
            }
        }
    }

    if (state_ == State::RecvPub && canSendCommand())
    {
        natpmpresp_t response;
        auto const val = readnatpmpresponseorretry(&natpmp_, &response);

        if (val >= 0)
        {
            tr_logAddInfo(
                fmt::format(
                    fmt::runtime(_("NAT-PMP gateway '{gateway}' reports public address '{address}'")),
                    fmt::arg("gateway", gatewayStr()),
                    fmt::arg("address", ipv4_str(&response.pnu.publicaddress.addr))));
            state_ = State::Idle;
            is_failing_ = false;
        }
        else if (val != NATPMP_TRYAGAIN)
        {
            onFailure("readnatpmpresponseorretry", val);
        }
    }

    if ((state_ == State::Idle || state_ == State::Err) && is_mapped_ && (!is_enabled || local_port_ != local_port))
    {
        state_ = State::SendUnmap;
    }

    if (state_ == State::SendUnmap && canSendCommand())
    {
        tr_logAddDebug(fmt::format("Asking NAT-PMP gateway '{}' to stop forwarding port {}", gatewayStr(), local_port_.host()));
        auto const val = sendnewportmappingrequest(
            &natpmp_,
            NATPMP_PROTOCOL_TCP,
            local_port_.host(),
            advertised_port_.host(),
            0);
        if (val < 0)
        {
            onFailure("sendnewportmappingrequest", val);
        }
        else
        {
            state_ = State::RecvUnmap;
        }
        setCommandTime();
    }

    if (state_ == State::RecvUnmap)
    {
        auto resp = natpmpresp_t{};
        auto const val = readnatpmpresponseorretry(&natpmp_, &resp);

        if (val >= 0)
        {
            auto const unmapped_port = tr_port::from_host(resp.pnu.newportmapping.privateport);
            is_failing_ = false;

            tr_logAddInfo(
                fmt::format(
                    fmt::runtime(_("NAT-PMP gateway '{gateway}' stopped forwarding port {port}")),
                    fmt::arg("gateway", gatewayStr()),
                    fmt::arg("port", unmapped_port.host())));

            if (local_port_ == unmapped_port)
            {
                local_port_.clear();
                advertised_port_.clear();
                state_ = State::Idle;
                is_mapped_ = false;
            }
        }
        else if (val != NATPMP_TRYAGAIN)
        {
            onFailure("readnatpmpresponseorretry", val);
        }
    }

    if ((state_ == State::Idle || state_ == State::Err) &&
        (is_mapped_ ? tr_time() >= renew_time_ : is_enabled && has_discovered_))
    {
        state_ = State::SendMap;
    }

    if (state_ == State::SendMap && canSendCommand())
    {
        tr_logAddDebug(
            fmt::format(
                "Asking NAT-PMP gateway '{}' to forward port {} for {} seconds",
                gatewayStr(),
                local_port.host(),
                LifetimeSecs));
        auto const val = sendnewportmappingrequest(
            &natpmp_,
            NATPMP_PROTOCOL_TCP,
            local_port.host(),
            local_port.host(),
            LifetimeSecs);
        if (val < 0)
        {
            onFailure("sendnewportmappingrequest", val);
        }
        else
        {
            state_ = State::RecvMap;
        }
        setCommandTime();
    }

    if (state_ == State::RecvMap)
    {
        auto resp = natpmpresp_t{};
        auto const val = readnatpmpresponseorretry(&natpmp_, &resp);

        if (val >= 0)
        {
            state_ = State::Idle;
            is_failing_ = false;
            is_mapped_ = true;
            renew_time_ = tr_time() + (resp.pnu.newportmapping.lifetime / 2);
            local_port_ = tr_port::from_host(resp.pnu.newportmapping.privateport);
            advertised_port_ = tr_port::from_host(resp.pnu.newportmapping.mappedpublicport);
            tr_logAddInfo(
                fmt::format(
                    fmt::runtime(_(
                        "NAT-PMP gateway '{gateway}' forwarded port {private_port} to public port {public_port} for {lifetime} seconds")),
                    fmt::arg("gateway", gatewayStr()),
                    fmt::arg("private_port", local_port_.host()),
                    fmt::arg("public_port", advertised_port_.host()),
                    fmt::arg("lifetime", resp.pnu.newportmapping.lifetime)));
        }
        else if (val != NATPMP_TRYAGAIN)
        {
            onFailure("readnatpmpresponseorretry", val);
        }
    }

    switch (state_)
    {
    case State::Idle:
        return {
            .state = is_mapped_ ? TR_PORT_MAPPED : TR_PORT_UNMAPPED,
            .local_port = local_port_,
            .advertised_port = advertised_port_,
        };

    case State::Discover:
        return { .state = TR_PORT_UNMAPPED, .local_port = {}, .advertised_port = {} };

    case State::RecvPub:
    case State::SendMap:
    case State::RecvMap:
        return { .state = TR_PORT_MAPPING, .local_port = {}, .advertised_port = {} };

    case State::SendUnmap:
    case State::RecvUnmap:
        return { .state = TR_PORT_UNMAPPING, .local_port = {}, .advertised_port = {} };

    default:
        return { .state = TR_PORT_ERROR, .local_port = {}, .advertised_port = {} };
    }
}
