/******************************************************************************
 * Copyright (c) 2005 Eric Petit
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef TR_UTILS_H
#define TR_UTILS_H 1

#define TR_MSG_ERR 1
#define TR_MSG_INF 2
#define TR_MSG_DBG 4
#define tr_err( a... ) tr_msg( TR_MSG_ERR, ## a )
#define tr_inf( a... ) tr_msg( TR_MSG_INF, ## a )
#define tr_dbg( a... ) tr_msg( TR_MSG_DBG, ## a )
void tr_msg  ( int level, char * msg, ... );

int  tr_rand ( int );

/***********************************************************************
 * tr_date
 ***********************************************************************
 * Returns the current date in milliseconds
 **********************************************************************/
static inline uint64_t tr_date()
{
    struct timeval tv;
    gettimeofday( &tv, NULL );
    return( (uint64_t) tv.tv_sec * 1000 + (uint64_t) tv.tv_usec / 1000 );
}

/***********************************************************************
 * tr_wait
 ***********************************************************************
 * Wait 'delay' milliseconds
 **********************************************************************/
static inline void tr_wait( uint64_t delay )
{
#ifdef SYS_BEOS
    snooze( 1000 * delay );
#else
    usleep( 1000 * delay );
#endif
}

/***********************************************************************
 * tr_bitfieldHas
 **********************************************************************/
static inline int tr_bitfieldHas( uint8_t * bitfield, int piece )
{
    return ( bitfield[ piece / 8 ] & ( 1 << ( 7 - ( piece % 8 ) ) ) );
}

/***********************************************************************
 * tr_bitfieldAdd
 **********************************************************************/
static inline void tr_bitfieldAdd( uint8_t * bitfield, int piece )
{
    bitfield[ piece / 8 ] |= ( 1 << ( 7 - ( piece % 8 ) ) );
}

static inline void tr_bitfieldRem( uint8_t * bitfield, int piece )
{
    bitfield[ piece / 8 ] &= ~( 1 << ( 7 - ( piece % 8 ) ) );
}

#define tr_blockPiece(a) _tr_blockPiece(tor,a)
static inline int _tr_blockPiece( tr_torrent_t * tor, int block )
{
    tr_info_t * inf = &tor->info;
    return block / ( inf->pieceSize / tor->blockSize );
}

#define tr_blockSize(a) _tr_blockSize(tor,a)
static inline int _tr_blockSize( tr_torrent_t * tor, int block )
{
    tr_info_t * inf = &tor->info;
    int dummy;

    if( block != tor->blockCount - 1 ||
        !( dummy = inf->totalSize % tor->blockSize ) )
    {
        return tor->blockSize;
    }

    return dummy;
}

#define tr_blockPosInPiece(a) _tr_blockPosInPiece(tor,a)
static inline int _tr_blockPosInPiece( tr_torrent_t * tor, int block )
{
    tr_info_t * inf = &tor->info;
    return tor->blockSize *
        ( block % ( inf->pieceSize / tor->blockSize ) );
}

#define tr_pieceCountBlocks(a) _tr_pieceCountBlocks(tor,a)
static inline int _tr_pieceCountBlocks( tr_torrent_t * tor, int piece )
{
    tr_info_t * inf = &tor->info;
    if( piece < inf->pieceCount - 1 ||
        !( tor->blockCount % ( inf->pieceSize / tor->blockSize ) ) )
    {
        return inf->pieceSize / tor->blockSize;
    }
    return tor->blockCount % ( inf->pieceSize / tor->blockSize );
}

#define tr_pieceStartBlock(a) _tr_pieceStartBlock(tor,a)
static inline int _tr_pieceStartBlock( tr_torrent_t * tor, int piece )
{
    tr_info_t * inf = &tor->info;
    return piece * ( inf->pieceSize / tor->blockSize );
}

#define tr_pieceSize(a) _tr_pieceSize(tor,a)
static inline int _tr_pieceSize( tr_torrent_t * tor, int piece )
{
    tr_info_t * inf = &tor->info;
    if( piece < inf->pieceCount - 1 ||
        !( inf->totalSize % inf->pieceSize ) )
    {
        return inf->pieceSize;
    }
    return inf->totalSize % inf->pieceSize;
}

#define tr_block(a,b) _tr_block(tor,a,b)
static inline int _tr_block( tr_torrent_t * tor, int index, int begin )
{
    tr_info_t * inf = &tor->info;
    return index * ( inf->pieceSize / tor->blockSize ) +
        begin / tor->blockSize;
}

#endif
