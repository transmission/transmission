// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <ctime>
#include <cstdint> // uint32_t

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

static auto constexpr LifetimeSecs = uint32_t{ 3600 };
static auto constexpr CommandWaitSecs = time_t{ 8 };

/**
***
**/

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

struct tr_natpmp* tr_natpmpInit()
{
    auto* const nat = tr_new0(struct tr_natpmp, 1);
    nat->state = TR_NATPMP_DISCOVER;
    nat->public_port.clear();
    nat->private_port.clear();
    nat->natpmp.s = TR_BAD_SOCKET; /* socket */
    return nat;
}

void tr_natpmpClose(tr_natpmp* nat)
{
    if (nat != nullptr)
    {
        closenatpmp(&nat->natpmp);
        tr_free(nat);
    }
}

static bool canSendCommand(struct tr_natpmp const* nat)
{
    return tr_time() >= nat->command_time;
}

static void setCommandTime(struct tr_natpmp* nat)
{
    nat->command_time = tr_time() + CommandWaitSecs;
}

tr_port_forwarding tr_natpmpPulse(
    struct tr_natpmp* nat,
    tr_port private_port,
    bool is_enabled,
    tr_port* public_port,
    tr_port* real_private_port)
{
    if (is_enabled && nat->state == TR_NATPMP_DISCOVER)
    {
        int val = initnatpmp(&nat->natpmp, 0, 0);
        logVal("initnatpmp", val);
        val = sendpublicaddressrequest(&nat->natpmp);
        logVal("sendpublicaddressrequest", val);
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_PUB;
        nat->has_discovered = true;
        setCommandTime(nat);
    }

    if (nat->state == TR_NATPMP_RECV_PUB && canSendCommand(nat))
    {
        natpmpresp_t response;
        int const val = readnatpmpresponseorretry(&nat->natpmp, &response);
        logVal("readnatpmpresponseorretry", val);

        if (val >= 0)
        {
            char str[128];
            evutil_inet_ntop(AF_INET, &response.pnu.publicaddress.addr, str, sizeof(str));
            tr_logAddInfo(fmt::format(_("Found public address '{address}'"), fmt::arg("address", str)));
            nat->state = TR_NATPMP_IDLE;
        }
        else if (val != NATPMP_TRYAGAIN)
        {
            nat->state = TR_NATPMP_ERR;
        }
    }

    if ((nat->state == TR_NATPMP_IDLE || nat->state == TR_NATPMP_ERR) && (nat->is_mapped) &&
        (!is_enabled || nat->private_port != private_port))
    {
        nat->state = TR_NATPMP_SEND_UNMAP;
    }

    if (nat->state == TR_NATPMP_SEND_UNMAP && canSendCommand(nat))
    {
        int const val = sendnewportmappingrequest(
            &nat->natpmp,
            NATPMP_PROTOCOL_TCP,
            nat->private_port.host(),
            nat->public_port.host(),
            0);
        logVal("sendnewportmappingrequest", val);
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_UNMAP;
        setCommandTime(nat);
    }

    if (nat->state == TR_NATPMP_RECV_UNMAP)
    {
        natpmpresp_t resp;
        int const val = readnatpmpresponseorretry(&nat->natpmp, &resp);
        logVal("readnatpmpresponseorretry", val);

        if (val >= 0)
        {
            auto const unmapped_port = tr_port::fromHost(resp.pnu.newportmapping.privateport);

            tr_logAddInfo(fmt::format(_("Port {port} is no longer forwarded"), fmt::arg("port", unmapped_port.host())));

            if (nat->private_port == unmapped_port)
            {
                nat->private_port.clear();
                nat->public_port.clear();
                nat->state = TR_NATPMP_IDLE;
                nat->is_mapped = false;
            }
        }
        else if (val != NATPMP_TRYAGAIN)
        {
            nat->state = TR_NATPMP_ERR;
        }
    }

    if (nat->state == TR_NATPMP_IDLE)
    {
        if (is_enabled && !nat->is_mapped && nat->has_discovered)
        {
            nat->state = TR_NATPMP_SEND_MAP;
        }
        else if (nat->is_mapped && tr_time() >= nat->renew_time)
        {
            nat->state = TR_NATPMP_SEND_MAP;
        }
    }

    if (nat->state == TR_NATPMP_SEND_MAP && canSendCommand(nat))
    {
        int const val = sendnewportmappingrequest(
            &nat->natpmp,
            NATPMP_PROTOCOL_TCP,
            private_port.host(),
            private_port.host(),
            LifetimeSecs);
        logVal("sendnewportmappingrequest", val);
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_MAP;
        setCommandTime(nat);
    }

    if (nat->state == TR_NATPMP_RECV_MAP)
    {
        natpmpresp_t resp;
        int const val = readnatpmpresponseorretry(&nat->natpmp, &resp);
        logVal("readnatpmpresponseorretry", val);

        if (val >= 0)
        {
            nat->state = TR_NATPMP_IDLE;
            nat->is_mapped = true;
            nat->renew_time = tr_time() + (resp.pnu.newportmapping.lifetime / 2);
            nat->private_port = tr_port::fromHost(resp.pnu.newportmapping.privateport);
            nat->public_port = tr_port::fromHost(resp.pnu.newportmapping.mappedpublicport);
            tr_logAddInfo(fmt::format(_("Port {port} forwarded successfully"), fmt::arg("port", nat->private_port.host())));
        }
        else if (val != NATPMP_TRYAGAIN)
        {
            nat->state = TR_NATPMP_ERR;
        }
    }

    switch (nat->state)
    {
    case TR_NATPMP_IDLE:
        *public_port = nat->public_port;
        *real_private_port = nat->private_port;
        return nat->is_mapped ? TR_PORT_MAPPED : TR_PORT_UNMAPPED;

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
