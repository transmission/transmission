/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* memset */

#include "transmission.h"
#include "bitfield.h"
#include "utils.h" /* tr_new0 () */

const tr_bitfield TR_BITFIELD_INIT = { NULL, 0, 0, 0, false, false };

/****
*****
****/

static const int8_t trueBitCount[256] =
{
  0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};

static size_t
countArray (const tr_bitfield * b)
{
  size_t ret = 0;
  size_t i = b->alloc_count;

  while (i > 0)
    ret += trueBitCount[b->bits[--i]];

  return ret;
}

static size_t
countRange (const tr_bitfield * b, size_t begin, size_t end)
{
  size_t ret = 0;
  const size_t first_byte = begin >> 3u;
  const size_t last_byte = (end - 1) >> 3u;

  if (!b->bit_count)
    return 0;

  if (first_byte >= b->alloc_count)
    return 0;

  assert (begin < end);
  assert (b->bits != NULL);

  if (first_byte == last_byte)
    {
      int i;
      uint8_t val = b->bits[first_byte];

      i = begin - (first_byte * 8);
      val <<= i;
      val >>= i;
      i = (last_byte+1)*8 - end;
      val >>= i;
      val <<= i;

      ret += trueBitCount[val];
    }
  else
    {
      size_t i;
      uint8_t val;
      const size_t walk_end = MIN (b->alloc_count, last_byte);

      /* first byte */
      i = begin - (first_byte * 8);
      val = b->bits[first_byte];
      val <<= i;
      val >>= i;
      ret += trueBitCount[val];

      /* middle bytes */
      for (i=first_byte+1; i<walk_end; ++i)
        ret += trueBitCount[b->bits[i]];

      /* last byte */
      if (last_byte < b->alloc_count)
        {
          i = (last_byte+1)*8 - end;
          val = b->bits[last_byte];
          val >>= i;
          val <<= i;
          ret += trueBitCount[val];
        }
    }

  assert (ret <= (begin - end));
  return ret;
}

size_t
tr_bitfieldCountRange (const tr_bitfield * b, size_t begin, size_t end)
{
  if (tr_bitfieldHasAll (b))
    return end - begin;

  if (tr_bitfieldHasNone (b))
    return 0;

  return countRange (b, begin, end);
}

bool
tr_bitfieldHas (const tr_bitfield * b, size_t n)
{
  if (tr_bitfieldHasAll (b))
    return true;

  if (tr_bitfieldHasNone (b))
    return false;

  if (n>>3u >= b->alloc_count)
    return false;

  return (b->bits[n>>3u] << (n & 7u) & 0x80) != 0;
}

/***
****
***/

#ifndef NDEBUG

static bool
tr_bitfieldIsValid (const tr_bitfield * b)
{
  assert (b != NULL);
  assert ((b->alloc_count == 0) == (b->bits == 0));
  assert (!b->bits || (b->true_count == countArray (b)));

  return true;
}

#endif

size_t
tr_bitfieldCountTrueBits (const tr_bitfield * b)
{
  assert (tr_bitfieldIsValid (b));

  return b->true_count;
}

static size_t
get_bytes_needed (size_t bit_count)
{
  return (bit_count >> 3) + (bit_count & 7 ? 1 : 0);
}

static void
set_all_true (uint8_t * array, size_t bit_count)
{
  const uint8_t val = 0xFF;
  const size_t n = get_bytes_needed (bit_count);

  if (n > 0)
    {
      memset (array, val, n-1);

      array[n-1] = val << (n*8 - bit_count);
    }
}

void*
tr_bitfieldGetRaw (const tr_bitfield * b, size_t * byte_count)
{
  const size_t n = get_bytes_needed (b->bit_count);
  uint8_t * bits = tr_new0 (uint8_t, n);

  assert (b->bit_count > 0);

  if (b->alloc_count)
    {
      assert (b->alloc_count <= n);
      memcpy (bits, b->bits, b->alloc_count);
    }
  else if (tr_bitfieldHasAll (b))
    {
      set_all_true (bits, b->bit_count);
    }

  *byte_count = n;
  return bits;
}

