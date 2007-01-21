/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#include "transmission.h"

struct tr_io_s
{
    tr_torrent_t * tor;

    /* Position of pieces
       -1 = we haven't started to download this piece yet
        n = we have started or completed the piece in slot n */
    int         * pieceSlot;

    /* Pieces in slot
       -1 = unused slot
        n = piece n */
    int         * slotPiece;

    int           slotsUsed;
};

#include "fastresume.h"

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static int  checkFiles( tr_io_t * );
static void closeFiles( tr_io_t * );
static int  readOrWriteBytes( tr_io_t *, uint64_t, int, uint8_t *, int );
static int  readOrWriteSlot( tr_io_t * io, int slot, uint8_t * buf,
                             int * size, int write );
static void findSlotForPiece( tr_io_t *, int );

#define readBytes(io,o,s,b)  readOrWriteBytes(io,o,s,b,0)
#define writeBytes(io,o,s,b) readOrWriteBytes(io,o,s,b,1)

#define readSlot(io,sl,b,s)  readOrWriteSlot(io,sl,b,s,0)
#define writeSlot(io,sl,b,s) readOrWriteSlot(io,sl,b,s,1)

/***********************************************************************
 * tr_ioLoadResume
 ***********************************************************************
 * Try to load the fast resume file
 **********************************************************************/
void tr_ioLoadResume( tr_torrent_t * tor )
{
    tr_io_t * io;
    tr_info_t * inf = &tor->info;

    io      = malloc( sizeof( tr_io_t ) );
    io->tor = tor;

    io->pieceSlot = malloc( inf->pieceCount * sizeof( int ) );
    io->slotPiece = malloc( inf->pieceCount * sizeof( int ) );

    fastResumeLoad( io );

    free( io->pieceSlot );
    free( io->slotPiece );
    free( io );
}

/***********************************************************************
 * tr_ioInit
 ***********************************************************************
 * Open all files we are going to write to
 **********************************************************************/
tr_io_t * tr_ioInit( tr_torrent_t * tor )
{
    tr_io_t * io;

    io      = malloc( sizeof( tr_io_t ) );
    io->tor = tor;

    if( checkFiles( io ) )
    {
        free( io );
        return NULL;
    }

    return io;
}

/***********************************************************************
 * tr_ioRead
 **********************************************************************/
int tr_ioRead( tr_io_t * io, int index, int begin, int length,
               uint8_t * buf )
{
    tr_info_t * inf = &io->tor->info;
    uint64_t    offset;

    offset = (uint64_t) io->pieceSlot[index] *
        (uint64_t) inf->pieceSize + (uint64_t) begin;

    return readBytes( io, offset, length, buf );
}

/***********************************************************************
 * tr_ioWrite
 **********************************************************************/
int tr_ioWrite( tr_io_t * io, int index, int begin, int length,
                uint8_t * buf )
{
    tr_info_t    * inf = &io->tor->info;
    uint64_t       offset;

    if( io->pieceSlot[index] < 0 )
    {
        findSlotForPiece( io, index );
        tr_inf( "Piece %d: starting in slot %d", index,
                io->pieceSlot[index] );
    }

    offset = (uint64_t) io->pieceSlot[index] *
        (uint64_t) inf->pieceSize + (uint64_t) begin;

    return writeBytes( io, offset, length, buf );
}

/***********************************************************************
 * tr_ioHash
 **********************************************************************/
int tr_ioHash( tr_io_t * io, int index )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &io->tor->info;
    int            pieceSize;
    uint8_t      * pieceBuf;
    uint8_t        hash[SHA_DIGEST_LENGTH];
    int            hashFailed;
    int            ret;
    int            i;

    pieceSize = tr_pieceSize( index );
    pieceBuf  = malloc( pieceSize );
    if( ( ret = readBytes( io, (uint64_t) io->pieceSlot[index] *
                    (uint64_t) inf->pieceSize, pieceSize, pieceBuf ) ) )
    {
        free( pieceBuf );
        return ret;
    }
    SHA1( pieceBuf, pieceSize, hash );
    free( pieceBuf );

    hashFailed = memcmp( hash, &inf->pieces[20*index], SHA_DIGEST_LENGTH );
    if( hashFailed )
    {
        tr_inf( "Piece %d (slot %d): hash FAILED", index,
                io->pieceSlot[index] );
        tr_cpPieceRem( tor->completion, index );
    }
    else
    {
        tr_inf( "Piece %d (slot %d): hash OK", index,
                io->pieceSlot[index] );
        tr_cpPieceAdd( tor->completion, index );
    }

    /* Assign blame or credit to peers */
    for( i = 0; i < tor->peerCount; i++ )
    {
        tr_peerBlame( tor->peers[i], index, !hashFailed );
    }

    return 0;
}

