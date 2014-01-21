/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>

#include "transmission.h"
#include "completion.h"
#include "torrent.h"
#include "utils.h"

/***
****
***/

static void
tr_cpReset (tr_completion * cp)
{
  cp->sizeNow = 0;
  cp->sizeWhenDoneIsDirty = true;
  cp->haveValidIsDirty = true;
  tr_bitfieldSetHasNone (&cp->blockBitfield);
}

void
tr_cpConstruct (tr_completion * cp, tr_torrent * tor)
{
  cp->tor = tor;
  tr_bitfieldConstruct (&cp->blockBitfield, tor->blockCount);
  tr_cpReset (cp);
}

void
tr_cpBlockInit (tr_completion * cp, const tr_bitfield * b)
{
  tr_cpReset (cp);

  /* set blockBitfield */
  tr_bitfieldSetFromBitfield (&cp->blockBitfield, b);

  /* set sizeNow */
  cp->sizeNow = tr_bitfieldCountTrueBits (&cp->blockBitfield);
  assert (cp->sizeNow <= cp->tor->blockCount);
  cp->sizeNow *= cp->tor->blockSize;
  if (tr_bitfieldHas (b, cp->tor->blockCount-1))
    cp->sizeNow -= (cp->tor->blockSize - cp->tor->lastBlockSize);

  assert (cp->sizeNow <= cp->tor->info.totalSize);
}

/***
****
***/

tr_completeness
tr_cpGetStatus (const tr_completion * cp)
{
  if (tr_cpHasAll (cp))
    return TR_SEED;

  if (!tr_torrentHasMetadata (cp->tor))
    return TR_LEECH;

  if (cp->sizeNow == tr_cpSizeWhenDone (cp))
    return TR_PARTIAL_SEED;

  return TR_LEECH;
}

void
tr_cpPieceRem (tr_completion *  cp, tr_piece_index_t piece)
{
  tr_block_index_t i, f, l;
  const tr_torrent * tor = cp->tor;

  tr_torGetPieceBlockRange (cp->tor, piece, &f, &l);

  for (i=f; i<=l; ++i)
    if (tr_cpBlockIsComplete (cp, i))
      cp->sizeNow -= tr_torBlockCountBytes (tor, i);

  cp->haveValidIsDirty = true;
  cp->sizeWhenDoneIsDirty = true;
  tr_bitfieldRemRange (&cp->blockBitfield, f, l+1);
}

void
tr_cpPieceAdd (tr_completion * cp, tr_piece_index_t piece)
{
  tr_block_index_t i, f, l;
  tr_torGetPieceBlockRange (cp->tor, piece, &f, &l);

  for (i=f; i<=l; ++i)
    tr_cpBlockAdd (cp, i);
}

void
tr_cpBlockAdd (tr_completion * cp, tr_block_index_t block)
{
  const tr_torrent * tor = cp->tor;

  if (!tr_cpBlockIsComplete (cp, block))
    {
      const tr_piece_index_t piece = tr_torBlockPiece (cp->tor, block);

      tr_bitfieldAdd (&cp->blockBitfield, block);
      cp->sizeNow += tr_torBlockCountBytes (tor, block);

      cp->haveValidIsDirty = true;
      cp->sizeWhenDoneIsDirty |= tor->info.pieces[piece].dnd;
    }
}

/***
****
***/

uint64_t
tr_cpHaveValid (const tr_completion * ccp)
{
  if (ccp->haveValidIsDirty)
    {
      tr_piece_index_t i;
      uint64_t size = 0;
      tr_completion * cp = (tr_completion *) ccp; /* mutable */
      const tr_torrent * tor = ccp->tor;
      const tr_info * info = &tor->info;

      for (i=0; i<info->pieceCount; ++i)
        if (tr_cpPieceIsComplete (ccp, i))
          size += tr_torPieceCountBytes (tor, i);

      cp->haveValidLazy = size;
      cp->haveValidIsDirty = false;
    }

  return ccp->haveValidLazy;
}

uint64_t
tr_cpSizeWhenDone (const tr_completion * ccp)
{
  if (ccp->sizeWhenDoneIsDirty)
    {
      uint64_t size = 0;
      const tr_torrent * tor = ccp->tor;
      const tr_info * inf = tr_torrentInfo (tor);
      tr_completion * cp = (tr_completion *) ccp; /* mutable */

      if (tr_cpHasAll (ccp))
        {
          size = inf->totalSize;
        }
      else
        {
          tr_piece_index_t p;

          for (p=0; p<inf->pieceCount; ++p)
            {
              uint64_t n = 0;
              const uint64_t pieceSize = tr_torPieceCountBytes (tor, p);

              if (!inf->pieces[p].dnd)
                {
                  n = pieceSize;
                }
              else
                {
                  tr_block_index_t f, l;
                  tr_torGetPieceBlockRange (cp->tor, p, &f, &l);

                  n = tr_bitfieldCountRange (&cp->blockBitfield, f, l+1);
                  n *= cp->tor->blockSize;
                  if (l == (cp->tor->blockCount-1) && tr_bitfieldHas (&cp->blockBitfield, l))
                    n -= (cp->tor->blockSize - cp->tor->lastBlockSize);
                }

              assert (n <= tr_torPieceCountBytes (tor, p));
              size += n;
            }
        }

      assert (size <= inf->totalSize);
      assert (size >= cp->sizeNow);

      cp->sizeWhenDoneLazy = size;
      cp->sizeWhenDoneIsDirty = false;
    }

  return ccp->sizeWhenDoneLazy;
}