static void
tr_bitfieldEnsureBitsAlloced (tr_bitfield * b, size_t n)
{
  size_t bytes_needed;
  const bool has_all = tr_bitfieldHasAll (b);

  if (has_all)
    bytes_needed = get_bytes_needed (MAX (n, b->true_count));
  else
    bytes_needed = get_bytes_needed (n);

  if (b->alloc_count < bytes_needed)
    {
      b->bits = tr_renew (uint8_t, b->bits, bytes_needed);
      memset (b->bits + b->alloc_count, 0, bytes_needed - b->alloc_count);
      b->alloc_count = bytes_needed;

      if (has_all)
        set_all_true (b->bits, b->true_count);
    }
}

static bool
tr_bitfieldEnsureNthBitAlloced (tr_bitfield * b, size_t nth)
{
  /* count is zero-based, so we need to allocate nth+1 bits before setting the nth */

  if (nth == SIZE_MAX)
    return false;

  tr_bitfieldEnsureBitsAlloced (b, nth + 1);
  return true;
}

static void
tr_bitfieldFreeArray (tr_bitfield * b)
{
  tr_free (b->bits);
  b->bits = NULL;
  b->alloc_count = 0;
}

static void
tr_bitfieldSetTrueCount (tr_bitfield * b, size_t n)
{
  assert (b->bit_count == 0 || n <= b->bit_count);

  b->true_count = n;

  if (tr_bitfieldHasAll (b) || tr_bitfieldHasNone (b))
    tr_bitfieldFreeArray (b);

  assert (tr_bitfieldIsValid (b));
}

static void
tr_bitfieldRebuildTrueCount (tr_bitfield * b)
{
  tr_bitfieldSetTrueCount (b, countArray (b));
}

static void
tr_bitfieldIncTrueCount (tr_bitfield * b, size_t i)
{
  assert (b->bit_count == 0 || i <= b->bit_count);
  assert (b->bit_count == 0 || b->true_count <= b->bit_count - i);

  tr_bitfieldSetTrueCount (b, b->true_count + i);
}

static void
tr_bitfieldDecTrueCount (tr_bitfield * b, size_t i)
{
  assert (b->bit_count == 0 || i <= b->bit_count);
  assert (b->bit_count == 0 || b->true_count >= i);

  tr_bitfieldSetTrueCount (b, b->true_count - i);
}

/****
*****
****/

void
tr_bitfieldConstruct (tr_bitfield * b, size_t bit_count)
{
  b->bit_count = bit_count;
  b->true_count = 0;
  b->bits = NULL;
  b->alloc_count = 0;
  b->have_all_hint = false;
  b->have_none_hint = false;

  assert (tr_bitfieldIsValid (b));
}

void
tr_bitfieldSetHasNone (tr_bitfield * b)
{
  tr_bitfieldFreeArray (b);
  b->true_count = 0;
  b->have_all_hint = false;
  b->have_none_hint = true;

  assert (tr_bitfieldIsValid (b));
}

void
tr_bitfieldSetHasAll (tr_bitfield * b)
{
  tr_bitfieldFreeArray (b);
  b->true_count = b->bit_count;
  b->have_all_hint = true;
  b->have_none_hint = false;

  assert (tr_bitfieldIsValid (b));
}

void
tr_bitfieldSetFromBitfield (tr_bitfield * b, const tr_bitfield * src)
{
  if (tr_bitfieldHasAll (src))
    tr_bitfieldSetHasAll (b);
  else if (tr_bitfieldHasNone (src))
    tr_bitfieldSetHasNone (b);
  else
    tr_bitfieldSetRaw (b, src->bits, src->alloc_count, true);
}

