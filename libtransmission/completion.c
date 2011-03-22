/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <string.h> /* memcpy() */

#include "transmission.h"
#include "completion.h"
#include "torrent.h"
#include "utils.h"

/***
****
***/

static void
tr_cpReset( tr_completion * cp )
{
    tr_bitsetSetHaveNone( &cp->blockBitset );
    tr_free( cp->completeBlocks );
    cp->completeBlocks = NULL;
    cp->sizeNow = 0;
    cp->sizeWhenDoneIsDirty = true;
    cp->blocksWantedIsDirty = true;
    cp->haveValidIsDirty = true;
}

tr_completion *
tr_cpConstruct( tr_completion * cp, tr_torrent * tor )
{
    cp->tor = tor;
    cp->completeBlocks = NULL;
    tr_bitsetConstruct( &cp->blockBitset, tor->blockCount );
    tr_cpReset( cp );
    return cp;
}

tr_completion*
tr_cpDestruct( tr_completion * cp )
{
    tr_free( cp->completeBlocks );
    tr_bitsetDestruct( &cp->blockBitset );
    return cp;
}

/***
****
***/

static inline bool
isSeed( const tr_completion * cp )
{
    return cp->blockBitset.haveAll;
}

tr_completeness
tr_cpGetStatus( const tr_completion * cp )
{
    if( !tr_torrentHasMetadata( cp->tor ) ) return TR_LEECH;
    if( cp->sizeNow == cp->tor->info.totalSize ) return TR_SEED;
    if( cp->sizeNow == tr_cpSizeWhenDone( cp ) ) return TR_PARTIAL_SEED;
    return TR_LEECH;
}

/* how many blocks are in this piece? */
static inline uint16_t
countBlocksInPiece( const tr_torrent * tor, const tr_piece_index_t piece )
{
    return piece + 1 == tor->info.pieceCount ? tor->blockCountInLastPiece
                                             : tor->blockCountInPiece;
}

static uint16_t *
getCompleteBlocks( const tr_completion * ccp )
{
    if( ccp->completeBlocks == NULL )
    {
        tr_completion * cp = (tr_completion*) ccp;
        cp->completeBlocks = tr_new0( uint16_t, ccp->tor->info.pieceCount );
    }

    return ccp->completeBlocks;
}

void
tr_cpInvalidateDND( tr_completion * cp )
{
    cp->sizeWhenDoneIsDirty = true;
    cp->blocksWantedIsDirty = true;
}

tr_block_index_t
tr_cpBlocksMissing( const tr_completion * ccp )
{
    if( isSeed( ccp ) )
        return 0;

    if( ccp->blocksWantedIsDirty )
    {
        tr_piece_index_t   i;
        tr_block_index_t   wanted = 0;
        tr_block_index_t   complete = 0;
        tr_completion    * cp = (tr_completion *) ccp; /* mutable */
        const uint16_t   * complete_blocks = getCompleteBlocks( cp );
        const tr_torrent * tor = ccp->tor;
        const tr_info    * info = &tor->info;

        for( i = 0; i < info->pieceCount; ++i )
        {
            if( !info->pieces[i].dnd )
            {
                wanted += countBlocksInPiece( tor, i );
                complete += complete_blocks[i];
            }
        }

        cp->blocksWantedLazy = wanted;
        cp->blocksWantedCompleteLazy = complete;
        cp->blocksWantedIsDirty = false;
    }

    return ccp->blocksWantedLazy - ccp->blocksWantedCompleteLazy;
}

void
tr_cpPieceRem( tr_completion *  cp, tr_piece_index_t piece )
{
    tr_block_index_t i;
    tr_block_index_t first;
    tr_block_index_t last;
    const tr_torrent * tor = cp->tor;
    uint16_t * complete_blocks = getCompleteBlocks( cp );

    tr_torGetPieceBlockRange( cp->tor, piece, &first, &last );
    for( i=first; i<=last; ++i )
        if( tr_cpBlockIsComplete( cp, i ) )
            cp->sizeNow -= tr_torBlockCountBytes( tor, i );

    if( !tor->info.pieces[piece].dnd )
        cp->blocksWantedCompleteLazy -= complete_blocks[piece];

    cp->sizeWhenDoneIsDirty = true;
    cp->haveValidIsDirty = true;
    complete_blocks[piece] = 0;
    tr_bitsetRemRange( &cp->blockBitset, first, last+1 );
}

void
tr_cpPieceAdd( tr_completion * cp, tr_piece_index_t piece )
{
    tr_block_index_t i;
    tr_block_index_t first;
    tr_block_index_t last;
    tr_torGetPieceBlockRange( cp->tor, piece, &first, &last );

    for( i=first; i<=last; ++i )
        tr_cpBlockAdd( cp, i );
}

void
tr_cpBlockAdd( tr_completion * cp, tr_block_index_t block )
{
    const tr_torrent * tor = cp->tor;

    if( !tr_cpBlockIsComplete( cp, block ) )
    {
        const tr_piece_index_t piece = tr_torBlockPiece( tor, block );
        const int blockSize = tr_torBlockCountBytes( tor, block );

        getCompleteBlocks(cp)[piece]++;

        tr_bitsetAdd( &cp->blockBitset, block );

        cp->sizeNow += blockSize;
        if( !tor->info.pieces[piece].dnd )
            cp->blocksWantedCompleteLazy++;

        cp->sizeWhenDoneIsDirty = true;
        cp->haveValidIsDirty = true;
    }
}


