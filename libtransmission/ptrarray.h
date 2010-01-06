/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
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
 * @brief simple pointer array that resizes itself dynamically.
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

/** @brief Destructor to free a tr_ptrArray's internal memory */
void tr_ptrArrayDestruct( tr_ptrArray*, PtrArrayForeachFunc func );

/** @brief Iterate through each item in a tr_ptrArray */
void tr_ptrArrayForeach( tr_ptrArray         * array,
                         PtrArrayForeachFunc   func );

/** @brief Return the nth item in a tr_ptrArray
    @return the nth item in a tr_ptrArray */
void*         tr_ptrArrayNth( tr_ptrArray   * array,
                              int             nth );

/** @brief Remove the last item from the array and return it
    @return the pointer that's been removed from the array
    @see tr_ptrArrayBack() */
void* tr_ptrArrayPop( tr_ptrArray * array );

/** @brief Return the last item in a tr_ptrArray
    @return the last item in a tr_ptrArray, or NULL if the array is empty
    @see tr_ptrArrayPop() */
static inline void* tr_ptrArrayBack( tr_ptrArray * array )
{
    return array->n_items > 0 ? tr_ptrArrayNth( array, array->n_items - 1 )
                              : NULL;
}


/** @brief Peek at the array pointer and its size, for easy iteration */
void** tr_ptrArrayPeek( tr_ptrArray * array, int * size );

static inline void  tr_ptrArrayClear( tr_ptrArray * a ) { a->n_items = 0; }

/** @brief Insert a pointer into the array at the specified position
    @return the index of the stored pointer */
int tr_ptrArrayInsert( tr_ptrArray * array, void * insertMe, int pos );

/** @brief Append a pointer into the array */
static inline int tr_ptrArrayAppend( tr_ptrArray * array, void * appendMe )
{
    return tr_ptrArrayInsert( array, appendMe, -1 );
}

static inline void** tr_ptrArrayBase( const tr_ptrArray * a )
{
    return a->items;
}

/** @brief Return the number of items in the array
    @return the number of items in the array */
static inline int tr_ptrArraySize( const tr_ptrArray *  a )
{
    return a->n_items;
}

/** @brief Return True if the array has no pointers
    @return True if the array has no pointers */
static inline tr_bool tr_ptrArrayEmpty( const tr_ptrArray * a )
{
    return tr_ptrArraySize(a) == 0;
}

/** @brief Insert a pointer into the array at the position determined by the sort function
    @return the index of the stored pointer */
int tr_ptrArrayInsertSorted( tr_ptrArray * array,
                             void        * value,
                             int compare(const void*, const void*) );

/** @brief Remove a pointer from an array sorted by the specified sort function
    @return the matching pointer, or NULL if no match was found */
void* tr_ptrArrayRemoveSorted( tr_ptrArray * array,
                               void        * value,
                               int compare(const void*, const void*) );

/** @brief Find a pointer from an array sorted by the specified sort function
    @return the matching pointer, or NULL if no match was found */
void* tr_ptrArrayFindSorted( tr_ptrArray * array,
                             const void  * key,
                             int compare(const void*, const void*) );

/* @} */
#endif
