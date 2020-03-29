/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* memmove */

#include "ptrarray.h"
#include "tr-assert.h"
#include "utils.h"

#define FLOOR 32

tr_ptrArray const TR_PTR_ARRAY_INIT = TR_PTR_ARRAY_INIT_STATIC;

void tr_ptrArrayDestruct(tr_ptrArray* p, PtrArrayForeachFunc func)
{
    TR_ASSERT(p != NULL);
    TR_ASSERT(p->items != NULL || p->n_items == 0);

    if (func != NULL)
    {
        tr_ptrArrayForeach(p, func);
    }

    tr_free(p->items);
}

void tr_ptrArrayForeach(tr_ptrArray* t, PtrArrayForeachFunc func)
{
    TR_ASSERT(t != NULL);
    TR_ASSERT(t->items != NULL || t->n_items == 0);
    TR_ASSERT(func != NULL);

    for (int i = 0; i < t->n_items; ++i)
    {
        func(t->items[i]);
    }
}

void** tr_ptrArrayPeek(tr_ptrArray* t, int* size)
{
    *size = t->n_items;
    return t->items;
}

int tr_ptrArrayInsert(tr_ptrArray* t, void* ptr, int pos)
{
    if (t->n_items >= t->n_alloc)
    {
        t->n_alloc = MAX(FLOOR, t->n_alloc * 2);
        t->items = tr_renew(void*, t->items, t->n_alloc);
    }

    if (pos < 0 || pos > t->n_items)
    {
        pos = t->n_items;
    }
    else
    {
        memmove(t->items + pos + 1, t->items + pos, sizeof(void*) * (t->n_items - pos));
    }

    t->items[pos] = ptr;
    t->n_items++;
    return pos;
}

void* tr_ptrArrayPop(tr_ptrArray* t)
{
    void* ret = NULL;

    if (t->n_items != 0)
    {
        ret = t->items[--t->n_items];
    }

    return ret;
}

void tr_ptrArrayErase(tr_ptrArray* t, int begin, int end)
{
    if (end < 0)
    {
        end = t->n_items;
    }

    TR_ASSERT(begin >= 0);
    TR_ASSERT(begin < end);
    TR_ASSERT(end <= t->n_items);

    memmove(t->items + begin, t->items + end, sizeof(void*) * (t->n_items - end));

    t->n_items -= end - begin;
}

/**
***
**/

int tr_ptrArrayLowerBound(tr_ptrArray const* t, void const* ptr, tr_voidptr_compare_func compare, bool* exact_match)
{
    int pos = -1;
    bool match = false;

    if (t->n_items == 0)
    {
        pos = 0;
    }
    else
    {
        int first = 0;
        int last = t->n_items - 1;

        for (;;)
        {
            int const half = (last - first) / 2;
            int const c = compare(t->items[first + half], ptr);

            if (c < 0)
            {
                int const new_first = first + half + 1;

                if (new_first > last)
                {
                    pos = new_first;
                    break;
                }

                first = new_first;
            }
            else if (c > 0)
            {
                int const new_last = first + half - 1;

                if (new_last < first)
                {
                    pos = first;
                    break;
                }

                last = new_last;
            }
            else
            {
                match = true;
                pos = first + half;
                break;
            }
        }
    }

    if (exact_match != NULL)
    {
        *exact_match = match;
    }

    return pos;
}

#ifndef TR_ENABLE_ASSERTS

#define assertArrayIsSortedAndUnique(array, compare) /* no-op */
#define assertIndexIsSortedAndUnique(array, pos, compare) /* no-op */

#else

static void assertArrayIsSortedAndUnique(tr_ptrArray const* t, tr_voidptr_compare_func compare)
{
    for (int i = 0; i < t->n_items - 2; ++i)
    {
        TR_ASSERT(compare(t->items[i], t->items[i + 1]) < 0);
    }
}

static void assertIndexIsSortedAndUnique(tr_ptrArray const* t, int pos, tr_voidptr_compare_func compare)
{
    if (pos > 0)
    {
        TR_ASSERT(compare(t->items[pos - 1], t->items[pos]) < 0);
    }

    if (pos + 1 < t->n_items)
    {
        TR_ASSERT(compare(t->items[pos], t->items[pos + 1]) < 0);
    }
}

#endif

int tr_ptrArrayInsertSorted(tr_ptrArray* t, void* ptr, tr_voidptr_compare_func compare)
{
    int pos;
    int ret;
    assertArrayIsSortedAndUnique(t, compare);

    pos = tr_ptrArrayLowerBound(t, ptr, compare, NULL);
    ret = tr_ptrArrayInsert(t, ptr, pos);

    assertIndexIsSortedAndUnique(t, ret, compare);
    return ret;
}

void* tr_ptrArrayFindSorted(tr_ptrArray* t, void const* ptr, tr_voidptr_compare_func compare)
{
    bool match = false;
    int const pos = tr_ptrArrayLowerBound(t, ptr, compare, &match);
    return match ? t->items[pos] : NULL;
}

static void* tr_ptrArrayRemoveSortedValue(tr_ptrArray* t, void const* ptr, tr_voidptr_compare_func compare)
{
    int pos;
    bool match;
    void* ret = NULL;

    assertArrayIsSortedAndUnique(t, compare);

    pos = tr_ptrArrayLowerBound(t, ptr, compare, &match);

    if (match)
    {
        ret = t->items[pos];
        TR_ASSERT(compare(ret, ptr) == 0);
        tr_ptrArrayErase(t, pos, pos + 1);
    }

    TR_ASSERT(ret == NULL || compare(ret, ptr) == 0);
    return ret;
}

void tr_ptrArrayRemoveSortedPointer(tr_ptrArray* t, void const* ptr, tr_voidptr_compare_func compare)
{
    void* removed = tr_ptrArrayRemoveSortedValue(t, ptr, compare);

#ifndef TR_ENABLE_ASSERTS

    (void)removed;

#else

    TR_ASSERT(removed != NULL);
    TR_ASSERT(removed == ptr);
    TR_ASSERT(tr_ptrArrayFindSorted(t, ptr, compare) == NULL);

#endif
}
