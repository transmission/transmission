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

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_PEER_REQ_H
#define TR_PEER_REQ_H

#include <inttypes.h>

struct peer_request
{
    uint32_t    index;
    uint32_t    offset;
    uint32_t    length;
    time_t      time_requested;
};

struct request_list
{
    size_t                 len;
    size_t                 max;
    struct peer_request  * fifo;
    struct peer_request  * sort;
};

extern const struct request_list REQUEST_LIST_INIT;

void reqListClear( struct request_list * list );

void reqListCopy( struct request_list * dest, const struct request_list * src );

/* O(log(N)) */
tr_bool reqListHas( const struct request_list * list, const struct peer_request * key );

/* O(log(N) + 1) */
void reqListAppend( struct request_list * list, const struct peer_request * req );

/* O(log(N) + 1) */
tr_bool reqListPop( struct request_list * list, struct peer_request * setme );

/* O(N + log(N)) */
tr_bool reqListRemove( struct request_list * list, const struct peer_request * key );


#endif
