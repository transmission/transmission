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
#include "list.h"
#include "publish.h"
#include "utils.h"

struct tr_publisher_node
{
   tr_delivery_func * func; 
   void * user_data;
};

struct tr_publisher_s
{
    tr_list * list;
};

tr_publisher_t*
tr_publisherNew( void )
{
    return tr_new0( tr_publisher_t, 1 );
}

void
tr_publisherFree( tr_publisher_t ** p )
{
    assert( p != NULL );
    assert( *p != NULL );

    tr_list_free( &(*p)->list );
    tr_free( *p );
    *p = NULL;
}

tr_publisher_tag
tr_publisherSubscribe ( tr_publisher_t    * p,
                        tr_delivery_func    func,
                        void              * user_data )
{
    struct tr_publisher_node * node = tr_new( struct tr_publisher_node, 1 );
    node->func = func;
    node->user_data = user_data;
    tr_list_append( &p->list, node );
    return node;
}

void
tr_publisherUnsubscribe( tr_publisher_t   * p,
                         tr_publisher_tag   tag )
{
    tr_list_remove_data( &p->list, tag );
    tr_free( tag );
}

void
tr_publisherPublish( tr_publisher_t  * p,
                     void            * source,
                     void            * event )
{
    tr_list * walk;
    for( walk=p->list; walk!=NULL; )
    {
        tr_list * next = walk->next;
        struct tr_publisher_node * node = (struct tr_publisher_node*)walk->data;
        (node->func)(source, event, node->user_data);
        walk = next;
    }
}
