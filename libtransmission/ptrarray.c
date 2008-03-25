/******************************************************************************
 * $Id$
 *
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
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

#include <assert.h>
#include <stdlib.h>
#include <string.h> /* memmove */

#include "ptrarray.h"
#include "utils.h"

#define GROW 32

struct tr_ptrArray
{
    void ** items;
    int n_items;
    int n_alloc;
};

tr_ptrArray*
tr_ptrArrayNew( void )
{
    tr_ptrArray * p;

    p = tr_new( tr_ptrArray, 1 );
    p->n_items = 0;
    p->n_alloc = GROW;
    p->items = tr_new( void*, p->n_alloc );

    return p;
}

tr_ptrArray*
tr_ptrArrayDup( tr_ptrArray* in )
{
    tr_ptrArray * out;

    out = tr_new( tr_ptrArray, 1 );
    out->n_items = in->n_items;
    out->n_alloc = in->n_items;
    out->items = tr_new( void*, out->n_alloc );
    memcpy( out->items, in->items, out->n_items * sizeof(void*) );

    return out;
}

void
tr_ptrArrayForeach( tr_ptrArray * t, PtrArrayForeachFunc func )
{
    int i;

    assert( t != NULL );
    assert( t->items != NULL );
    assert( func != NULL );

    for( i=0; i<t->n_items; ++i )
        func( t->items[i] );
}

void
tr_ptrArrayFree( tr_ptrArray * t, PtrArrayForeachFunc func )
{
    assert( t != NULL );
    assert( t->items != NULL );

    if( func != NULL )
        tr_ptrArrayForeach( t, func );

    tr_free( t->items );
    tr_free( t );
}

void**
tr_ptrArrayPeek( tr_ptrArray * t, int * size )
{
    *size = t->n_items;
    return t->items;
}

void*
tr_ptrArrayNth( tr_ptrArray* t, int i )
{
    assert( t != NULL  );
    assert( i >= 0 );
    assert( i < t->n_items );

    return t->items[i];
}

void*
tr_ptrArrayBack( tr_ptrArray* t )
{
    assert( t->n_items > 0 );

    return tr_ptrArrayNth( t, t->n_items-1 );
}

int
tr_ptrArraySize( const tr_ptrArray * t )
{
    return t->n_items;
}

int
tr_ptrArrayEmpty( const tr_ptrArray * t )
{
    return t->n_items == 0;
}

void
tr_ptrArrayClear( tr_ptrArray * t )
{
    t->n_items = 0;
}

int
tr_ptrArrayInsert( tr_ptrArray * t, void * ptr, int pos )
{
    if( pos<0 || pos>t->n_items )
        pos = t->n_items;

    if( t->n_items >= t->n_alloc ) {
        t->n_alloc = t->n_items + GROW;
        t->items = tr_renew( void*, t->items, t->n_alloc );
    }

    memmove( t->items + pos + 1,
             t->items + pos,
             sizeof(void*) * (t->n_items - pos));

    t->items[pos] = ptr;
    t->n_items++;
    return pos;
}

int
tr_ptrArrayAppend( tr_ptrArray * t, void * ptr )
{
    return tr_ptrArrayInsert( t, ptr, -1 );
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
tr_ptrArrayErase( tr_ptrArray * t, int begin, int end )
{
    assert( begin >= 0 );
    if( end < 0 ) end = t->n_items;
    assert( end > begin );
    assert( end <= t->n_items );

    memmove( t->items + begin,
             t->items + end,
             sizeof(void*) * (t->n_items - end) );

    t->n_items -= (end - begin);
}

/**
***
**/

int
tr_ptrArrayLowerBound( const tr_ptrArray * t,
                       const void        * ptr,
                       int                 compare( const void *,const void * ),
                       int               * exact_match )
{
    int len = t->n_items;
    int first = 0;

    while( len > 0 )
    {
        int half = len / 2;
        int middle = first + half;
        const int c = compare( t->items[middle], ptr );
        if( c < 0 ) {
            first = middle + 1;
            len = len - half - 1;
        } else if (!c ) {
            if( exact_match )
                *exact_match = 1;
            return middle;
            break;
        } else {
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
    for( i=0; i<t->n_items-2; ++i )
        assert( compare( t->items[i], t->items[i+1] ) < 0 );
}

int
tr_ptrArrayInsertSorted( tr_ptrArray  * t,
                         void         * ptr,
                         int            compare(const void*,const void*) )
{
    const int pos = tr_ptrArrayLowerBound( t, ptr, compare, NULL );
    const int ret = tr_ptrArrayInsert( t, ptr, pos );
    assertSortedAndUnique( t, compare );
    return ret;
}

void*
tr_ptrArrayFindSorted( tr_ptrArray  * t,
                       const void   * ptr,
                       int            compare(const void*,const void*) )
{
    int match;
    const int pos = tr_ptrArrayLowerBound( t, ptr, compare, &match );
    return match ? t->items[pos] : NULL;
}

void*
tr_ptrArrayRemoveSorted( tr_ptrArray  * t,
                         void         * ptr,
                         int            compare(const void*,const void*) )
{
    void * ret = NULL;
    int match;
    const int pos = tr_ptrArrayLowerBound( t, ptr, compare, &match );
    if( match ) {
        ret = t->items[pos];
        tr_ptrArrayErase( t, pos, pos+1 );
    }
    assertSortedAndUnique( t, compare );
    return ret;
}
