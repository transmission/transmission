/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <errno.h>
#include <time.h>
#include <inttypes.h>

#include <event2/util.h> /* evutil_inet_ntop () */

#define ENABLE_STRNATPMPERR
#include "natpmp.h"

#include "transmission.h"
#include "natpmp_local.h"
#include "log.h"
#include "net.h" /* tr_netCloseSocket */
#include "port-forwarding.h"
#include "utils.h"

#define LIFETIME_SECS 3600
#define COMMAND_WAIT_SECS 8

static const char *
getKey (void) { return _("Port Forwarding (NAT-PMP)"); }

typedef enum
{
    TR_NATPMP_IDLE,
    TR_NATPMP_ERR,
    TR_NATPMP_DISCOVER,
    TR_NATPMP_RECV_PUB,
    TR_NATPMP_SEND_MAP,
    TR_NATPMP_RECV_MAP,
    TR_NATPMP_SEND_UNMAP,
    TR_NATPMP_RECV_UNMAP
}
tr_natpmp_state;

struct tr_natpmp
{
    bool              has_discovered;
    bool              is_mapped;

    tr_port           public_port;
    tr_port           private_port;

    time_t            renew_time;
    time_t            command_time;
    tr_natpmp_state   state;
    natpmp_t          natpmp;
};

/**
***
**/

static void
logVal (const char * func,
        int          ret)
{
    if (ret == NATPMP_TRYAGAIN)
        return;
    if (ret >= 0)
        tr_logAddNamedInfo (getKey (), _("%s succeeded (%d)"), func, ret);
    else
        tr_logAddNamedDbg (
             getKey (),
            "%s failed. Natpmp returned %d (%s); errno is %d (%s)",
            func, ret, strnatpmperr (ret), errno, tr_strerror (errno));
}

struct tr_natpmp*
tr_natpmpInit (void)
{
    struct tr_natpmp * nat;

    nat = tr_new0 (struct tr_natpmp, 1);
    nat->state = TR_NATPMP_DISCOVER;
    nat->public_port = 0;
    nat->private_port = 0;
    nat->natpmp.s = TR_BAD_SOCKET; /* socket */
    return nat;
}

void
tr_natpmpClose (tr_natpmp * nat)
{
    if (nat)
    {
        closenatpmp (&nat->natpmp);
        tr_free (nat);
    }
}

static bool
canSendCommand (const struct tr_natpmp * nat)
{
    return tr_time () >= nat->command_time;
}

static void
setCommandTime (struct tr_natpmp * nat)
{
    nat->command_time = tr_time () + COMMAND_WAIT_SECS;
}

int
tr_natpmpPulse (struct tr_natpmp * nat, tr_port private_port, bool is_enabled, tr_port * public_port)
{
    int ret;

    if (is_enabled && (nat->state == TR_NATPMP_DISCOVER))
    {
        int val = initnatpmp (&nat->natpmp, 0, 0);
        logVal ("initnatpmp", val);
        val = sendpublicaddressrequest (&nat->natpmp);
        logVal ("sendpublicaddressrequest", val);
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_PUB;
        nat->has_discovered = true;
        setCommandTime (nat);
    }

    if ((nat->state == TR_NATPMP_RECV_PUB) && canSendCommand (nat))
    {
        natpmpresp_t response;
        const int val = readnatpmpresponseorretry (&nat->natpmp, &response);
        logVal ("readnatpmpresponseorretry", val);
        if (val >= 0)
        {
            char str[128];
            evutil_inet_ntop (AF_INET, &response.pnu.publicaddress.addr, str, sizeof (str));
            tr_logAddNamedInfo (getKey (), _("Found public address \"%s\""), str);
            nat->state = TR_NATPMP_IDLE;
        }
        else if (val != NATPMP_TRYAGAIN)
        {
            nat->state = TR_NATPMP_ERR;
        }
    }

    if ((nat->state == TR_NATPMP_IDLE) || (nat->state == TR_NATPMP_ERR))
    {
        if (nat->is_mapped && (!is_enabled || (nat->private_port != private_port)))
            nat->state = TR_NATPMP_SEND_UNMAP;
    }

    if ((nat->state == TR_NATPMP_SEND_UNMAP) && canSendCommand (nat))
    {
        const int val = sendnewportmappingrequest (&nat->natpmp, NATPMP_PROTOCOL_TCP,
                                                   nat->private_port,
                                                   nat->public_port,
                                                   0);
        logVal ("sendnewportmappingrequest", val);
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_UNMAP;
        setCommandTime (nat);
    }

    if (nat->state == TR_NATPMP_RECV_UNMAP)
    {
        natpmpresp_t resp;
        const int val = readnatpmpresponseorretry (&nat->natpmp, &resp);
        logVal ("readnatpmpresponseorretry", val);
        if (val >= 0)
        {
            const int private_port = resp.pnu.newportmapping.privateport;

            tr_logAddNamedInfo (getKey (), _("no longer forwarding port %d"), private_port);

            if (nat->private_port == private_port)
            {
                nat->private_port = 0;
                nat->public_port = 0;
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
            nat->state = TR_NATPMP_SEND_MAP;

        else if (nat->is_mapped && tr_time () >= nat->renew_time)
            nat->state = TR_NATPMP_SEND_MAP;
    }

    if ((nat->state == TR_NATPMP_SEND_MAP) && canSendCommand (nat))
    {
        const int val = sendnewportmappingrequest (&nat->natpmp, NATPMP_PROTOCOL_TCP, private_port, private_port, LIFETIME_SECS);
        logVal ("sendnewportmappingrequest", val);
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_MAP;
        setCommandTime (nat);
    }

    if (nat->state == TR_NATPMP_RECV_MAP)
    {
        natpmpresp_t resp;
        const int    val = readnatpmpresponseorretry (&nat->natpmp, &resp);
        logVal ("readnatpmpresponseorretry", val);
        if (val >= 0)
        {
            nat->state = TR_NATPMP_IDLE;
            nat->is_mapped = true;
            nat->renew_time = tr_time () + (resp.pnu.newportmapping.lifetime / 2);
            nat->private_port = resp.pnu.newportmapping.privateport;
            nat->public_port = resp.pnu.newportmapping.mappedpublicport;
            tr_logAddNamedInfo (getKey (), _("Port %d forwarded successfully"), nat->private_port);
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
            return nat->is_mapped ? TR_PORT_MAPPED : TR_PORT_UNMAPPED;
            break;

        case TR_NATPMP_DISCOVER:
            ret = TR_PORT_UNMAPPED; break;

        case TR_NATPMP_RECV_PUB:
        case TR_NATPMP_SEND_MAP:
        case TR_NATPMP_RECV_MAP:
            ret = TR_PORT_MAPPING; break;

        case TR_NATPMP_SEND_UNMAP:
        case TR_NATPMP_RECV_UNMAP:
            ret = TR_PORT_UNMAPPING; break;

        default:
            ret = TR_PORT_ERROR; break;
    }
    return ret;
}