bool
tr_cpBlockBitsetInit( tr_completion * cp, const tr_bitset * blocks )
{
    bool success = false;
    tr_torrent * tor = cp->tor;

    /* start cp with a state where it thinks we have nothing */
    tr_cpReset( cp );

    if( blocks->haveAll )
    {
        tr_bitsetSetHaveAll( &cp->blockBitset );
        cp->sizeNow = tor->info.totalSize;

        success = true;
    }
    else if( blocks->haveNone )
    {
        /* already reset... */
        success = true;
    }
    else
    {
        const tr_bitfield * src = &blocks->bitfield;
        tr_bitfield * tgt = &cp->blockBitset.bitfield;

        tr_bitfieldConstruct( tgt, tor->blockCount );

        /* The bitfield of block flags is typically loaded from a resume file.
           Test the bitfield's length in case the resume file is corrupt */
        if(( success = src->byteCount == tgt->byteCount ))
        {
            size_t i = 0;
            uint16_t * complete_blocks_in_piece = getCompleteBlocks( cp );

            /* init our block bitfield from the one passed in */
            memcpy( tgt->bits, src->bits, src->byteCount );

            /* update cp.sizeNow and the cp.blockBitset flags */
            i = tr_bitfieldCountTrueBits( tgt );
            if( i == tor->blockCount ) {
                tr_bitsetSetHaveAll( &cp->blockBitset );
                cp->sizeNow = cp->tor->info.totalSize;
            } else if( !i ) {
                tr_bitsetSetHaveNone( &cp->blockBitset );
                cp->sizeNow = 0;
            } else {
                cp->blockBitset.haveAll = cp->blockBitset.haveNone = false;
                cp->sizeNow = tr_bitfieldCountRange( tgt, 0, tor->blockCount-1 );
                cp->sizeNow *= tor->blockSize;
                if( tr_bitfieldHas( tgt, tor->blockCount-1 ) )
                    cp->sizeNow += tr_torBlockCountBytes( tor, tor->blockCount-1 );
            }

            /* update complete_blocks_in_piece */
            for( i=0; i<tor->info.pieceCount; ++i ) {
                 tr_block_index_t first, last;
                 tr_torGetPieceBlockRange( tor, i, &first, &last );
                 complete_blocks_in_piece[i] = tr_bitfieldCountRange( src, first, last+1 );
            }
        }
    }

    return success;
}

/***
****
***/

uint64_t
tr_cpHaveValid( const tr_completion * ccp )
{
    if( ccp->haveValidIsDirty )
    {
        tr_piece_index_t   i;
        uint64_t           size = 0;
        tr_completion    * cp = (tr_completion *) ccp; /* mutable */
        const tr_torrent * tor = ccp->tor;
        const tr_info    * info = &tor->info;

        for( i=0; i<info->pieceCount; ++i )
            if( tr_cpPieceIsComplete( ccp, i ) )
                size += tr_torPieceCountBytes( tor, i );

        cp->haveValidIsDirty = false;
        cp->haveValidLazy = size;
    }

    return ccp->haveValidLazy;
}

uint64_t
tr_cpSizeWhenDone( const tr_completion * ccp )
{
    if( ccp->sizeWhenDoneIsDirty )
    {
        tr_piece_index_t   i;
        uint64_t           size = 0;
        tr_completion    * cp = (tr_completion *) ccp; /* mutable */
        const tr_torrent * tor = ccp->tor;
        const tr_info    * info = &tor->info;

        for( i=0; i<info->pieceCount; ++i )
            if( !info->pieces[i].dnd || tr_cpPieceIsComplete( cp, i ) )
                size += tr_torPieceCountBytes( tor, i );

        cp->sizeWhenDoneIsDirty = false;
        cp->sizeWhenDoneLazy = size;
    }

    return ccp->sizeWhenDoneLazy;
}

void
tr_cpGetAmountDone( const tr_completion * cp, float * tab, int tabCount )
{
    int i;
    const float interval = cp->tor->info.pieceCount / (float)tabCount;
    const bool seed = isSeed( cp );

    for( i=0; i<tabCount; ++i ) {
        if( seed )
            tab[i] = 1.0f;
        else {
            const tr_piece_index_t piece = (tr_piece_index_t)i * interval;
            tab[i] = getCompleteBlocks(cp)[piece] / (float)countBlocksInPiece( cp->tor, piece );
        }
    }
}

int
tr_cpMissingBlocksInPiece( const tr_completion * cp, tr_piece_index_t i )
{
    if( isSeed( cp ) )
        return 0;

    return countBlocksInPiece( cp->tor, i ) - getCompleteBlocks(cp)[i];
}

bool
tr_cpFileIsComplete( const tr_completion * cp, tr_file_index_t i )
{
    tr_block_index_t f, l;

    if( cp->tor->info.files[i].length == 0 )
        return true;

    tr_torGetFileBlockRange( cp->tor, i, &f, &l );
    return tr_bitsetCountRange( &cp->blockBitset, f, l+1 ) == (l+1-f);
}

tr_bitfield *
tr_cpCreatePieceBitfield( const tr_completion * cp )
{
    tr_piece_index_t i;
    const tr_piece_index_t n = cp->tor->info.pieceCount;
    tr_bitfield * bf = tr_bitfieldNew( n );

    for( i=0; i<n; ++i )
        if( tr_cpPieceIsComplete( cp, i ) )
            tr_bitfieldAdd( bf, i );

    return bf;
}
