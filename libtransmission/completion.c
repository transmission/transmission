/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h>

#include "transmission.h"
#include "completion.h"
#include "torrent.h"
#include "utils.h"

static void
tr_cpReset( tr_completion * cp )
{
    tr_bitfieldClear( &cp->pieceBitfield );
    tr_bitfieldClear( &cp->blockBitfield );
    memset( cp->completeBlocks, 0, sizeof( uint16_t ) * cp->tor->info.pieceCount );
    cp->sizeNow = 0;
    cp->sizeWhenDoneIsDirty = 1;
    cp->haveValidIsDirty = 1;
}

tr_completion *
tr_cpConstruct( tr_completion * cp, tr_torrent * tor )
{
    cp->tor = tor;
    cp->completeBlocks  = tr_new( uint16_t, tor->info.pieceCount );
    tr_bitfieldConstruct( &cp->blockBitfield, tor->blockCount );
    tr_bitfieldConstruct( &cp->pieceBitfield, tor->info.pieceCount );
    tr_cpReset( cp );
    return cp;
}

tr_completion*
tr_cpDestruct( tr_completion * cp )
{
    tr_free( cp->completeBlocks );
    tr_bitfieldDestruct( &cp->pieceBitfield );
    tr_bitfieldDestruct( &cp->blockBitfield );
    return cp;
}

void
tr_cpInvalidateDND( tr_completion * cp )
{
    cp->sizeWhenDoneIsDirty = 1;
}

uint64_t
tr_cpSizeWhenDone( const tr_completion * ccp )
{
    if( ccp->sizeWhenDoneIsDirty )
    {
        tr_completion *    cp = (tr_completion *) ccp; /* mutable */
        const tr_torrent * tor = cp->tor;
        const tr_info *    info = &tor->info;
        tr_piece_index_t   i;
        uint64_t           size = 0;

        for( i = 0; i < info->pieceCount; ++i )
        {
            if( !info->pieces[i].dnd )
            {
                /* we want the piece... */
                size += tr_torPieceCountBytes( tor, i );
            }
            else if( tr_cpPieceIsComplete( cp, i ) )
            {
                /* we have the piece... */
                size += tr_torPieceCountBytes( tor, i );
            }
            else if( cp->completeBlocks[i] )
            {
                /* we have part of the piece... */
                const tr_block_index_t b = tr_torPieceFirstBlock( tor, i );
                const tr_block_index_t e = b + tr_torPieceCountBlocks( tor,
                                                                       i );
                tr_block_index_t       j;
                for( j = b; j < e; ++j )
                    if( tr_cpBlockIsComplete( cp, j ) )
                        size += tr_torBlockCountBytes( tor, j );
            }
        }

        cp->sizeWhenDoneLazy = size;
        cp->sizeWhenDoneIsDirty = 0;
    }

    assert( ccp->sizeWhenDoneLazy <= ccp->tor->info.totalSize );
    assert( ccp->sizeWhenDoneLazy >= ccp->sizeNow );
    return ccp->sizeWhenDoneLazy;
}

void
tr_cpPieceAdd( tr_completion *  cp,
               tr_piece_index_t piece )
{
    const tr_torrent *     tor = cp->tor;
    const tr_block_index_t start = tr_torPieceFirstBlock( tor, piece );
    const tr_block_index_t end = start + tr_torPieceCountBlocks( tor, piece );
    tr_block_index_t       i;

    for( i = start; i < end; ++i )
        tr_cpBlockAdd( cp, i );
}

void
tr_cpPieceRem( tr_completion *  cp,
               tr_piece_index_t piece )
{
    const tr_torrent *     tor = cp->tor;
    const tr_block_index_t start = tr_torPieceFirstBlock( tor, piece );
    const tr_block_index_t end = start + tr_torPieceCountBlocks( tor, piece );
    tr_block_index_t       block;

    assert( cp );
    assert( piece < tor->info.pieceCount );
    assert( start < tor->blockCount );
    assert( start <= end );
    assert( end <= tor->blockCount );

    for( block = start; block < end; ++block )
        if( tr_cpBlockIsComplete( cp, block ) )
            cp->sizeNow -= tr_torBlockCountBytes( tor, block );

    cp->sizeWhenDoneIsDirty = 1;
    cp->haveValidIsDirty = 1;
    cp->completeBlocks[piece] = 0;
    tr_bitfieldRemRange ( &cp->blockBitfield, start, end );
    tr_bitfieldRem( &cp->pieceBitfield, piece );
}

void
tr_cpBlockAdd( tr_completion * cp, tr_block_index_t block )
{
    const tr_torrent * tor = cp->tor;

    if( !tr_cpBlockIsComplete( cp, block ) )
    {
        const tr_piece_index_t piece = tr_torBlockPiece( tor, block );
        const int              blockSize = tr_torBlockCountBytes( tor,
                                                                  block );

        ++cp->completeBlocks[piece];

        if( tr_cpPieceIsComplete( cp, piece ) )
            tr_bitfieldAdd( &cp->pieceBitfield, piece );

        tr_bitfieldAdd( &cp->blockBitfield, block );

        cp->sizeNow += blockSize;

        cp->haveValidIsDirty = 1;
        cp->sizeWhenDoneIsDirty = 1;
    }
}

