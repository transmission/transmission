/*
 * This file Copyright (C) 2007-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <errno.h>
#include <time.h>
#include <inttypes.h>

#define ENABLE_STRNATPMPERR
#include <libnatpmp/natpmp.h>

#include "transmission.h"
#include "natpmp.h"
#include "net.h" /* inet_ntoa() */
#include "port-forwarding.h"
#include "utils.h"

#define LIFETIME_SECS 3600
#define COMMAND_WAIT_SECS 8

static const char *
getKey( void ) { return _( "Port Forwarding (NAT-PMP)" ); }

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
    tr_bool           has_discovered;
    tr_bool           is_mapped;

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
logVal( const char * func,
        int          ret )
{
    if( ret == NATPMP_TRYAGAIN )
        return;
    if( ret >= 0 )
        tr_ninf( getKey( ), _( "%s succeeded (%d)" ), func, ret );
    else
        tr_ndbg(
             getKey( ),
            "%s failed.  natpmp returned %d (%s); errno is %d (%s)",
            func, ret, strnatpmperr( ret ), errno, tr_strerror( errno ) );
}

struct tr_natpmp*
tr_natpmpInit( void )
{
    struct tr_natpmp * nat;

    nat = tr_new0( struct tr_natpmp, 1 );
    nat->state = TR_NATPMP_DISCOVER;
    nat->public_port = 0;
    nat->private_port = 0;
    nat->natpmp.s = -1; /* socket */
    return nat;
}

void
tr_natpmpClose( tr_natpmp * nat )
{
    if( nat )
    {
        if( nat->natpmp.s >= 0 )
            tr_netCloseSocket( nat->natpmp.s );
        tr_free( nat );
    }
}

static int
canSendCommand( const struct tr_natpmp * nat )
{
    return tr_time( ) >= nat->command_time;
}

static void
setCommandTime( struct tr_natpmp * nat )
{
    nat->command_time = tr_time( ) + COMMAND_WAIT_SECS;
}

int
tr_natpmpPulse( struct tr_natpmp * nat, tr_port private_port, tr_bool is_enabled, tr_port * public_port )
{
    int ret;

    if( is_enabled && ( nat->state == TR_NATPMP_DISCOVER ) )
    {
        int val = initnatpmp( &nat->natpmp );
        logVal( "initnatpmp", val );
        val = sendpublicaddressrequest( &nat->natpmp );
        logVal( "sendpublicaddressrequest", val );
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_PUB;
        nat->has_discovered = TRUE;
        setCommandTime( nat );
    }

    if( ( nat->state == TR_NATPMP_RECV_PUB ) && canSendCommand( nat ) )
    {
        natpmpresp_t response;
        const int val = readnatpmpresponseorretry( &nat->natpmp, &response );
        logVal( "readnatpmpresponseorretry", val );
        if( val >= 0 )
        {
            tr_ninf( getKey( ), _( "Found public address \"%s\"" ),
                     inet_ntoa( response.pnu.publicaddress.addr ) );
            nat->state = TR_NATPMP_IDLE;
        }
        else if( val != NATPMP_TRYAGAIN )
        {
            nat->state = TR_NATPMP_ERR;
        }
    }

    if( ( nat->state == TR_NATPMP_IDLE ) || ( nat->state == TR_NATPMP_ERR ) )
    {
        if( nat->is_mapped && ( !is_enabled || ( nat->private_port != private_port ) ) )
            nat->state = TR_NATPMP_SEND_UNMAP;
    }

    if( ( nat->state == TR_NATPMP_SEND_UNMAP ) && canSendCommand( nat ) )
    {
        const int val = sendnewportmappingrequest( &nat->natpmp, NATPMP_PROTOCOL_TCP,
                                                   nat->private_port,
                                                   nat->public_port,
                                                   0 );
        logVal( "sendnewportmappingrequest", val );
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_UNMAP;
        setCommandTime( nat );
    }

    if( nat->state == TR_NATPMP_RECV_UNMAP )
    {
        natpmpresp_t resp;
        const int val = readnatpmpresponseorretry( &nat->natpmp, &resp );
        logVal( "readnatpmpresponseorretry", val );
        if( val >= 0 )
        {
            const int private_port = resp.pnu.newportmapping.privateport;

            tr_ninf( getKey( ), _( "no longer forwarding port %d" ), private_port );

            if( nat->private_port == private_port )
            {
                nat->private_port = 0;
                nat->public_port = 0;
                nat->state = TR_NATPMP_IDLE;
                nat->is_mapped = FALSE;
            }
        }
        else if( val != NATPMP_TRYAGAIN )
        {
            nat->state = TR_NATPMP_ERR;
        }
    }

    if( nat->state == TR_NATPMP_IDLE )
    {
        if( is_enabled && !nat->is_mapped && nat->has_discovered )
            nat->state = TR_NATPMP_SEND_MAP;

        else if( nat->is_mapped && tr_time( ) >= nat->renew_time )
            nat->state = TR_NATPMP_SEND_MAP;
    }

    if( ( nat->state == TR_NATPMP_SEND_MAP ) && canSendCommand( nat ) )
    {
        const int val = sendnewportmappingrequest( &nat->natpmp, NATPMP_PROTOCOL_TCP, private_port, private_port, LIFETIME_SECS );
        logVal( "sendnewportmappingrequest", val );
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_MAP;
        setCommandTime( nat );
    }

    if( nat->state == TR_NATPMP_RECV_MAP )
    {
        natpmpresp_t resp;
        const int    val = readnatpmpresponseorretry( &nat->natpmp, &resp );
        logVal( "readnatpmpresponseorretry", val );
        if( val >= 0 )
        {
            nat->state = TR_NATPMP_IDLE;
            nat->is_mapped = TRUE;
            nat->renew_time = tr_time( ) + LIFETIME_SECS;
            nat->private_port = resp.pnu.newportmapping.privateport;
            nat->public_port = resp.pnu.newportmapping.mappedpublicport;
            tr_ninf( getKey( ), _( "Port %d forwarded successfully" ), nat->private_port );
        }
        else if( val != NATPMP_TRYAGAIN )
        {
            nat->state = TR_NATPMP_ERR;
        }
    }

    switch( nat->state )
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

