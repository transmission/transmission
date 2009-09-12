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

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef _TR_PTR_ARRAY_H_
#define _TR_PTR_ARRAY_H_

#include "transmission.h"

/**
 * @addtogroup utils Utilities
 * @{
 */

/**
 * A simple pointer array that resizes itself dynamically.
 */
typedef struct tr_ptrArray
{
    void ** items;
    int     n_items;
    int     n_alloc;
}
tr_ptrArray;

typedef void ( *PtrArrayForeachFunc )( void * );

extern const tr_ptrArray TR_PTR_ARRAY_INIT;

void          tr_ptrArrayDestruct( tr_ptrArray*, PtrArrayForeachFunc func );

void          tr_ptrArrayForeach( tr_ptrArray         * array,
                                  PtrArrayForeachFunc   func );

void*         tr_ptrArrayNth( tr_ptrArray   * array,
                              int             nth );

void*         tr_ptrArrayBack( tr_ptrArray  * array );

void**        tr_ptrArrayPeek( tr_ptrArray  * array,
                               int          * size );

static TR_INLINE void  tr_ptrArrayClear( tr_ptrArray * a ) { a->n_items = 0; }

int           tr_ptrArrayInsert( tr_ptrArray * array,
                                 void        * insertMe,
                                 int           pos );

static TR_INLINE int tr_ptrArrayAppend( tr_ptrArray * array, void * appendMe )
{
    return tr_ptrArrayInsert( array, appendMe, -1 );
}

void*         tr_ptrArrayPop( tr_ptrArray    * array );

static TR_INLINE void** tr_ptrArrayBase( const tr_ptrArray * a )
{
    return a->items;
}

static TR_INLINE int tr_ptrArraySize( const tr_ptrArray *  a )
{
    return a->n_items;
}

static TR_INLINE tr_bool tr_ptrArrayEmpty( const tr_ptrArray * a )
{
    return tr_ptrArraySize(a) == 0;
}

int           tr_ptrArrayInsertSorted( tr_ptrArray * array,
                                       void        * value,
                                       int compare(const void*, const void*) );

void*         tr_ptrArrayRemoveSorted( tr_ptrArray * array,
                                       void        * value,
                                       int compare(const void*, const void*) );

void*         tr_ptrArrayFindSorted( tr_ptrArray * array,
                                     const void  * key,
                                     int compare(const void*, const void*) );

/* @} */
#endif
