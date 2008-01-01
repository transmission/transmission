/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
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
typedef struct tr_ptrArray tr_ptrArray;

typedef void (*PtrArrayForeachFunc)(void *);

tr_ptrArray * tr_ptrArrayNew     ( void );
tr_ptrArray * tr_ptrArrayDup     ( tr_ptrArray* );
void    tr_ptrArrayForeach       ( tr_ptrArray*, PtrArrayForeachFunc func );
void    tr_ptrArrayFree          ( tr_ptrArray*, PtrArrayForeachFunc func );
void*   tr_ptrArrayNth           ( tr_ptrArray*, int n );
void**  tr_ptrArrayPeek          ( tr_ptrArray*, int * size );
void**  tr_ptrArrayBase          ( tr_ptrArray* );
void    tr_ptrArrayClear         ( tr_ptrArray* );
int     tr_ptrArrayInsert        ( tr_ptrArray*, void*, int pos );
int     tr_ptrArrayAppend        ( tr_ptrArray*, void* );
void    tr_ptrArrayErase         ( tr_ptrArray*, int begin, int end );
int     tr_ptrArraySize          ( const tr_ptrArray* );
int     tr_ptrArrayEmpty         ( const tr_ptrArray* );
int     tr_ptrArrayInsertSorted  ( tr_ptrArray*, void*,
                                   int compare(const void*,const void*) );
void*   tr_ptrArrayRemoveSorted  ( tr_ptrArray*, void*,
                                   int compare(const void*,const void*) );
void*   tr_ptrArrayFindSorted    ( tr_ptrArray*, const void*,
                                   int compare(const void*,const void*) );

#endif
