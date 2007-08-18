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

#ifndef _TR_POINTERS_H_
#define _TR_POINTERS_H_

/**
 * A simple pointer array that resizes itself dynamically.
 */
typedef struct tr_ptrArray_s tr_ptrArray_t;

tr_ptrArray_t * tr_ptrArrayNew         ( void );
void            tr_ptrArrayFree        ( tr_ptrArray_t* );
void**          tr_ptrArrayPeek        ( tr_ptrArray_t*, int * size );
void**          tr_ptrArrayBase        ( tr_ptrArray_t* );
void            tr_ptrArrayClear       ( tr_ptrArray_t* );
int             tr_ptrArrayInsert      ( tr_ptrArray_t*, void*, int pos );
int             tr_ptrArrayAppend      ( tr_ptrArray_t*, void* );
void            tr_ptrArrayErase       ( tr_ptrArray_t*, int begin, int end );
int             tr_ptrArraySize        ( const tr_ptrArray_t* );
int             tr_ptrArrayEmpty       ( const tr_ptrArray_t* );

int             tr_ptrArrayInsertSorted( tr_ptrArray_t*, void*,
                                         int compare(const void*,const void*) );
void*           tr_ptrArrayRemoveSorted( tr_ptrArray_t*, void*,
                                         int compare(const void*,const void*) );
void*           tr_ptrArrayFindSorted  ( tr_ptrArray_t*, void*,
                                         int compare(const void*,const void*) );

#endif
