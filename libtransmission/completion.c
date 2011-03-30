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
    cp->sizeNow = 0;
    cp->sizeWhenDoneIsDirty = true;
    cp->haveValidIsDirty = true;
    tr_bitfieldSetHasNone( &cp->blockBitfield );
}

void
tr_cpConstruct( tr_completion * cp, tr_torrent * tor )
{
    cp->tor = tor;
    tr_bitfieldConstruct( &cp->blockBitfield, tor->blockCount );
    tr_cpReset( cp );
}

void
tr_cpBlockInit( tr_completion * cp, const tr_bitfield * b )
{
    tr_cpReset( cp );

    /* set blockBitfield */
    tr_bitfieldSetFromBitfield( &cp->blockBitfield, b );

    /* set sizeNow */
    cp->sizeNow = tr_bitfieldCountTrueBits( &cp->blockBitfield );
    cp->sizeNow *= cp->tor->blockSize;
    if( tr_bitfieldHas( b, cp->tor->blockCount-1 ) )
        cp->sizeNow -= ( cp->tor->blockSize - cp->tor->lastBlockSize );
}

/***
****
***/

tr_completeness
tr_cpGetStatus( const tr_completion * cp )
{
    if( tr_cpHasAll( cp ) ) return TR_SEED;
    if( !tr_torrentHasMetadata( cp->tor ) ) return TR_LEECH;
    if( cp->sizeNow == tr_cpSizeWhenDone( cp ) ) return TR_PARTIAL_SEED;
    return TR_LEECH;
}

void
tr_cpPieceRem( tr_completion *  cp, tr_piece_index_t piece )
{
    tr_block_index_t i, f, l;
    const tr_torrent * tor = cp->tor;

    tr_torGetPieceBlockRange( cp->tor, piece, &f, &l );

    for( i=f; i<=l; ++i )
        if( tr_cpBlockIsComplete( cp, i ) )
            cp->sizeNow -= tr_torBlockCountBytes( tor, i );

    cp->haveValidIsDirty = true;
    cp->sizeWhenDoneIsDirty = true;
    tr_bitfieldRemRange( &cp->blockBitfield, f, l+1 );
}

void
tr_cpPieceAdd( tr_completion * cp, tr_piece_index_t piece )
{
    tr_block_index_t i, f, l;
    tr_torGetPieceBlockRange( cp->tor, piece, &f, &l );

    for( i=f; i<=l; ++i )
        tr_cpBlockAdd( cp, i );
}

void
tr_cpBlockAdd( tr_completion * cp, tr_block_index_t block )
{
    const tr_torrent * tor = cp->tor;

    if( !tr_cpBlockIsComplete( cp, block ) )
    {
        tr_bitfieldAdd( &cp->blockBitfield, block );
        cp->sizeNow += tr_torBlockCountBytes( tor, block );

        cp->haveValidIsDirty = true;
        cp->sizeWhenDoneIsDirty = true;
    }
}

/***
****
***/

uint64_t
tr_cpHaveValid( const tr_completion * ccp )
{
    if( ccp->haveValidIsDirty )
    {
        tr_piece_index_t i;
        uint64_t size = 0;
        tr_completion * cp = (tr_completion *) ccp; /* mutable */
        const tr_torrent * tor = ccp->tor;
        const tr_info * info = &tor->info;

        for( i=0; i<info->pieceCount; ++i )
            if( tr_cpPieceIsComplete( ccp, i ) )
                size += tr_torPieceCountBytes( tor, i );

        cp->haveValidLazy = size;
        cp->haveValidIsDirty = false;
    }

    return ccp->haveValidLazy;
}

uint64_t
tr_cpSizeWhenDone( const tr_completion * ccp )
{
    if( ccp->sizeWhenDoneIsDirty )
    {
        uint64_t size = 0;
        const tr_torrent * tor = ccp->tor;
        tr_completion * cp = (tr_completion *) ccp; /* mutable */

        if( tr_cpHasAll( ccp ) )
        {
            size = tor->info.totalSize;
        }
        else
        {
            tr_piece_index_t p;

            for( p=0; p<tor->info.pieceCount; ++p )
            {
                if( !tor->info.pieces[p].dnd )
                {
                    size += tr_torPieceCountBytes( tor, p );
                }
                else
                {
                    tr_block_index_t b, f, l;
                    tr_torGetPieceBlockRange( cp->tor, p, &f, &l );
                    for( b=f; b<=l; ++b )
                        if( tr_cpBlockIsComplete( cp, b ) )
                            size += tr_torBlockCountBytes( tor, b );
                }
            }
        }

        cp->sizeWhenDoneLazy = size;
        cp->sizeWhenDoneIsDirty = false;
    }

    return ccp->sizeWhenDoneLazy;
}

