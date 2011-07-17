/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <stdlib.h> /* tr_renew() -> realloc() */
#include <string.h> /* memmove */

#include "ptrarray.h"
#include "utils.h"

#define FLOOR 32

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

int
tr_ptrArrayInsert( tr_ptrArray * t,
                   void        * ptr,
                   int           pos )
{
    if( t->n_items >= t->n_alloc )
    {
        t->n_alloc = MAX( FLOOR, t->n_alloc * 2 );
        t->items = tr_renew( void*, t->items, t->n_alloc );
    }

    if( pos < 0 || pos > t->n_items )
        pos = t->n_items;
    else
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
    assert( begin < end );
    assert( end <= t->n_items );

    memmove( t->items + begin,
             t->items + end,
             sizeof( void* ) * ( t->n_items - end ) );

    t->n_items -= ( end - begin );
}

/**
***
**/

#ifndef NDEBUG
static int
lowerBound2( const tr_ptrArray * t,
             const void * ptr,
             int compare( const void *, const void * ),
             bool * exact_match )
{
    int i;
    const int len = t->n_items;

    for( i=0; i<len; ++i )
    {
        const int c = compare( t->items[i], ptr );

        if( c == 0 ) {
            *exact_match = true;
            return i;
        }

        if( c < 0 )
            continue;

        *exact_match = false;
        return i;
    }

    *exact_match = false;
    return len;
}
#endif

int
tr_ptrArrayLowerBound( const tr_ptrArray  * t,
                       const void         * ptr,
                       int                  compare( const void *, const void * ),
                       bool               * exact_match )
{
    int pos = -1;
    bool match = false;
#ifndef NDEBUG
    bool match_2;
#endif

    if( t->n_items == 0 )
    {
        pos = 0;
    }
    else
    {
        int first = 0;
        int last = t->n_items - 1;

        for( ;; )
        {
            const int half = (last - first) / 2;
            const int c = compare( t->items[first + half], ptr );

            if( c < 0 ) {
                const int new_first = first + half + 1;
                if( new_first > last ) {
                    pos = new_first;
                    break;
                }
                first = new_first;
            }
            else if( c > 0 ) {
                const int new_last = first + half - 1;
                if( new_last < first ) {
                    pos = first;
                    break;
                }
                last = new_last;
            }
            else {
                match = true;
                pos = first + half;
                break;
            }
        }
    }

    assert( pos == lowerBound2( t, ptr, compare, &match_2 ) );
    assert( match == match_2 );

    if( exact_match )
        *exact_match = match;
    return pos;
}

static void
assertSortedAndUnique( const tr_ptrArray * t UNUSED,
                       int compare(const void*, const void*) UNUSED )
{
#if 1
    int i;

    for( i = 0; i < t->n_items - 2; ++i )
        assert( compare( t->items[i], t->items[i + 1] ) < 0 );
#endif
}

int
tr_ptrArrayInsertSorted( tr_ptrArray * t,
                         void *        ptr,
                         int           compare(const void*, const void*) )
{
    int pos;
    int ret;

    assertSortedAndUnique( t, compare );

    pos = tr_ptrArrayLowerBound( t, ptr, compare, NULL );
    ret = tr_ptrArrayInsert( t, ptr, pos );

    assertSortedAndUnique( t, compare );
    return ret;
}

void*
tr_ptrArrayFindSorted( tr_ptrArray * t,
                       const void *  ptr,
                       int           compare(const void*, const void*) )
{
    bool match = false;
    const int pos = tr_ptrArrayLowerBound( t, ptr, compare, &match );
    return match ? t->items[pos] : NULL;
}

void*
tr_ptrArrayRemoveSorted( tr_ptrArray * t,
                         const void  * ptr,
                         int           compare(const void*, const void*) )
{
    bool match;
    void * ret = NULL;
    const int pos = tr_ptrArrayLowerBound( t, ptr, compare, &match );
    assertSortedAndUnique( t, compare );

    if( match )
    {
        ret = t->items[pos];
        assert( compare( ret, ptr ) == 0 );
        tr_ptrArrayErase( t, pos, pos + 1 );
    }
    assertSortedAndUnique( t, compare );
    assert( ( ret == NULL ) || ( compare( ret, ptr ) == 0 ) );
    return ret;
}
