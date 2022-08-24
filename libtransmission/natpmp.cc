// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <cstdint> // uint32_t
#include <ctime>

#include <event2/util.h> /* evutil_inet_ntop() */

#include <fmt/core.h>

#define ENABLE_STRNATPMPERR
#include "natpmp.h"

#include "transmission.h"
#include "natpmp_local.h"
#include "log.h"
#include "net.h" /* tr_netCloseSocket */
#include "port-forwarding.h"
#include "utils.h"

static void logVal(char const* func, int ret)
{
    if (ret == NATPMP_TRYAGAIN)
    {
        return;
    }

    if (ret >= 0)
    {
        tr_logAddDebug(fmt::format("{} succeeded ({})", func, ret));
    }
    else
    {
        tr_logAddDebug(fmt::format(
            "{} failed. Natpmp returned {} ({}); errno is {} ({})",
            func,
            ret,
            strnatpmperr(ret),
            errno,
            tr_strerror(errno)));
    }
}

bool tr_natpmp::canSendCommand() const
{
    return tr_time() >= command_time_;
}

void tr_natpmp::setCommandTime()
{
    command_time_ = tr_time() + CommandWaitSecs;
}

tr_port_forwarding tr_natpmp::pulse(tr_port private_port, bool is_enabled, tr_port* public_port, tr_port* real_private_port)
{
    if (is_enabled && state_ == TR_NATPMP_DISCOVER)
    {
        int val = initnatpmp(&natpmp_, 0, 0);
        logVal("initnatpmp", val);
        val = sendpublicaddressrequest(&natpmp_);
        logVal("sendpublicaddressrequest", val);
        state_ = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_PUB;
        has_discovered_ = true;
        setCommandTime();
    }

    if (state_ == TR_NATPMP_RECV_PUB && canSendCommand())
    {
        natpmpresp_t response;
        int const val = readnatpmpresponseorretry(&natpmp_, &response);
        logVal("readnatpmpresponseorretry", val);

        if (val >= 0)
        {
            auto str = std::array<char, 128>{};
            evutil_inet_ntop(AF_INET, &response.pnu.publicaddress.addr, std::data(str), std::size(str));
            tr_logAddInfo(fmt::format(_("Found public address '{address}'"), fmt::arg("address", std::data(str))));
            state_ = TR_NATPMP_IDLE;
        }
        else if (val != NATPMP_TRYAGAIN)
        {
            state_ = TR_NATPMP_ERR;
        }
    }

    if ((state_ == TR_NATPMP_IDLE || state_ == TR_NATPMP_ERR) && is_mapped_ && (!is_enabled || private_port_ != private_port))
    {
        state_ = TR_NATPMP_SEND_UNMAP;
    }

    if (state_ == TR_NATPMP_SEND_UNMAP && canSendCommand())
    {
        int const val = sendnewportmappingrequest(&natpmp_, NATPMP_PROTOCOL_TCP, private_port_.host(), public_port_.host(), 0);
        logVal("sendnewportmappingrequest", val);
        state_ = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_UNMAP;
        setCommandTime();
    }

    if (state_ == TR_NATPMP_RECV_UNMAP)
    {
        natpmpresp_t resp;
        int const val = readnatpmpresponseorretry(&natpmp_, &resp);
        logVal("readnatpmpresponseorretry", val);

        if (val >= 0)
        {
            auto const unmapped_port = tr_port::fromHost(resp.pnu.newportmapping.privateport);

            tr_logAddInfo(fmt::format(_("Port {port} is no longer forwarded"), fmt::arg("port", unmapped_port.host())));

            if (private_port_ == unmapped_port)
            {
                private_port_.clear();
                public_port_.clear();
                state_ = TR_NATPMP_IDLE;
                is_mapped_ = false;
            }
        }
        else if (val != NATPMP_TRYAGAIN)
        {
            state_ = TR_NATPMP_ERR;
        }
    }

    if (state_ == TR_NATPMP_IDLE)
    {
        if (is_enabled && !is_mapped_ && has_discovered_)
        {
            state_ = TR_NATPMP_SEND_MAP;
        }
        else if (is_mapped_ && tr_time() >= renew_time_)
        {
            state_ = TR_NATPMP_SEND_MAP;
        }
    }

    if (state_ == TR_NATPMP_SEND_MAP && canSendCommand())
    {
        int const val = sendnewportmappingrequest(
            &natpmp_,
            NATPMP_PROTOCOL_TCP,
            private_port.host(),
            private_port.host(),
            LifetimeSecs);
        logVal("sendnewportmappingrequest", val);
        state_ = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_MAP;
        setCommandTime();
    }

    if (state_ == TR_NATPMP_RECV_MAP)
    {
        natpmpresp_t resp;
        int const val = readnatpmpresponseorretry(&natpmp_, &resp);
        logVal("readnatpmpresponseorretry", val);

        if (val >= 0)
        {
            state_ = TR_NATPMP_IDLE;
            is_mapped_ = true;
            renew_time_ = tr_time() + (resp.pnu.newportmapping.lifetime / 2);
            private_port_ = tr_port::fromHost(resp.pnu.newportmapping.privateport);
            public_port_ = tr_port::fromHost(resp.pnu.newportmapping.mappedpublicport);
            tr_logAddInfo(fmt::format(_("Port {port} forwarded successfully"), fmt::arg("port", private_port_.host())));
        }
        else if (val != NATPMP_TRYAGAIN)
        {
            state_ = TR_NATPMP_ERR;
        }
    }

    switch (state_)
    {
    case TR_NATPMP_IDLE:
        *public_port = public_port_;
        *real_private_port = private_port_;
        return is_mapped_ ? TR_PORT_MAPPED : TR_PORT_UNMAPPED;

    case TR_NATPMP_DISCOVER:
        return TR_PORT_UNMAPPED;

    case TR_NATPMP_RECV_PUB:
    case TR_NATPMP_SEND_MAP:
    case TR_NATPMP_RECV_MAP:
        return TR_PORT_MAPPING;

    case TR_NATPMP_SEND_UNMAP:
    case TR_NATPMP_RECV_UNMAP:
        return TR_PORT_UNMAPPING;

    default:
        return TR_PORT_ERROR;
    }
}
