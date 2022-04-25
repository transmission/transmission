// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "transmission.h"

#include "tr-macros.h"

/**
 * @addtogroup utils Utilities
 * @{
 */

/**
 * @brief simple pointer array that resizes itself dynamically.
 */
struct tr_ptrArray
{
    void** items;
    int n_items;
    int n_alloc;
};

using tr_voidptr_compare_func = int (*)(void const* lhs, void const* rhs);

using PtrArrayCompareFunc = tr_voidptr_compare_func;

using PtrArrayForeachFunc = void (*)(void*);

/** @brief Destructor to free a tr_ptrArray's internal memory */
void tr_ptrArrayDestruct(tr_ptrArray*, PtrArrayForeachFunc func);

/** @brief Iterate through each item in a tr_ptrArray */
void tr_ptrArrayForeach(tr_ptrArray* array, PtrArrayForeachFunc func);

/** @brief Return the nth item in a tr_ptrArray
    @return the nth item in a tr_ptrArray */
void* tr_ptrArrayNth(tr_ptrArray* array, int i);

void tr_ptrArrayErase(tr_ptrArray* t, int begin, int end);

static inline void tr_ptrArrayRemove(tr_ptrArray* t, int pos)
{
    tr_ptrArrayErase(t, pos, pos + 1);
}

/** @brief Peek at the array pointer and its size, for easy iteration */
void** tr_ptrArrayPeek(tr_ptrArray* array, int* size);

/** @brief Insert a pointer into the array at the specified position
    @return the index of the stored pointer */
int tr_ptrArrayInsert(tr_ptrArray* array, void* insertMe, int pos);

/** @brief Append a pointer into the array */
static inline int tr_ptrArrayAppend(tr_ptrArray* array, void* appendMe)
{
    return tr_ptrArrayInsert(array, appendMe, -1);
}

constexpr void** tr_ptrArrayBase(tr_ptrArray const* a)
{
    return a->items;
}

/** @brief Return the number of items in the array
    @return the number of items in the array */
constexpr int tr_ptrArraySize(tr_ptrArray const* a)
{
    return a->n_items;
}

/** @brief Return True if the array has no pointers
    @return True if the array has no pointers */
constexpr bool tr_ptrArrayEmpty(tr_ptrArray const* a)
{
    return tr_ptrArraySize(a) == 0;
}

int tr_ptrArrayLowerBound(tr_ptrArray const* array, void const* key, tr_voidptr_compare_func compare, bool* exact_match);

/** @brief Insert a pointer into the array at the position determined by the sort function
    @return the index of the stored pointer */
int tr_ptrArrayInsertSorted(tr_ptrArray* array, void* value, tr_voidptr_compare_func compare);

/** @brief Remove this specific pointer from a sorted ptrarray */
void tr_ptrArrayRemoveSortedPointer(tr_ptrArray* t, void const* ptr, tr_voidptr_compare_func compare);

/** @brief Find a pointer from an array sorted by the specified sort function
    @return the matching pointer, or nullptr if no match was found */
void* tr_ptrArrayFindSorted(tr_ptrArray* array, void const* key, tr_voidptr_compare_func compare);

/* @} */