void
tr_bitfieldSetRaw (tr_bitfield * b, const void * bits, size_t byte_count, bool bounded)
{
  tr_bitfieldFreeArray (b);
  b->true_count = 0;

  if (bounded)
    byte_count = MIN (byte_count, get_bytes_needed (b->bit_count));

  b->bits = tr_memdup (bits, byte_count);
  b->alloc_count = byte_count;

  if (bounded)
    {
      /* ensure the excess bits are set to '0' */
      const int excess_bit_count = byte_count*8 - b->bit_count;
      assert (excess_bit_count >= 0);
      assert (excess_bit_count <= 7);
      if (excess_bit_count)
        b->bits[b->alloc_count-1] &= ((0xff) << excess_bit_count);
    }

  tr_bitfieldRebuildTrueCount (b);
}

void
tr_bitfieldSetFromFlags (tr_bitfield * b, const bool * flags, size_t n)
{
  size_t i;
  size_t trueCount = 0;

  tr_bitfieldFreeArray (b);
  tr_bitfieldEnsureBitsAlloced (b, n);

  for (i=0; i<n; ++i)
    {
      if (flags[i])
        {
          ++trueCount;
          b->bits[i >> 3u] |= (0x80 >> (i & 7u));
        }
    }

  tr_bitfieldSetTrueCount (b, trueCount);
}

void
tr_bitfieldAdd (tr_bitfield * b, size_t nth)
{
  if (!tr_bitfieldHas (b, nth) && tr_bitfieldEnsureNthBitAlloced (b, nth))
    {
      b->bits[nth >> 3u] |= (0x80 >> (nth & 7u));
      tr_bitfieldIncTrueCount (b, 1);
    }
}

/* Sets bit range [begin, end) to 1 */
void
tr_bitfieldAddRange (tr_bitfield * b, size_t begin, size_t end)
{
  size_t sb, eb;
  unsigned char sm, em;
  const size_t diff = (end-begin) - tr_bitfieldCountRange (b, begin, end);

  if (diff == 0)
    return;

  end--;
  if ((end >= b->bit_count) || (begin > end))
    return;

  sb = begin >> 3;
  sm = ~ (0xff << (8 - (begin & 7)));
  eb = end >> 3;
  em = 0xff << (7 - (end & 7));

  if (!tr_bitfieldEnsureNthBitAlloced (b, end))
    return;

  if (sb == eb)
    {
      b->bits[sb] |= (sm & em);
    }
  else
    {
      b->bits[sb] |= sm;
      b->bits[eb] |= em;
      if (++sb < eb)
        memset (b->bits + sb, 0xff, eb - sb);
    }

  tr_bitfieldIncTrueCount (b, diff);
}

void
tr_bitfieldRem (tr_bitfield * b, size_t nth)
{
  assert (tr_bitfieldIsValid (b));

  if (tr_bitfieldHas (b, nth) && tr_bitfieldEnsureNthBitAlloced (b, nth))
    {
      b->bits[nth >> 3u] &= (0xff7f >> (nth & 7u));
      tr_bitfieldDecTrueCount (b, 1);
    }
}

/* Clears bit range [begin, end) to 0 */
void
tr_bitfieldRemRange (tr_bitfield * b, size_t begin, size_t end)
{
  size_t sb, eb;
  unsigned char sm, em;
  const size_t diff = tr_bitfieldCountRange (b, begin, end);

  if (!diff)
    return;

  end--;

  if ((end >= b->bit_count) || (begin > end))
    return;

  sb = begin >> 3;
  sm = 0xff << (8 - (begin & 7));
  eb = end >> 3;
  em = ~ (0xff << (7 - (end & 7)));

  if (!tr_bitfieldEnsureNthBitAlloced (b, end))
    return;

  if (sb == eb)
    {
      b->bits[sb] &= (sm | em);
    }
  else
    {
      b->bits[sb] &= sm;
      b->bits[eb] &= em;
      if (++sb < eb)
        memset (b->bits + sb, 0, eb - sb);
    }

  tr_bitfieldDecTrueCount (b, diff);
}
