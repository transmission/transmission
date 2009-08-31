/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
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
#include <stdlib.h>
#include <string.h> /* memmove */

#include "ptrarray.h"
#include "utils.h"

#define GROW 32

const tr_ptrArray TR_PTR_ARRAY_INIT = { NULL, 0, 0 };

void
tr_ptrArrayDestruct( tr_ptrArray * p, PtrArrayForeachFunc func )
{
    assert( p );
    assert( p->items || !p->n_items );

    if( func )
        tr_ptrArrayForeach( p, func );

    tr_free( p->items );

    memset( p, ~0, sizeof( tr_ptrArray ) );
}

tr_ptrArray*
tr_ptrArrayDup( tr_ptrArray* in )
{
    tr_ptrArray * out;

    out = tr_new( tr_ptrArray, 1 );
    out->n_items = out->n_alloc = in->n_items;
    out->items = tr_memdup( in->items, out->n_items * sizeof( void* ) );

    return out;
}

void
tr_ptrArrayForeach( tr_ptrArray *       t,
                    PtrArrayForeachFunc func )
{
    int i;

    assert( t );
    assert( t->items || !t->n_items );
    assert( func );

    for( i = 0; i < t->n_items; ++i )
        func( t->items[i] );
}

void**
tr_ptrArrayPeek( tr_ptrArray * t,
                 int *         size )
{
    *size = t->n_items;
    return t->items;
}

void*
tr_ptrArrayNth( tr_ptrArray* t,
                int          i )
{
    assert( t );
    assert( i >= 0 );
    assert( i < t->n_items );

    return t->items[i];
}

void*
tr_ptrArrayBack( tr_ptrArray* t )
{
    assert( t->n_items > 0 );

    return tr_ptrArrayNth( t, t->n_items - 1 );
}

int
tr_ptrArrayInsert( tr_ptrArray * t,
                   void        * ptr,
                   int           pos )
{
    if( pos < 0 || pos > t->n_items )
        pos = t->n_items;

    if( t->n_items >= t->n_alloc )
    {
        t->n_alloc = t->n_items + GROW;
        t->items = tr_renew( void*, t->items, t->n_alloc );
    }

    memmove( t->items + pos + 1,
             t->items + pos,
             sizeof( void* ) * ( t->n_items - pos ) );

    t->items[pos] = ptr;
    t->n_items++;
    return pos;
}

void*
tr_ptrArrayPop( tr_ptrArray* t )
{
    void * ret = NULL;

    if( t->n_items )
        ret = t->items[--t->n_items];

    return ret;
}

void
tr_ptrArrayErase( tr_ptrArray * t,
                  int           begin,
                  int           end )
{
    assert( begin >= 0 );
    if( end < 0 ) end = t->n_items;
    assert( end - begin > 0 );
    assert( end <= t->n_items );

    memmove( t->items + begin,
            t->items + end,
            sizeof( void* ) * ( t->n_items - end ) );

    t->n_items -= ( end - begin );
}

/**
***
**/

static int
tr_ptrArrayLowerBound( const tr_ptrArray *                t,
                       const void *                       ptr,
                       int                 compare( const void *,
                                                    const void * ),
                       int *                              exact_match )
{
    int len = t->n_items;
    int first = 0;

    while( len > 0 )
    {
        int       half = len / 2;
        int       middle = first + half;
        const int c = compare( t->items[middle], ptr );
        if( c < 0 )
        {
            first = middle + 1;
            len = len - half - 1;
        }
        else if( !c )
        {
            if( exact_match )
                *exact_match = 1;
            return middle;
            break;
        }
        else
        {
            len = half;
        }
    }

    if( exact_match )
        *exact_match = 0;

    return first;
}

static void
assertSortedAndUnique( const tr_ptrArray * t,
                        int compare(const void*, const void*) )
{
    int i;

    for( i = 0; i < t->n_items - 2; ++i )
        assert( compare( t->items[i], t->items[i + 1] ) <= 0 );
}

int
tr_ptrArrayInsertSorted( tr_ptrArray * t,
                         void *        ptr,
                          int            compare(const void*, const void*) )
{
    const int pos = tr_ptrArrayLowerBound( t, ptr, compare, NULL );
    const int ret = tr_ptrArrayInsert( t, ptr, pos );

    assertSortedAndUnique( t, compare );
    return ret;
}

void*
tr_ptrArrayFindSorted( tr_ptrArray * t,
                       const void *  ptr,
                        int            compare(const void*, const void*) )
{
    int       match;
    const int pos = tr_ptrArrayLowerBound( t, ptr, compare, &match );

    return match ? t->items[pos] : NULL;
}

void*
tr_ptrArrayRemoveSorted( tr_ptrArray * t,
                         void *        ptr,
                          int            compare(const void*, const void*) )
{
    void *    ret = NULL;
    int       match;
    const int pos = tr_ptrArrayLowerBound( t, ptr, compare, &match );

    if( match )
    {
        ret = t->items[pos];
        tr_ptrArrayErase( t, pos, pos + 1 );
    }
    assertSortedAndUnique( t, compare );
    return ret;
}

