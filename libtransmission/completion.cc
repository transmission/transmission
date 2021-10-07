/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "completion.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"

/***
****
***/

static void tr_cpReset(tr_completion* cp)
{
    cp->sizeNow = 0;
    cp->sizeWhenDoneIsDirty = true;
    cp->haveValidIsDirty = true;
    tr_bitfieldSetHasNone(&cp->blockBitfield);
}

void tr_cpConstruct(tr_completion* cp, tr_torrent* tor)
{
    cp->tor = tor;
    tr_bitfieldConstruct(&cp->blockBitfield, tor->blockCount);
    tr_cpReset(cp);
}

void tr_cpBlockInit(tr_completion* cp, tr_bitfield const* b)
{
    tr_cpReset(cp);

    /* set blockBitfield */
    tr_bitfieldSetFromBitfield(&cp->blockBitfield, b);

    /* set sizeNow */
    cp->sizeNow = tr_bitfieldCountTrueBits(&cp->blockBitfield);
    TR_ASSERT(cp->sizeNow <= cp->tor->blockCount);
    cp->sizeNow *= cp->tor->blockSize;

    if (tr_bitfieldHas(b, cp->tor->blockCount - 1))
    {
        cp->sizeNow -= (cp->tor->blockSize - cp->tor->lastBlockSize);
    }

    TR_ASSERT(cp->sizeNow <= cp->tor->info.totalSize);
}

/***
****
***/

tr_completeness tr_cpGetStatus(tr_completion const* cp)
{
    if (tr_cpHasAll(cp))
    {
        return TR_SEED;
    }

    if (!tr_torrentHasMetadata(cp->tor))
    {
        return TR_LEECH;
    }

    if (cp->sizeNow == tr_cpSizeWhenDone(cp))
    {
        return TR_PARTIAL_SEED;
    }

    return TR_LEECH;
}

void tr_cpPieceRem(tr_completion* cp, tr_piece_index_t piece)
{
    tr_block_index_t f;
    tr_block_index_t l;
    tr_torrent const* tor = cp->tor;

    tr_torGetPieceBlockRange(cp->tor, piece, &f, &l);

    for (tr_block_index_t i = f; i <= l; ++i)
    {
        if (tr_cpBlockIsComplete(cp, i))
        {
            cp->sizeNow -= tr_torBlockCountBytes(tor, i);
        }
    }

    cp->haveValidIsDirty = true;
    cp->sizeWhenDoneIsDirty = true;
    tr_bitfieldRemRange(&cp->blockBitfield, f, l + 1);
}

void tr_cpPieceAdd(tr_completion* cp, tr_piece_index_t piece)
{
    tr_block_index_t f;
    tr_block_index_t l;
    tr_torGetPieceBlockRange(cp->tor, piece, &f, &l);

    for (tr_block_index_t i = f; i <= l; ++i)
    {
        tr_cpBlockAdd(cp, i);
    }
}

void tr_cpBlockAdd(tr_completion* cp, tr_block_index_t block)
{
    tr_torrent const* tor = cp->tor;

    if (!tr_cpBlockIsComplete(cp, block))
    {
        tr_piece_index_t const piece = tr_torBlockPiece(cp->tor, block);

        tr_bitfieldAdd(&cp->blockBitfield, block);
        cp->sizeNow += tr_torBlockCountBytes(tor, block);

        cp->haveValidIsDirty = true;
        cp->sizeWhenDoneIsDirty = cp->sizeWhenDoneIsDirty || tor->info.pieces[piece].dnd;
    }
}

/***
****
***/

uint64_t tr_cpHaveValid(tr_completion const* ccp)
{
    if (ccp->haveValidIsDirty)
    {
        uint64_t size = 0;
        tr_completion* cp = (tr_completion*)ccp; /* mutable */
        tr_torrent const* tor = ccp->tor;
        tr_info const* info = &tor->info;

        for (tr_piece_index_t i = 0; i < info->pieceCount; ++i)
        {
            if (tr_cpPieceIsComplete(ccp, i))
            {
                size += tr_torPieceCountBytes(tor, i);
            }
        }

        cp->haveValidLazy = size;
        cp->haveValidIsDirty = false;
    }

    return ccp->haveValidLazy;
}

uint64_t tr_cpSizeWhenDone(tr_completion const* ccp)
{
    if (ccp->sizeWhenDoneIsDirty)
    {
        uint64_t size = 0;
        tr_torrent const* tor = ccp->tor;
        tr_info const* inf = tr_torrentInfo(tor);
        tr_completion* cp = (tr_completion*)ccp; /* mutable */

        if (tr_cpHasAll(ccp))
        {
            size = inf->totalSize;
        }
        else
        {
            for (tr_piece_index_t p = 0; p < inf->pieceCount; ++p)
            {
                uint64_t n = 0;
                uint64_t const pieceSize = tr_torPieceCountBytes(tor, p);

                if (!inf->pieces[p].dnd)
                {
                    n = pieceSize;
                }
                else
                {
                    tr_block_index_t f;
                    tr_block_index_t l;
                    tr_torGetPieceBlockRange(cp->tor, p, &f, &l);

                    n = tr_bitfieldCountRange(&cp->blockBitfield, f, l + 1);
                    n *= cp->tor->blockSize;

                    if (l == cp->tor->blockCount - 1 && tr_bitfieldHas(&cp->blockBitfield, l))
                    {
                        n -= cp->tor->blockSize - cp->tor->lastBlockSize;
                    }
                }

                TR_ASSERT(n <= tr_torPieceCountBytes(tor, p));
                size += n;
            }
        }

        TR_ASSERT(size <= inf->totalSize);
        TR_ASSERT(size >= cp->sizeNow);

        cp->sizeWhenDoneLazy = size;
        cp->sizeWhenDoneIsDirty = false;
    }

    return ccp->sizeWhenDoneLazy;
}

