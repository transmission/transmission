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

#include <stdio.h> /* printf */

#include <miniupnp/miniwget.h>
#include <miniupnp/miniupnpc.h>
#include <miniupnp/upnpcommands.h>

#include "transmission.h"
#include "internal.h"
#include "utils.h"
#include "upnp.h"

struct tr_upnp
{
    struct UPNPUrls urls;
    struct IGDdatas data;
    int port;
    char lanaddr[16];
    unsigned int isForwarding : 1;
    unsigned int isEnabled : 1;
};

/**
***
**/

tr_upnp*
tr_upnpInit( void )
{
    tr_upnp * ret = tr_new0( tr_upnp, 1 );
    struct UPNPDev * devlist = upnpDiscover( 2000, NULL );
    if( UPNP_GetValidIGD( devlist, &ret->urls, &ret->data, ret->lanaddr, sizeof(ret->lanaddr))) {
        tr_dbg( "Found Internet Gateway Device '%s'", ret->urls.controlURL );
        tr_dbg( "Local LAN IP Address is '%s'", ret->lanaddr );
    }
    ret->port = -1;
    freeUPNPDevlist( devlist );
    return ret;
}

void
tr_upnpClose( tr_upnp * handle )
{
    tr_upnpStop( handle );
    FreeUPNPUrls( &handle->urls );
    tr_free( handle );
}

/**
***
**/

void
tr_upnpStart( tr_upnp * handle )
{
    handle->isEnabled = 1;

    if( handle->port >= 0 )
    {
        char portStr[16];
        snprintf( portStr, sizeof(portStr), "%d", handle->port );
        handle->isForwarding = ( handle->urls.controlURL != NULL ) && 
                               ( handle->data.servicetype != NULL ) &&
                               ( UPNP_AddPortMapping( handle->urls.controlURL,
                                                      handle->data.servicetype,
                                                      portStr, portStr, handle->lanaddr,
                                                      "Transmission", "TCP" ) );

        tr_dbg( "UPNP Port Forwarding via '%s', service '%s'.  (local address: %s:%d)",
                handle->urls.controlURL, handle->data.servicetype, handle->lanaddr, handle->port );
        tr_dbg( "UPNP Port Forwarding Enabled?  %s", (handle->isForwarding?"Yes":"No") );
    }
}

void
tr_upnpRemoveForwarding ( tr_upnp * handle )
{
    handle->port = -1;

    if( handle->isForwarding )
    {
        char portStr[16];
        snprintf( portStr, sizeof(portStr), "%d", handle->port );

        UPNP_DeletePortMapping( handle->urls.controlURL,
                                handle->data.servicetype,
                                portStr, "TCP" );
        tr_dbg( "Stopping port forwarding of '%s', service '%s'",
                handle->urls.controlURL, handle->data.servicetype );

        handle->isForwarding = FALSE;
    }
}

void
tr_upnpForwardPort( tr_upnp * handle, int publicPort )
{
    tr_upnpRemoveForwarding( handle ); /* remove the old forwarding */

    handle->port = publicPort;

    if( handle->isEnabled )
        tr_upnpStart( handle );
}

void
tr_upnpStop( tr_upnp * handle )
{
    tr_upnpRemoveForwarding( handle );
    handle->isEnabled = 0;
}

int
tr_upnpStatus( tr_upnp * handle )
{
    if( !handle->isEnabled )
        return TR_NAT_TRAVERSAL_DISABLED;

    if( !handle->isForwarding )
        return TR_NAT_TRAVERSAL_ERROR;

    return TR_NAT_TRAVERSAL_MAPPED;
}

void
tr_upnpPulse( tr_upnp * handle UNUSED )
{
    /* no-op */
}