/* Initialize a completion object from a bitfield indicating which blocks we have */
tr_bool
tr_cpBlockBitfieldSet( tr_completion * cp, tr_bitfield * blockBitfield )
{
    int success = FALSE;

    assert( cp );
    assert( blockBitfield );

    /* The bitfield of block flags is typically loaded from a resume file.
       Test the bitfield's length in case the resume file somehow got corrupted */
    if(( success = blockBitfield->byteCount == cp->blockBitfield.byteCount ))
    {
        tr_block_index_t b = 0;
        tr_piece_index_t p = 0;
        uint32_t pieceBlock = 0;
        uint32_t completeBlocksInPiece = 0;
        tr_block_index_t completeBlocksInTorrent = 0;
        uint32_t blocksInCurrentPiece = tr_torPieceCountBlocks( cp->tor, p );

        /* start cp with a state where it thinks we have nothing */
        tr_cpReset( cp );

        /* init our block bitfield from the one passed in */
        memcpy( cp->blockBitfield.bits, blockBitfield->bits, blockBitfield->byteCount );

        /* invalidate the fields that are lazy-evaluated */
        cp->sizeWhenDoneIsDirty = TRUE;
        cp->haveValidIsDirty = TRUE;

        /* to set the remaining fields, we walk through every block... */
        while( b < cp->tor->blockCount )
        {
            if( tr_bitfieldHasFast( blockBitfield, b ) )
                ++completeBlocksInPiece;

            ++b;
            ++pieceBlock;

            /* by the time we reach the end of a piece, we have enough info
               to update that piece's slot in cp.completeBlocks and cp.pieceBitfield */
            if( pieceBlock == blocksInCurrentPiece )
            {
                cp->completeBlocks[p] = completeBlocksInPiece;
                completeBlocksInTorrent += completeBlocksInPiece;
                if( completeBlocksInPiece == blocksInCurrentPiece )
                    tr_bitfieldAdd( &cp->pieceBitfield, p );

                /* reset the per-piece counters because we're starting on a new piece now */
                ++p;
                completeBlocksInPiece = 0;
                pieceBlock = 0;
                blocksInCurrentPiece = tr_torPieceCountBlocks( cp->tor, p );
            }
        }

        /* update sizeNow */
        cp->sizeNow = completeBlocksInTorrent;
        cp->sizeNow *= tr_torBlockCountBytes( cp->tor, 0 );
        if( tr_bitfieldHasFast( &cp->blockBitfield, cp->tor->blockCount-1 ) ) {
            /* the last block is usually smaller than the other blocks,
               so handle that special case or cp->sizeNow might be too large */
            cp->sizeNow -= tr_torBlockCountBytes( cp->tor, 0 );
            cp->sizeNow += tr_torBlockCountBytes( cp->tor, cp->tor->blockCount-1 );
        }
    }

    return success;
}

/***
****
***/

tr_completeness
tr_cpGetStatus( const tr_completion * cp )
{
    if( cp->sizeNow == cp->tor->info.totalSize ) return TR_SEED;
    if( cp->sizeNow == tr_cpSizeWhenDone( cp ) ) return TR_PARTIAL_SEED;
    return TR_LEECH;
}

static uint64_t
calculateHaveValid( const tr_completion * ccp )
{
    uint64_t                  b = 0;
    tr_piece_index_t          i;
    const tr_torrent        * tor            = ccp->tor;
    const uint64_t            pieceSize      = tor->info.pieceSize;
    const uint64_t            lastPieceSize  = tor->lastPieceSize;
    const tr_piece_index_t    lastPiece      = tor->info.pieceCount - 1;

    for( i=0; i!=lastPiece; ++i )
        if( tr_cpPieceIsComplete( ccp, i ) )
            b += pieceSize;

    if( tr_cpPieceIsComplete( ccp, lastPiece ) )
        b += lastPieceSize;

    return b;
}

uint64_t
tr_cpHaveValid( const tr_completion * ccp )
{
    if( ccp->haveValidIsDirty )
    {
        tr_completion * cp = (tr_completion *) ccp; /* mutable */
        cp->haveValidLazy = calculateHaveValid( ccp );
        cp->haveValidIsDirty = 0;
    }

    return ccp->haveValidLazy;
}

void
tr_cpGetAmountDone( const tr_completion * cp,
                    float *               tab,
                    int                   tabCount )
{
    int                i;
    const tr_torrent * tor = cp->tor;
    const float        interval = tor->info.pieceCount / (float)tabCount;
    const int          isSeed = tr_cpGetStatus( cp ) == TR_SEED;

    for( i = 0; i < tabCount; ++i )
    {
        const tr_piece_index_t piece = i * interval;

        if( tor == NULL )
            tab[i] = 0.0f;
        else if( isSeed || tr_cpPieceIsComplete( cp, piece ) )
            tab[i] = 1.0f;
        else
            tab[i] = (float)cp->completeBlocks[piece] /
                     tr_torPieceCountBlocks( tor, piece );
    }
}

int
tr_cpMissingBlocksInPiece( const tr_completion * cp, tr_piece_index_t piece )
{
    return tr_torPieceCountBlocks( cp->tor, piece ) - cp->completeBlocks[piece];
}


tr_bool
tr_cpPieceIsComplete( const tr_completion * cp, tr_piece_index_t piece )
{
    return cp->completeBlocks[piece] == tr_torPieceCountBlocks( cp->tor, piece );
}

tr_bool
tr_cpFileIsComplete( const tr_completion * cp, tr_file_index_t fileIndex )
{
    tr_block_index_t block;

    const tr_torrent * tor = cp->tor;
    const tr_file * file = &tor->info.files[fileIndex];
    const tr_block_index_t firstBlock = file->offset / tor->blockSize;
    const tr_block_index_t lastBlock = ( file->offset + file->length - 1 ) / tor->blockSize;

    assert( tr_torBlockPiece( tor, firstBlock ) == file->firstPiece );
    assert( tr_torBlockPiece( tor, lastBlock ) == file->lastPiece );

    for( block=firstBlock; block<=lastBlock; ++block )
        if( !tr_cpBlockIsComplete( cp, block ) )
            return FALSE;

    return TRUE;
}