uint64_t
tr_cpLeftUntilDone (const tr_completion * cp)
{
  const uint64_t sizeWhenDone = tr_cpSizeWhenDone (cp);

  assert (sizeWhenDone >= cp->sizeNow);

  return sizeWhenDone - cp->sizeNow;
}

void
tr_cpGetAmountDone (const tr_completion * cp, float * tab, int tabCount)
{
  int i;
  const bool seed = tr_cpHasAll (cp);
  const float interval = cp->tor->info.pieceCount / (float)tabCount;

  for (i=0; i<tabCount; ++i)
    {
      if (seed)
        {
          tab[i] = 1.0f;
        }
      else
        {
          tr_block_index_t f, l;
          const tr_piece_index_t piece = (tr_piece_index_t)i * interval;
          tr_torGetPieceBlockRange (cp->tor, piece, &f, &l);
          tab[i] = tr_bitfieldCountRange (&cp->blockBitfield, f, l+1) / (float)(l+1-f);
        }
    }
}

size_t
tr_cpMissingBlocksInPiece (const tr_completion * cp, tr_piece_index_t piece)
{
  if (tr_cpHasAll (cp))
    {
      return 0;
    }
  else
    {
      tr_block_index_t f, l;
      tr_torGetPieceBlockRange (cp->tor, piece, &f, &l);
      return (l+1-f) - tr_bitfieldCountRange (&cp->blockBitfield, f, l+1);
    }
}

size_t
tr_cpMissingBytesInPiece (const tr_completion * cp, tr_piece_index_t piece)
{
  if (tr_cpHasAll (cp))
    {
      return 0;
    }
  else
    {
      size_t haveBytes = 0;
      tr_block_index_t f, l;
      const size_t pieceByteSize = tr_torPieceCountBytes (cp->tor, piece);
      tr_torGetPieceBlockRange (cp->tor, piece, &f, &l);
      if (f != l)
        {
          /* nb: we don't pass the usual l+1 here to tr_bitfieldCountRange ().
             It's faster to handle the last block separately because its size
             needs to be checked separately. */
          haveBytes = tr_bitfieldCountRange (&cp->blockBitfield, f, l);
          haveBytes *= cp->tor->blockSize;
        }

      if (tr_bitfieldHas (&cp->blockBitfield, l)) /* handle the last block */
        haveBytes += tr_torBlockCountBytes (cp->tor, l);

      assert (haveBytes <= pieceByteSize);
      return pieceByteSize - haveBytes;
    }
}

bool
tr_cpFileIsComplete (const tr_completion * cp, tr_file_index_t i)
{
  if (cp->tor->info.files[i].length == 0)
    {
      return true;
    }
  else
    {
      tr_block_index_t f, l;
      tr_torGetFileBlockRange (cp->tor, i, &f, &l);
      return tr_bitfieldCountRange (&cp->blockBitfield, f, l+1) == (l+1-f);
    }
}

void *
tr_cpCreatePieceBitfield (const tr_completion * cp, size_t * byte_count)
{
  void * ret;
  tr_piece_index_t n;
  tr_bitfield pieces;

  assert (tr_torrentHasMetadata (cp->tor));

  n = cp->tor->info.pieceCount;
  tr_bitfieldConstruct (&pieces, n);

  if (tr_cpHasAll (cp))
    {
      tr_bitfieldSetHasAll (&pieces);
    }
  else if (!tr_cpHasNone (cp))
    {
      tr_piece_index_t i;
      bool * flags = tr_new (bool, n);
      for (i=0; i<n; ++i)
        flags[i] = tr_cpPieceIsComplete (cp, i);
      tr_bitfieldSetFromFlags (&pieces, flags, n);
      tr_free (flags);
    }

  ret = tr_bitfieldGetRaw (&pieces, byte_count);
  tr_bitfieldDestruct (&pieces);
  return ret;
}

double
tr_cpPercentComplete (const tr_completion * cp)
{
  const double ratio = tr_getRatio (cp->sizeNow, cp->tor->info.totalSize);

  if ((int)ratio == TR_RATIO_NA)
    return 0.0;
  else if ((int)ratio == TR_RATIO_INF)
    return 1.0;
  else
    return ratio;
}

double
tr_cpPercentDone (const tr_completion * cp)
{
  const double ratio = tr_getRatio (cp->sizeNow, tr_cpSizeWhenDone (cp));
  const int iratio = (int)ratio;
  return ((iratio == TR_RATIO_NA) || (iratio == TR_RATIO_INF)) ? 0.0 : ratio;
}

