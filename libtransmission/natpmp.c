/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>
#include <string.h> /* strerror */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <libnatpmp/natpmp.h>

#include "transmission.h"
#include "natpmp.h"
#include "shared.h"
#include "utils.h"

#define LIFETIME_SECS 3600
#define COMMAND_WAIT_SECS 8

#define KEY "Port Mapping (NAT-PMP): "

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
    int port;
    unsigned int isMapped      : 1;
    unsigned int hasDiscovered : 1;
    time_t renewTime;
    time_t commandTime;
    tr_natpmp_state state;
    natpmp_t natpmp;
};

/**
***
**/

static void
logVal( const char * func, int ret )
{
    if( ret==NATPMP_TRYAGAIN )
        tr_dbg( KEY "%s returned 'try again'", func );
    else if( ret >= 0 )
        tr_dbg( KEY "%s returned success (%d)", func, ret );
    else
        tr_err( KEY "%s returned error %d, errno is %d (%s)", func, ret, errno, strerror(errno) );
}

struct tr_natpmp*
tr_natpmpInit( void )
{
    struct tr_natpmp * nat;
    nat = tr_new0( struct tr_natpmp, 1 );
    nat->state = TR_NATPMP_DISCOVER;
    nat->port = -1;
    return nat;
}

void
tr_natpmpClose( tr_natpmp * nat )
{
    assert( !nat->isMapped );
    assert( ( nat->state == TR_NATPMP_IDLE )
         || ( nat->state == TR_NATPMP_ERR )
         || ( nat->state == TR_NATPMP_DISCOVER ) );

    closenatpmp( &nat->natpmp );
    tr_free( nat );
}

static int
canSendCommand( const struct tr_natpmp * nat )
{
    return time(NULL) >= nat->commandTime;
}

static void
setCommandTime( struct tr_natpmp * nat )
{
    nat->commandTime = time(NULL) + COMMAND_WAIT_SECS;
}

static void
setErrorState( struct tr_natpmp * nat )
{
    tr_err( KEY "If your router supports NAT-PMP, please make sure NAT-PMP is enabled!" );
    tr_err( KEY "NAT-PMP port forwarding unsuccessful, trying UPnP next" );
    nat->state = TR_NATPMP_ERR;
}

int
tr_natpmpPulse( struct tr_natpmp * nat, int port, int isEnabled )
{
    int ret;

    if( isEnabled && ( nat->state == TR_NATPMP_DISCOVER ) )
    {
        int val = initnatpmp( &nat->natpmp );
        logVal( "initnatpmp", val );
        val = sendpublicaddressrequest( &nat->natpmp );
        logVal( "sendpublicaddressrequest", val );
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_PUB;
        nat->hasDiscovered = 1;
        setCommandTime( nat );
    }

    if( ( nat->state == TR_NATPMP_RECV_PUB ) && canSendCommand( nat ) )
    {
        natpmpresp_t response;
        const int val = readnatpmpresponseorretry( &nat->natpmp, &response );
        logVal( "readnatpmpresponseorretry", val );
        if( val >= 0 ) {
            tr_inf( KEY "found public address %s", inet_ntoa( response.publicaddress.addr ) );
            nat->state = TR_NATPMP_IDLE;
        } else if( val != NATPMP_TRYAGAIN ) {
            setErrorState( nat );
        }
    }

    if( ( nat->state == TR_NATPMP_IDLE ) || ( nat->state == TR_NATPMP_ERR ) )
    {
        if( nat->isMapped && ( !isEnabled || ( nat->port != port ) ) )
            nat->state = TR_NATPMP_SEND_UNMAP;
    }

    if( ( nat->state == TR_NATPMP_SEND_UNMAP ) && canSendCommand( nat ) )
    {
        const int val = sendnewportmappingrequest( &nat->natpmp, NATPMP_PROTOCOL_TCP, nat->port, nat->port, 0 );
        logVal( "sendnewportmappingrequest", val );
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_UNMAP;
        setCommandTime( nat );
    }

    if( nat->state == TR_NATPMP_RECV_UNMAP )
    {
        natpmpresp_t resp;
        const int val = readnatpmpresponseorretry( &nat->natpmp, &resp );
        logVal( "readnatpmpresponseorretry", val );
        if( val >= 0 ) {
            tr_inf( KEY "port %d has been unmapped.", nat->port );
            nat->state = TR_NATPMP_IDLE;
            nat->port = -1;
            nat->isMapped = 0;
        } else if( val != NATPMP_TRYAGAIN ) {
            setErrorState( nat );
        }
    }

    if( nat->state == TR_NATPMP_IDLE )
    {
        if( isEnabled && !nat->isMapped && nat->hasDiscovered )
            nat->state = TR_NATPMP_SEND_MAP;

        else if( nat->isMapped && time(NULL) >= nat->renewTime )
            nat->state = TR_NATPMP_SEND_MAP;
    }

    if( ( nat->state == TR_NATPMP_SEND_MAP ) && canSendCommand( nat ) )
    {
        const int val = sendnewportmappingrequest( &nat->natpmp, NATPMP_PROTOCOL_TCP, port, port, LIFETIME_SECS );
        logVal( "sendnewportmappingrequest", val );
        nat->state = val < 0 ? TR_NATPMP_ERR : TR_NATPMP_RECV_MAP;
        setCommandTime( nat );
    }

    if( nat->state == TR_NATPMP_RECV_MAP )
    {
        natpmpresp_t resp;
        const int val = readnatpmpresponseorretry( &nat->natpmp, &resp );
        logVal( "readnatpmpresponseorretry", val );
        if( val >= 0 ) {
            nat->state = TR_NATPMP_IDLE;
            nat->isMapped = 1;
            nat->renewTime = time( NULL ) + LIFETIME_SECS;
            nat->port = resp.newportmapping.privateport;
            tr_inf( KEY "port %d mapped successfully", nat->port );
        } else if( val != NATPMP_TRYAGAIN ) {
            setErrorState( nat );
        }
    }

    switch( nat->state ) {
        case TR_NATPMP_IDLE:        ret = nat->isMapped ? TR_NAT_TRAVERSAL_MAPPED : TR_NAT_TRAVERSAL_UNMAPPED; break;
        case TR_NATPMP_DISCOVER:    ret = TR_NAT_TRAVERSAL_UNMAPPED; break;
        case TR_NATPMP_RECV_PUB:
        case TR_NATPMP_SEND_MAP:
        case TR_NATPMP_RECV_MAP:    ret = TR_NAT_TRAVERSAL_MAPPING; break;
        case TR_NATPMP_SEND_UNMAP:
        case TR_NATPMP_RECV_UNMAP:  ret = TR_NAT_TRAVERSAL_UNMAPPING; break;
        default:                    ret = TR_NAT_TRAVERSAL_ERROR; break;
    }
    return ret;
}