/***********************************************************************
 * tr_ioSync
 **********************************************************************/
void tr_ioSync( tr_io_t * io )
{
    closeFiles( io );
    fastResumeSave( io );
}

/***********************************************************************
 * tr_ioClose
 **********************************************************************/
void tr_ioClose( tr_io_t * io )
{
    tr_ioSync( io );

    free( io->pieceSlot );
    free( io->slotPiece );
    free( io );
}

/***********************************************************************
 * checkFiles
 ***********************************************************************
 * Look for pieces
 **********************************************************************/
static int checkFiles( tr_io_t * io )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;

    int i;
    uint8_t * buf;
    uint8_t hash[SHA_DIGEST_LENGTH];

    io->pieceSlot = malloc( inf->pieceCount * sizeof( int ) );
    io->slotPiece = malloc( inf->pieceCount * sizeof( int ) );

    if( !fastResumeLoad( io ) )
    {
        return 0;
    }

    tr_dbg( "Checking pieces..." );

    /* Yet we don't have anything */
    memset( io->pieceSlot, 0xFF, inf->pieceCount * sizeof( int ) );
    memset( io->slotPiece, 0xFF, inf->pieceCount * sizeof( int ) );

    /* Check pieces */
    io->slotsUsed = 0;
    buf           = malloc( inf->pieceSize );
    for( i = 0; i < inf->pieceCount; i++ )
    {
        int size, j;

        if( readSlot( io, i, buf, &size ) )
        {
            break;
        }

        io->slotsUsed = i + 1;
        SHA1( buf, size, hash );

        for( j = i; j < inf->pieceCount - 1; j++ )
        {
            if( !memcmp( hash, &inf->pieces[20*j], SHA_DIGEST_LENGTH ) )
            {
                io->pieceSlot[j] = i;
                io->slotPiece[i] = j;

                tr_cpPieceAdd( tor->completion, j );
                break;
            }
        }

        if( io->slotPiece[i] > -1 )
        {
            continue;
        }

        /* Special case for the last piece */
        SHA1( buf, tr_pieceSize( inf->pieceCount - 1 ), hash );
        if( !memcmp( hash, &inf->pieces[20 * (inf->pieceCount - 1)],
                     SHA_DIGEST_LENGTH ) )
        {
            io->pieceSlot[inf->pieceCount - 1] = i;
            io->slotPiece[i]                   = inf->pieceCount - 1;

            tr_cpPieceAdd( tor->completion, inf->pieceCount - 1 );
        }
    }
    free( buf );

    return 0;
}

/***********************************************************************
 * closeFiles
 **********************************************************************/
static void closeFiles( tr_io_t * io )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;

    int i;

    for( i = 0; i < inf->fileCount; i++ )
    {
        tr_fdFileClose( tor->fdlimit, tor->destination,
                        inf->files[i].name );
    }
}

/***********************************************************************
 * readOrWriteBytes
 ***********************************************************************
 *
 **********************************************************************/
typedef size_t (* iofunc) ( int, void *, size_t );
static int readOrWriteBytes( tr_io_t * io, uint64_t offset, int size,
                             uint8_t * buf, int isWrite )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;

    int    piece = offset / inf->pieceSize;
    int    begin = offset % inf->pieceSize;
    int    i;
    size_t cur;
    int    file;
    iofunc readOrWrite = isWrite ? (iofunc) write : (iofunc) read;
    int    ret = 0;

    /* Release the torrent lock so the UI can still update itself if
       this blocks for a while */
    tr_lockUnlock( &tor->lock );

    /* We don't ever read or write more than a piece at a time */
    if( tr_pieceSize( piece ) < begin + size )
    {
        tr_err( "readOrWriteBytes: trying to write more than a piece" );
        ret = TR_ERROR_ASSERT;
        goto cleanup;
    }

    /* Find which file we shall start reading/writing in */
    for( i = 0; i < inf->fileCount; i++ )
    {
        if( offset < inf->files[i].length )
        {
            /* This is the file */
            break;
        }
        offset -= inf->files[i].length;
    }
    if( i >= inf->fileCount )
    {
        /* Should not happen */
        tr_err( "readOrWriteBytes: offset out of range (%"PRIu64", %d, %d)",
                offset, size, isWrite );
        ret = TR_ERROR_ASSERT;
        goto cleanup;
    }

    while( size > 0 )
    {
        /* How much can we put or take with this file */
        if( inf->files[i].length < offset + size )
        {
            cur = (int) ( inf->files[i].length - offset );
        }
        else
        {
            cur = size;
        }

        if( cur > 0 )
        {
            /* Now let's get a descriptor on the file... */
            file = tr_fdFileOpen( tor->fdlimit, tor->destination,
                                  inf->files[i].name, isWrite );
            if( file < 0 )
            {
                ret = file;
                goto cleanup;
            }

            /* seek to the right offset... */
            if( lseek( file, offset, SEEK_SET ) < 0 )
            {
                tr_fdFileRelease( tor->fdlimit, file );
                ret = TR_ERROR_IO_OTHER;
                goto cleanup;
            }

            /* do what we are here to do... */
            if( readOrWrite( file, buf, cur ) != cur )
            {
                tr_fdFileRelease( tor->fdlimit, file );
                ret = TR_ERROR_IO_OTHER;
                goto cleanup;
            }

            /* and release the descriptor. */
            tr_fdFileRelease( tor->fdlimit, file );
        }

        /* 'cur' bytes done, 'size - cur' bytes to go with the next file */
        i      += 1;
        offset  = 0;
        size   -= cur;
        buf    += cur;
    }