void
tr_cpGetAmountDone( const tr_completion * cp, float * tab, int tabCount )
{
    int i;
    const bool seed = tr_cpHasAll( cp );
    const float interval = cp->tor->info.pieceCount / (float)tabCount;

    for( i=0; i<tabCount; ++i ) {
        if( seed )
            tab[i] = 1.0f;
        else {
            tr_block_index_t f, l;
            const tr_piece_index_t piece = (tr_piece_index_t)i * interval;
            tr_torGetPieceBlockRange( cp->tor, piece, &f, &l );
            tab[i] = tr_bitfieldCountRange( &cp->blockBitfield, f, l+1 )
                                                            / (float)(l+1-f);
        }
    }
}

size_t
tr_cpMissingBlocksInPiece( const tr_completion * cp, tr_piece_index_t piece )
{
    if( tr_cpHasAll( cp ) )
        return 0;
    else {
        tr_block_index_t f, l;
        tr_torGetPieceBlockRange( cp->tor, piece, &f, &l );
        return (l+1-f) - tr_bitfieldCountRange( &cp->blockBitfield, f, l+1 );
    }
}

size_t
tr_cpMissingBytesInPiece( const tr_completion * cp, tr_piece_index_t piece )
{
    if( tr_cpHasAll( cp ) )
        return 0;
    else {
        size_t haveBytes = 0;
        tr_block_index_t f, l;
        tr_torGetPieceBlockRange( cp->tor, piece, &f, &l );
        haveBytes = tr_bitfieldCountRange( &cp->blockBitfield, f, l );
        haveBytes *= cp->tor->blockSize;
        if( tr_bitfieldHas( &cp->blockBitfield, l ) )
            haveBytes += tr_torBlockCountBytes( cp->tor, l );
        return tr_torPieceCountBytes( cp->tor, piece ) - haveBytes;
    }
}

bool
tr_cpFileIsComplete( const tr_completion * cp, tr_file_index_t i )
{
    if( cp->tor->info.files[i].length == 0 )
        return true;
    else {
        tr_block_index_t f, l;
        tr_torGetFileBlockRange( cp->tor, i, &f, &l );
        return tr_bitfieldCountRange( &cp->blockBitfield, f, l+1 ) == (l+1-f);
    }
}

void *
tr_cpCreatePieceBitfield( const tr_completion * cp, size_t * byte_count )
{
    void * ret;
    tr_bitfield pieces;
    const tr_piece_index_t n = cp->tor->info.pieceCount;
    tr_bitfieldConstruct( &pieces, n );

    if( tr_cpHasAll( cp ) )
        tr_bitfieldSetHasAll( &pieces );
    else if( !tr_cpHasNone( cp ) ) {
        tr_piece_index_t i;
        for( i=0; i<n; ++i )
            if( tr_cpPieceIsComplete( cp, i ) )
                tr_bitfieldAdd( &pieces, i );
    }

    ret = tr_bitfieldGetRaw( &pieces, byte_count );
    tr_bitfieldDestruct( &pieces );
    return ret;
}

double
tr_cpPercentComplete( const tr_completion * cp )
{
    const double ratio = tr_getRatio( cp->sizeNow, cp->tor->info.totalSize );

    if( (int)ratio == TR_RATIO_NA )
        return 0.0;
    else if( (int)ratio == TR_RATIO_INF )
        return 1.0;
    else
        return ratio;
}

double
tr_cpPercentDone( const tr_completion * cp )
{
    const double ratio = tr_getRatio( cp->sizeNow, tr_cpSizeWhenDone( cp ) );
    const int iratio = (int)ratio;
    return ((iratio == TR_RATIO_NA) || (iratio == TR_RATIO_INF)) ? 0.0 : ratio;
}
