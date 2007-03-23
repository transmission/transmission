/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include "bsdtree.h"

typedef struct tr_peertree_entry_s
{
    RB_ENTRY( tr_peertree_entry_s ) magic;
    uint8_t peer[6];
} tr_peertree_entry_t;

typedef
RB_HEAD( tr_peertree_s, tr_peertree_entry_s )
tr_peertree_t;

#define peertreeInit(tree)       RB_INIT(  (tree) )
#define peertreeEmpty(tree)      RB_EMPTY( (tree) )
#define peertreeFirst(tree)      RB_MIN(   tr_peertree_s, (tree) )
#define peertreeNext(tree, item) RB_NEXT(  tr_peertree_s, (tree), (item) )
#define peertreeFind(tree, item) RB_FIND(  tr_peertree_s, (tree), (item) )

static inline int
peertreekeycmp( tr_peertree_entry_t * aa, tr_peertree_entry_t * bb )
{
    return memcmp( aa->peer, bb->peer, 6 );
}

RB_GENERATE_STATIC( tr_peertree_s, tr_peertree_entry_s, magic, peertreekeycmp );

static int
peertreeCount( tr_peertree_t * tree )
{
    tr_peertree_entry_t * ii;
    int                   count;

    count = 0;
    RB_FOREACH( ii, tr_peertree_s, tree )
    {
        count++;
    }

    return count;
}

static tr_peertree_entry_t *
peertreeGet( tr_peertree_t * tree, struct in_addr * addr, in_port_t port )
{
    tr_peertree_entry_t entry;

    memset( &entry, 0, sizeof( entry ) );
    memcpy( entry.peer, addr, 4 );
    memcpy( entry.peer + 4, &port, 2 );

    return RB_FIND( tr_peertree_s, tree, &entry );
}

static tr_peertree_entry_t *
peertreeAdd( tr_peertree_t * tree, struct in_addr * addr, in_port_t port )
{
    tr_peertree_entry_t * entry;

    entry = calloc( 1, sizeof( *entry) );
    if( NULL == entry )
    {
        return NULL;
    }
    memcpy( entry->peer, addr, 4 );
    memcpy( entry->peer + 4, &port, 2 );
    if( NULL == RB_INSERT( tr_peertree_s, tree, entry ) )
    {
        return entry;
    }
    free( entry );
    return NULL;
}

static void
peertreeMove( tr_peertree_t * dest, tr_peertree_t * src,
              tr_peertree_entry_t * entry )
{
    tr_peertree_entry_t * sanity;

    sanity = RB_REMOVE( tr_peertree_s, src, entry );
    assert( sanity == entry );
    sanity = RB_INSERT( tr_peertree_s, dest, entry );
    assert( NULL == sanity );
}

static void
peertreeMerge( tr_peertree_t * dest, tr_peertree_t * src )
{
    tr_peertree_entry_t * ii, * next, * sanity;

    for( ii = peertreeFirst( src ); NULL != ii; ii = next )
    {
        next = peertreeNext( src, ii );
        sanity = RB_REMOVE( tr_peertree_s, src, ii );
        assert( sanity == ii );
        sanity = RB_INSERT( tr_peertree_s, dest, ii );
        assert( NULL == sanity );
    }
}

static void
peertreeSwap( tr_peertree_t * dest, tr_peertree_t * src )
{
    tr_peertree_t buf;

    RB_ROOT( &buf ) = RB_ROOT( dest );
    RB_ROOT( dest ) = RB_ROOT( src  );
    RB_ROOT( src  ) = RB_ROOT( &buf );
}

static void
peertreeFree( tr_peertree_t * tree )
{
    tr_peertree_entry_t * ii, * next;
    for( ii = RB_MIN( tr_peertree_s, tree ); NULL != ii; ii = next )
    {
        next = RB_NEXT( tr_peertree_s, tree, ii );
        RB_REMOVE( tr_peertree_s, tree, ii );
        free( ii );
    }
}