cleanup:
    tr_lockLock( &tor->lock );
    return ret;
}

/***********************************************************************
 * readSlot
 ***********************************************************************
 * 
 **********************************************************************/
static int readOrWriteSlot( tr_io_t * io, int slot, uint8_t * buf,
                            int * size, int write )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;

    uint64_t offset = (uint64_t) slot * (uint64_t) inf->pieceSize;
    
    *size = 0;
    if( slot == inf->pieceCount - 1 )
    {
        *size = inf->totalSize % inf->pieceSize;
    }
    if( !*size )
    {
        *size = inf->pieceSize;
    }

    return readOrWriteBytes( io, offset, *size, buf, write );
}

static void invertSlots( tr_io_t * io, int slot1, int slot2 )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;

    uint8_t * buf1, * buf2;
    int piece1, piece2, foo;

    buf1 = calloc( inf->pieceSize, 1 );
    buf2 = calloc( inf->pieceSize, 1 );

    readSlot( io, slot1, buf1, &foo );
    readSlot( io, slot2, buf2, &foo );

    writeSlot( io, slot1, buf2, &foo );
    writeSlot( io, slot2, buf1, &foo );

    free( buf1 );
    free( buf2 );

    piece1                = io->slotPiece[slot1];
    piece2                = io->slotPiece[slot2];
    io->slotPiece[slot1]  = piece2;
    io->slotPiece[slot2]  = piece1;
    if( piece1 >= 0 )
    {
        io->pieceSlot[piece1] = slot2;
    }
    if( piece2 >= 0 )
    {
        io->pieceSlot[piece2] = slot1;
    }
}

static void reorderPieces( tr_io_t * io )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;

    int i, didInvert;

    /* Try to move pieces to their final places */
    do
    {
        didInvert = 0;

        for( i = 0; i < inf->pieceCount; i++ )
        {
            if( io->pieceSlot[i] < 0 )
            {
                /* We haven't started this piece yet */
                continue;
            }
            if( io->pieceSlot[i] == i )
            {
                /* Already in place */
                continue;
            }
            if( i >= io->slotsUsed )
            {
                /* File is not big enough yet */
                continue;
            }

            /* Move piece i into slot i */
            tr_inf( "invert %d and %d", io->pieceSlot[i], i );
            invertSlots( io, i, io->pieceSlot[i] );
            didInvert = 1;
        }
    } while( didInvert );
}

static void findSlotForPiece( tr_io_t * io, int piece )
{
    int i;
#if 0
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;

    tr_dbg( "Entering findSlotForPiece" );

    for( i = 0; i < inf->pieceCount; i++ )
        printf( "%02d ", io->slotPiece[i] );
    printf( "\n" );
    for( i = 0; i < inf->pieceCount; i++ )
        printf( "%02d ", io->pieceSlot[i] );
    printf( "\n" );
#endif

    /* Look for an empty slot somewhere */
    for( i = 0; i < io->slotsUsed; i++ )
    {
        if( io->slotPiece[i] < 0 )
        {
            io->pieceSlot[piece] = i;
            io->slotPiece[i]     = piece;
            goto reorder;
        }
    }

    /* No empty slot, extend the file */
    io->pieceSlot[piece]         = io->slotsUsed;
    io->slotPiece[io->slotsUsed] = piece;
    (io->slotsUsed)++;

  reorder:
    reorderPieces( io );

#if 0
    for( i = 0; i < inf->pieceCount; i++ )
        printf( "%02d ", io->slotPiece[i] );
    printf( "\n" );
    for( i = 0; i < inf->pieceCount; i++ )
        printf( "%02d ", io->pieceSlot[i] );
    printf( "\n" );

    printf( "Leaving findSlotForPiece\n" );
#endif
}
