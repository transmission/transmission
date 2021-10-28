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
    cp->blockBitfield->setHasNone();
}

void tr_cpConstruct(tr_completion* cp, tr_torrent* tor)
{
    cp->tor = tor;
    cp->blockBitfield = new tr_bitfield(tor->blockCount);
    tr_cpReset(cp);
}

void tr_cpBlockInit(tr_completion* cp, tr_bitfield const& b)
{
    tr_cpReset(cp);

    // set blockBitfield
    *(cp->blockBitfield) = b;

    // set sizeNow
    cp->sizeNow = cp->blockBitfield->count();
    TR_ASSERT(cp->sizeNow <= cp->tor->blockCount);
    cp->sizeNow *= cp->tor->blockSize;

    if (b.test(cp->tor->blockCount - 1))
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
    tr_torrent const* tor = cp->tor;
    auto const [first, last] = tr_torGetPieceBlockRange(cp->tor, piece);
    for (tr_block_index_t i = first; i <= last; ++i)
    {
        if (tr_cpBlockIsComplete(cp, i))
        {
            cp->sizeNow -= tr_torBlockCountBytes(tor, i);
        }
    }

    cp->haveValidIsDirty = true;
    cp->sizeWhenDoneIsDirty = true;
    cp->blockBitfield->unsetRange(first, last + 1);
}

void tr_cpPieceAdd(tr_completion* cp, tr_piece_index_t piece)
{
    auto const [first, last] = tr_torGetPieceBlockRange(cp->tor, piece);
    for (tr_block_index_t i = first; i <= last; ++i)
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

        cp->blockBitfield->set(block);
        cp->sizeNow += tr_torBlockCountBytes(tor, block);

        cp->haveValidIsDirty = true;
        cp->sizeWhenDoneIsDirty = cp->sizeWhenDoneIsDirty || tor->pieceIsDnd(piece);
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
        tr_completion* cp = const_cast<tr_completion*>(ccp); /* mutable */
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
        tr_completion* cp = const_cast<tr_completion*>(ccp); /* mutable */

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

                if (!tor->pieceIsDnd(p))
                {
                    n = pieceSize;
                }
                else
                {
                    auto const [first, last] = tr_torGetPieceBlockRange(cp->tor, p);

                    n = cp->blockBitfield->count(first, last + 1);
                    n *= cp->tor->blockSize;

                    if (last == cp->tor->blockCount - 1 && cp->blockBitfield->test(last))
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
            tr_piece_index_t const piece = (tr_piece_index_t)i * interval;
            auto const [first, last] = tr_torGetPieceBlockRange(cp->tor, piece);
            tab[i] = cp->blockBitfield->count(first, last + 1) / (float)(last + 1 - first);
        }
    }
}

size_t tr_cpMissingBlocksInPiece(tr_completion const* cp, tr_piece_index_t piece)
{
    if (tr_cpHasAll(cp))
    {
        return 0;
    }

    auto const [first, last] = tr_torGetPieceBlockRange(cp->tor, piece);
    return (last + 1 - first) - cp->blockBitfield->count(first, last + 1);
}

size_t tr_cpMissingBytesInPiece(tr_completion const* cp, tr_piece_index_t piece)
{
    if (tr_cpHasAll(cp))
    {
        return 0;
    }

    size_t const pieceByteSize = tr_torPieceCountBytes(cp->tor, piece);
    auto const [first, last] = tr_torGetPieceBlockRange(cp->tor, piece);

    auto haveBytes = size_t{};
    if (first != last)
    {
        /* nb: we don't pass the usual l+1 here to Bitfield::countRange().
           It's faster to handle the last block separately because its size
           needs to be checked separately. */
        haveBytes = cp->blockBitfield->count(first, last);
        haveBytes *= cp->tor->blockSize;
    }

    if (cp->blockBitfield->test(last)) /* handle the last block */
    {
        haveBytes += tr_torBlockCountBytes(cp->tor, last);
    }

    TR_ASSERT(haveBytes <= pieceByteSize);
    return pieceByteSize - haveBytes;
}

bool tr_cpFileIsComplete(tr_completion const* cp, tr_file_index_t i)
{
    if (cp->tor->info.files[i].length == 0)
    {
        return true;
    }

    auto const [first, last] = tr_torGetFileBlockRange(cp->tor, i);
    return cp->blockBitfield->count(first, last + 1) == (last + 1 - first);
}

std::vector<uint8_t> tr_cpCreatePieceBitfield(tr_completion const* cp)
{
    TR_ASSERT(tr_torrentHasMetadata(cp->tor));

    auto const n = cp->tor->info.pieceCount;

    auto pieces = tr_bitfield{ n };

    if (tr_cpHasAll(cp))
    {
        pieces.setHasAll();
    }
    else if (!tr_cpHasNone(cp))
    {
        bool* flags = tr_new(bool, n);

        for (tr_piece_index_t i = 0; i < n; ++i)
        {
            flags[i] = tr_cpPieceIsComplete(cp, i);
        }

        pieces.setFromBools(flags, n);
        tr_free(flags);
    }

    return pieces.raw();
}

double tr_cpPercentComplete(tr_completion const* cp)
{
    double const ratio = tr_getRatio(cp->sizeNow, cp->tor->info.totalSize);

    if ((int)ratio == TR_RATIO_NA)
    {
        return 0.0;
    }

    if ((int)ratio == TR_RATIO_INF)
    {
        return 1.0;
    }

    return ratio;
}

double tr_cpPercentDone(tr_completion const* cp)
{
    double const ratio = tr_getRatio(cp->sizeNow, tr_cpSizeWhenDone(cp));
    int const iratio = (int)ratio;
    return (iratio == TR_RATIO_NA || iratio == TR_RATIO_INF) ? 0.0 : ratio;
}
