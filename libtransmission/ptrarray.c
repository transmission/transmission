/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* memmove */

#include "ptrarray.h"
#include "utils.h"

#define FLOOR 32

const tr_ptrArray TR_PTR_ARRAY_INIT = TR_PTR_ARRAY_INIT_STATIC;

void
tr_ptrArrayDestruct (tr_ptrArray * p, PtrArrayForeachFunc func)
{
  assert (p != NULL);
  assert (p->items || !p->n_items);

  if (func)
    tr_ptrArrayForeach (p, func);

  tr_free (p->items);
}

void
tr_ptrArrayForeach (tr_ptrArray * t, PtrArrayForeachFunc func)
{
  int i;

  assert (t);
  assert (t->items || !t->n_items);
  assert (func);

  for (i=0; i<t->n_items; ++i)
    func (t->items[i]);
}

void**
tr_ptrArrayPeek (tr_ptrArray * t, int * size)
{
  *size = t->n_items;
  return t->items;
}

int
tr_ptrArrayInsert (tr_ptrArray * t, void * ptr, int pos)
{
  if (t->n_items >= t->n_alloc)
    {
      t->n_alloc = MAX (FLOOR, t->n_alloc * 2);
      t->items = tr_renew (void*, t->items, t->n_alloc);
    }

  if (pos < 0 || pos > t->n_items)
    pos = t->n_items;
  else
    memmove (t->items+pos+1, t->items+pos, sizeof(void*)*(t->n_items-pos));

  t->items[pos] = ptr;
  t->n_items++;
  return pos;
}

void*
tr_ptrArrayPop (tr_ptrArray* t)
{
  void * ret = NULL;

  if (t->n_items)
    ret = t->items[--t->n_items];

  return ret;
}

void
tr_ptrArrayErase (tr_ptrArray * t, int begin, int end)
{
  if (end < 0)
    end = t->n_items;

  assert (begin >= 0);
  assert (begin < end);
  assert (end <= t->n_items);

  memmove (t->items+begin, t->items+end, sizeof(void*)*(t->n_items-end));

  t->n_items -= (end - begin);
}

/**
***
**/

int
tr_ptrArrayLowerBound (const tr_ptrArray  * t,
                       const void         * ptr,
                       int                  compare (const void *, const void *),
                       bool               * exact_match)
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
          const int half = (last - first) / 2;
          const int c = compare (t->items[first + half], ptr);

          if (c < 0)
            {
              const int new_first = first + half + 1;
              if (new_first > last)
                {
                  pos = new_first;
                  break;
                }
              first = new_first;
            }
          else if (c > 0)
            {
              const int new_last = first + half - 1;
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
    *exact_match = match;

  return pos;
}

#ifdef NDEBUG
#define assertArrayIsSortedAndUnique(array,compare) /* no-op */
#define assertIndexIsSortedAndUnique(array,pos,compare) /* no-op */
#else

static void
assertArrayIsSortedAndUnique (const tr_ptrArray * t,
                              int compare (const void*, const void*))
{
  int i;

  for (i=0; i<t->n_items-2; ++i)
    assert (compare (t->items[i], t->items[i+1]) < 0);
}

static void
assertIndexIsSortedAndUnique (const tr_ptrArray * t,
                              int pos,
                              int compare (const void*, const void*))
{
  if (pos > 0)
    assert (compare (t->items[pos-1], t->items[pos]) < 0);

  if ((pos + 1) < t->n_items)
    assert (compare (t->items[pos], t->items[pos+1]) < 0);
}

#endif

int
tr_ptrArrayInsertSorted (tr_ptrArray * t,
                         void        * ptr,
                         int           compare (const void*, const void*))
{
  int pos;
  int ret;
  assertArrayIsSortedAndUnique (t, compare);

  pos = tr_ptrArrayLowerBound (t, ptr, compare, NULL);
  ret = tr_ptrArrayInsert (t, ptr, pos);

  assertIndexIsSortedAndUnique (t, ret, compare);
  return ret;
}

void*
tr_ptrArrayFindSorted (tr_ptrArray * t,
                       const void  * ptr,
                       int           compare (const void*, const void*))
{
  bool match = false;
  const int pos = tr_ptrArrayLowerBound (t, ptr, compare, &match);
  return match ? t->items[pos] : NULL;
}

static void*
tr_ptrArrayRemoveSortedValue (tr_ptrArray * t,
                              const void  * ptr,
                              int           compare (const void*, const void*))
{
  int pos;
  bool match;
  void * ret = NULL;

  assertArrayIsSortedAndUnique (t, compare);

  pos = tr_ptrArrayLowerBound (t, ptr, compare, &match);

  if (match)
    {
      ret = t->items[pos];
      assert (compare (ret, ptr) == 0);
      tr_ptrArrayErase (t, pos, pos + 1);
    }

  assert ((ret == NULL) || (compare (ret, ptr) == 0));
  return ret;
}

void
tr_ptrArrayRemoveSortedPointer (tr_ptrArray * t,
                                const void  * ptr,
                                int           compare (const void*, const void*))
{
#ifdef NDEBUG
  tr_ptrArrayRemoveSortedValue (t, ptr, compare);
#else
  void * removed = tr_ptrArrayRemoveSortedValue (t, ptr, compare);
  assert (removed != NULL);
  assert (removed == ptr);
  assert (tr_ptrArrayFindSorted (t, ptr, compare) == NULL);
#endif
}
