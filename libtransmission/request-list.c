/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <assert.h>
#include "transmission.h"
#include "request-list.h"
#include "utils.h"

static int
compareRequests( const struct peer_request * a,
                 const struct peer_request * b )
{
    if( a->index != b->index )
        return a->index < b->index ? -1 : 1;

    if( a->offset != b->offset )
        return a->offset < b->offset ? -1 : 1;

    if( a->length != b->length )
        return a->length < b->length ? -1 : 1;

    return 0;
}

const struct request_list REQUEST_LIST_INIT = { 0, 0, NULL, NULL };

static void
reqListReserve( struct request_list * list, size_t max )
{
    if( list->max < max )
    {
        list->max = max;
        list->fifo = tr_renew( struct peer_request, list->fifo, list->max );
        list->sort = tr_renew( struct peer_request, list->sort, list->max );
    }
}

void
reqListClear( struct request_list * list )
{
    tr_free( list->fifo );
    tr_free( list->sort );
    *list = REQUEST_LIST_INIT;
}

void
reqListCopy( struct request_list * dest, const struct request_list * src )
{
    dest->len = dest->max = src->len;
    dest->fifo = tr_memdup( src->fifo, dest->len * sizeof( struct peer_request ) );
    dest->sort = tr_memdup( src->sort, dest->len * sizeof( struct peer_request ) );
}

typedef int (*compareFunc)(const void * a, const void * b );

static int
reqListSortPos( const struct request_list * list,
                const struct peer_request * req,
                tr_bool                   * exactMatch )
{
    return tr_lowerBound( req,
                          list->sort,
                          list->len,
                          sizeof( struct peer_request ), 
                          (compareFunc)compareRequests,
                          exactMatch );
}

void
reqListAppend( struct request_list *       list,
               const struct peer_request * req )
{
    int low;

    reqListReserve( list, list->len + 8 );

    /* append into list->fifo */
    list->fifo[list->len] = *req;

    /* insert into list->sort */
    low = reqListSortPos( list, req, NULL );
    memmove( &list->sort[low+1], &list->sort[low], (list->len-low)*sizeof(struct peer_request) );
    list->sort[low] = *req;

    ++list->len;
}

static tr_bool
reqListRemoveFromSorted( struct request_list * list, const struct peer_request * key )
{
    tr_bool found;
    const int low = reqListSortPos( list, key, &found );
    if( found )
        memmove( &list->sort[low], &list->sort[low+1], (list->len-low-1)*sizeof(struct peer_request));
    return found;
}

static void
reqListRemoveNthFromFifo( struct request_list * list, int n )
{
    memmove( &list->fifo[n], &list->fifo[n+1], (list->len-1)*sizeof(struct peer_request));
}

tr_bool
reqListPop( struct request_list * list,
            struct peer_request * setme )
{
    tr_bool success;

    if( !list->len )
    {
        success = FALSE;
    }
    else
    {
        *setme = list->fifo[0];
        reqListRemoveNthFromFifo( list, 0 );
        reqListRemoveFromSorted( list, setme );
        --list->len;
        success = TRUE;
    }

    return success;
}

tr_bool
reqListHas( const struct request_list * list,
            const struct peer_request * key )
{
    tr_bool exactMatch;
    reqListSortPos( list, key, &exactMatch );
    return exactMatch;
}

tr_bool
reqListRemove( struct request_list       * list,
               const struct peer_request * key )
{
    tr_bool found = reqListRemoveFromSorted( list, key );

    if( found )
    {
        size_t i;
        for( i=0; i<list->len; ++i )
            if( !compareRequests( &list->fifo[i], key ) )
                break;
        assert( i < list->len );
        reqListRemoveNthFromFifo( list, i );
        --list->len;
    }

    return found;
}