uint64_t tr_cpLeftUntilDone(tr_completion const* cp)
{
    uint64_t const sizeWhenDone = tr_cpSizeWhenDone(cp);

    TR_ASSERT(sizeWhenDone >= cp->sizeNow);

    return sizeWhenDone - cp->sizeNow;
}

void tr_cpGetAmountDone(tr_completion const* cp, float* tab, int tabCount)
{
    bool const seed = tr_cpHasAll(cp);
    float const interval = cp->tor->info.pieceCount / (float)tabCount;

    for (int i = 0; i < tabCount; ++i)
    {
        if (seed)
        {
            tab[i] = 1.0F;
        }
        else
        {
            tr_block_index_t f;
            tr_block_index_t l;
            tr_piece_index_t const piece = (tr_piece_index_t)i * interval;
            tr_torGetPieceBlockRange(cp->tor, piece, &f, &l);
            tab[i] = tr_bitfieldCountRange(&cp->blockBitfield, f, l + 1) / (float)(l + 1 - f);
        }
    }
}

size_t tr_cpMissingBlocksInPiece(tr_completion const* cp, tr_piece_index_t piece)
{
    if (tr_cpHasAll(cp))
    {
        return 0;
    }
    else
    {
        tr_block_index_t f;
        tr_block_index_t l;
        tr_torGetPieceBlockRange(cp->tor, piece, &f, &l);
        return (l + 1 - f) - tr_bitfieldCountRange(&cp->blockBitfield, f, l + 1);
    }
}

size_t tr_cpMissingBytesInPiece(tr_completion const* cp, tr_piece_index_t piece)
{
    if (tr_cpHasAll(cp))
    {
        return 0;
    }
    else
    {
        size_t haveBytes = 0;
        tr_block_index_t f;
        tr_block_index_t l;
        size_t const pieceByteSize = tr_torPieceCountBytes(cp->tor, piece);
        tr_torGetPieceBlockRange(cp->tor, piece, &f, &l);

        if (f != l)
        {
            /* nb: we don't pass the usual l+1 here to tr_bitfieldCountRange().
               It's faster to handle the last block separately because its size
               needs to be checked separately. */
            haveBytes = tr_bitfieldCountRange(&cp->blockBitfield, f, l);
            haveBytes *= cp->tor->blockSize;
        }

        if (tr_bitfieldHas(&cp->blockBitfield, l)) /* handle the last block */
        {
            haveBytes += tr_torBlockCountBytes(cp->tor, l);
        }

        TR_ASSERT(haveBytes <= pieceByteSize);
        return pieceByteSize - haveBytes;
    }
}

bool tr_cpFileIsComplete(tr_completion const* cp, tr_file_index_t i)
{
    if (cp->tor->info.files[i].length == 0)
    {
        return true;
    }
    else
    {
        tr_block_index_t f;
        tr_block_index_t l;
        tr_torGetFileBlockRange(cp->tor, i, &f, &l);
        return tr_bitfieldCountRange(&cp->blockBitfield, f, l + 1) == (l + 1 - f);
    }
}

void* tr_cpCreatePieceBitfield(tr_completion const* cp, size_t* byte_count)
{
    TR_ASSERT(tr_torrentHasMetadata(cp->tor));

    void* ret;
    tr_piece_index_t n;
    tr_bitfield pieces;

    n = cp->tor->info.pieceCount;
    tr_bitfieldConstruct(&pieces, n);

    if (tr_cpHasAll(cp))
    {
        tr_bitfieldSetHasAll(&pieces);
    }
    else if (!tr_cpHasNone(cp))
    {
        bool* flags = tr_new(bool, n);

        for (tr_piece_index_t i = 0; i < n; ++i)
        {
            flags[i] = tr_cpPieceIsComplete(cp, i);
        }

        tr_bitfieldSetFromFlags(&pieces, flags, n);
        tr_free(flags);
    }

    ret = tr_bitfieldGetRaw(&pieces, byte_count);
    tr_bitfieldDestruct(&pieces);
    return ret;
}

double tr_cpPercentComplete(tr_completion const* cp)
{
    double const ratio = tr_getRatio(cp->sizeNow, cp->tor->info.totalSize);

    if ((int)ratio == TR_RATIO_NA)
    {
        return 0.0;
    }
    else if ((int)ratio == TR_RATIO_INF)
    {
        return 1.0;
    }
    else
    {
        return ratio;
    }
}

double tr_cpPercentDone(tr_completion const* cp)
{
    double const ratio = tr_getRatio(cp->sizeNow, tr_cpSizeWhenDone(cp));
    int const iratio = (int)ratio;
    return (iratio == TR_RATIO_NA || iratio == TR_RATIO_INF) ? 0.0 : ratio;
}
