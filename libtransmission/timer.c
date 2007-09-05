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

#include <sys/types.h> /* u_char for event.h */
#include <event.h>

#include "transmission.h"
#include "trevent.h"
#include "timer.h"
#include "utils.h"

typedef int tr_timer_func ( void * user_data );
typedef void tr_data_free_func( void * user_data );

/***
****
***/

struct timer_node
{
    tr_handle_t * handle;
    struct event * event;
    tr_timer_func * func;
    void * user_data;
    tr_data_free_func * free_func;
    struct timeval tv;
    uint8_t doFree;
};

void
tr_timerFree( struct timer_node ** node )
{
    if( *node )
    {
        (*node)->doFree = 1;
        *node = NULL;
    }
}

static void
timerCB( int fd UNUSED, short event UNUSED, void * arg )
{
    struct timer_node * node = (struct timer_node *) arg;

    if( !node->doFree )
         node->doFree = !(node->func)(node->user_data);

    if( !node->doFree )
        tr_event_add( node->handle, node->event, &node->tv );
    else {
        if( node->free_func != NULL )
            (node->free_func)( node->user_data );
        tr_event_del( node->handle, node->event );
        tr_free( node );
    }
}


tr_timer_tag
tr_timerNew( tr_handle_t        * handle,
             tr_timer_func        func,
             void               * user_data,
             tr_data_free_func    free_func,
             int                  timeout_milliseconds )
{
    struct timer_node * node;

    assert( func != NULL );
    assert( timeout_milliseconds >= 0 );

    node = tr_new( struct timer_node, 1 );
    node->handle = handle;
    node->event = tr_new0( struct event, 1 );
    node->func = func;
    node->user_data = user_data;
    node->free_func = free_func;
    node->doFree = 0;
    node->tv = timevalMsec ( timeout_milliseconds );
    timeout_set( node->event, timerCB, node );
    tr_event_add( handle, node->event, &node->tv );
    return node;
}
