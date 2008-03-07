/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
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
#include <stdio.h> /* snprintf */

#include <miniupnp/miniwget.h>
#include <miniupnp/miniupnpc.h>
#include <miniupnp/upnpcommands.h>

#include "transmission.h"
#include "internal.h"
#include "shared.h"
#include "utils.h"
#include "upnp.h"

static const char * getKey( void ) { return _( "Port Mapping (UPnP)" ); }

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
    struct UPNPUrls urls;
    struct IGDdatas data;
    int port;
    char lanaddr[16];
    unsigned int isMapped;
    unsigned int hasDiscovered : 1;
    tr_upnp_state state;
};

/**
***
**/

tr_upnp*
tr_upnpInit( void )
{
    tr_upnp * ret = tr_new0( tr_upnp, 1 );
    ret->state = TR_UPNP_DISCOVER;
    ret->port = -1;
    return ret;
}

void
tr_upnpClose( tr_upnp * handle )
{
    assert( !handle->isMapped );
    assert( ( handle->state == TR_UPNP_IDLE )
         || ( handle->state == TR_UPNP_ERR )
         || ( handle->state == TR_UPNP_DISCOVER ) );

    if( handle->hasDiscovered )
        FreeUPNPUrls( &handle->urls );
    tr_free( handle );
}

/**
***
**/

int
tr_upnpPulse( tr_upnp * handle, int port, int isEnabled )
{
    int ret;

    if( isEnabled && ( handle->state == TR_UPNP_DISCOVER ) )
    {
        struct UPNPDev * devlist;
        errno = 0;
        devlist = upnpDiscover( 2000, NULL, NULL );
        if( devlist == NULL ) {
            tr_err( _( "%s: upnpDiscover returned NULL (errno %d - %s)" ), getKey(), errno, tr_strerror(errno) );
        }
        errno = 0;
        if( UPNP_GetValidIGD( devlist, &handle->urls, &handle->data, handle->lanaddr, sizeof(handle->lanaddr))) {
            tr_inf( _( "%s: Found Internet Gateway Device '%s'" ), getKey(), handle->urls.controlURL );
            tr_inf( _( "%s: Local Address is '%s'" ), getKey(), handle->lanaddr );
            handle->state = TR_UPNP_IDLE;
            handle->hasDiscovered = 1;
        } else {
            handle->state = TR_UPNP_ERR;
            tr_err( _( "%s: UPNP_GetValidIGD failed (errno %d - %s)" ), getKey(), errno, tr_strerror(errno) );
            tr_err( _( "%s: If your router supports UPnP, please make sure UPnP is enabled!" ), getKey() );
        }
        freeUPNPDevlist( devlist );
    }

    if( handle->state == TR_UPNP_IDLE )
    {
        if( handle->isMapped && ( !isEnabled || ( handle->port != port ) ) )
            handle->state = TR_UPNP_UNMAP;
    }

    if( handle->state == TR_UPNP_UNMAP )
    {
        char portStr[16];
        snprintf( portStr, sizeof(portStr), "%d", handle->port );
        UPNP_DeletePortMapping( handle->urls.controlURL,
                                handle->data.servicetype,
                                portStr, "TCP" );
        tr_dbg( _( "%s: Stopping port forwarding of '%s', service '%s'" ),
                getKey(), handle->urls.controlURL, handle->data.servicetype );
        handle->isMapped = 0;
        handle->state = TR_UPNP_IDLE;
        handle->port = -1;
    }

    if( handle->state == TR_UPNP_IDLE )
    {
        if( isEnabled && !handle->isMapped )
            handle->state = TR_UPNP_MAP;
    }

    if( handle->state == TR_UPNP_MAP )
    {
        int err = -1;
        char portStr[16];
        snprintf( portStr, sizeof(portStr), "%d", port );
        errno = 0;

        if( !handle->urls.controlURL || !handle->data.servicetype )
            handle->isMapped = 0;
        else {
            err = UPNP_AddPortMapping( handle->urls.controlURL,
                                       handle->data.servicetype,
                                       portStr, portStr, handle->lanaddr,
                                       "Transmission", "TCP" );
            handle->isMapped = !err;
        }
        tr_inf( _( "%s: Port forwarding via '%s', service '%s'.  (local address: %s:%d)" ),
                getKey(), handle->urls.controlURL, handle->data.servicetype, handle->lanaddr, port );
        if( handle->isMapped ) {
            tr_inf( _( "%s: Port forwarding successful!" ), getKey() );
            handle->port = port;
            handle->state = TR_UPNP_IDLE;
        } else {
            tr_err( _( "%s: Port forwarding failed with error %d (%d - %s)" ), getKey(), err, errno, tr_strerror(errno) );
            tr_err( _( "%s: If your router supports UPnP, please make sure UPnP is enabled!" ), getKey() );
            handle->port = -1;
            handle->state = TR_UPNP_ERR;
        }
    }

    switch( handle->state )
    {
        case TR_UPNP_DISCOVER: ret = TR_NAT_TRAVERSAL_UNMAPPED; break;
        case TR_UPNP_MAP:      ret = TR_NAT_TRAVERSAL_MAPPING; break;
        case TR_UPNP_UNMAP:    ret = TR_NAT_TRAVERSAL_UNMAPPING; break;
        case TR_UPNP_IDLE:     ret = handle->isMapped ? TR_NAT_TRAVERSAL_MAPPED
                                                      : TR_NAT_TRAVERSAL_UNMAPPED; break;
        default:               ret = TR_NAT_TRAVERSAL_ERROR; break;
    }

    return ret;
}
