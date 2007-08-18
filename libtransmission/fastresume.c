/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

/***********************************************************************
 * Fast resume
 ***********************************************************************
 * The format of the resume file is a 4 byte format version (currently 1),
 * followed by several variable-sized blocks of data.  Each block is
 * preceded by a 1 byte ID and a 4 byte length.  The currently recognized
 * IDs are defined below by the FR_ID_* macros.  The length does not include
 * the 5 bytes for the ID and length.
 *
 * The name of the resume file is "resume.<hash>-<tag>", although
 * older files with a name of "resume.<hash>" will be recognized if
 * the former doesn't exist.
 *
 * All values are stored in the native endianness. Moving a
 * libtransmission resume file from an architecture to another will not
 * work, although it will not hurt either (the version will be wrong,
 * so the resume file will not be read).
 **********************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "transmission.h"
#include "completion.h"
#include "fastresume.h"
#include "peer.h"
#include "platform.h"
#include "utils.h"

/* time_t can be 32 or 64 bits... for consistency we'll hardwire 64 */ 
typedef uint64_t tr_time_t; 

enum
{
    /* deprecated */
    FR_ID_PROGRESS_SLOTS = 1,

    /* number of bytes downloaded */
    FR_ID_DOWNLOADED = 2,

    /* number of bytes uploaded */
    FR_ID_UPLOADED = 3,

    /* IPs and ports of connectable peers */
    FR_ID_PEERS = 4,

    /* progress data:
     *  - 4 bytes * number of files: mtimes of files
     *  - 1 bit * number of blocks: whether we have the block or not */
    FR_ID_PROGRESS = 5,

    /* dnd and priority 
     * char * number of files: l,n,h for low, normal, high priority
     * char * number of files: t,f for DND flags */
    FR_ID_PRIORITY = 6,

    /* transfer speeds
     * uint32_t: dl speed rate to use when the mode is single
     * uint32_t: dl's tr_speedlimit_t
     * uint32_t: ul speed rate to use when the mode is single
     * uint32_t: ul's tr_speedlimit_t
     */
    FR_ID_SPEED = 8,

    /* active
     * char: 't' if running, 'f' if paused
     */
    FR_ID_RUN = 9
};


/* macros for the length of various pieces of the progress data */
#define FR_MTIME_LEN( t ) \
  ( sizeof(tr_time_t) * (t)->info.fileCount )
#define FR_BLOCK_BITFIELD_LEN( t ) \
  ( ( (t)->blockCount + 7 ) / 8 )
#define FR_PROGRESS_LEN( t ) \
  ( FR_MTIME_LEN( t ) + FR_BLOCK_BITFIELD_LEN( t ) )
#define FR_SPEED_LEN (2 * (sizeof(uint16_t) + sizeof(uint8_t) ) )

static void
fastResumeFileName( char * buf, size_t buflen, const tr_torrent_t * tor, int tag )
{
    const char * cacheDir = tr_getCacheDirectory ();
    const char * hash = tor->info.hashString;

    if( !tag )
    {
        tr_buildPath( buf, buflen, cacheDir, hash, NULL );
    }
    else
    {
        char base[1024];
        snprintf( base, sizeof(base), "%s-%s", hash, tor->handle->tag );
        tr_buildPath( buf, buflen, cacheDir, base, NULL );
    }
}

static tr_time_t*
getMTimes( const tr_torrent_t * tor, int * setme_n )
{
    int i;
    const int n = tor->info.fileCount;
    tr_time_t * m = calloc( n, sizeof(tr_time_t) );

    for( i=0; i<n; ++i ) {
        char fname[MAX_PATH_LENGTH];
        struct stat sb;
        tr_buildPath( fname, sizeof(fname),
                      tor->destination, tor->info.files[i].name, NULL );
        if ( !stat( fname, &sb ) && S_ISREG( sb.st_mode ) ) {
#ifdef SYS_DARWIN
            m[i] = sb.st_mtimespec.tv_sec;
#else
            m[i] = sb.st_mtime;
#endif
        }
    }

    *setme_n = n;
    return m;
}

static void
fastResumeWriteData( uint8_t       id,
                     const void  * data,
                     uint32_t      size,
                     uint32_t      count,
                     FILE        * file )
{
    uint32_t  datalen = size * count;

    fwrite( &id, 1, 1, file );
    fwrite( &datalen, 4, 1, file );
    fwrite( data, size, count, file );
}

void
tr_fastResumeSave( const tr_torrent_t * tor )
{
    char      path[MAX_PATH_LENGTH];
    FILE    * file;
    const int version = 1;
    uint64_t  total;

    fastResumeFileName( path, sizeof path, tor, 1 );
    file = fopen( path, "wb" );
    if( NULL == file ) {
        tr_err( "Couldn't open '%s' for writing", path );
        return;
    }
    
    /* Write format version */
    fwrite( &version, 4, 1, file );

    /* Write progress data */
    if (1) {
        int n;
        tr_time_t * mtimes;
        uint8_t * buf = malloc( FR_PROGRESS_LEN( tor ) );
        uint8_t * walk = buf;
        const tr_bitfield_t * bitfield;

        /* mtimes */
        mtimes = getMTimes( tor, &n );
        memcpy( walk, mtimes, n*sizeof(tr_time_t) );
        walk += n * sizeof(tr_time_t);

        /* completion bitfield */
        bitfield = tr_cpBlockBitfield( tor->completion );
        assert( (unsigned)FR_BLOCK_BITFIELD_LEN( tor ) == bitfield->len );
        memcpy( walk, bitfield->bits, bitfield->len );
        walk += bitfield->len;

        /* write it */
        assert( walk-buf == (int)FR_PROGRESS_LEN( tor ) );
        fastResumeWriteData( FR_ID_PROGRESS, buf, 1, walk-buf, file );

        /* cleanup */
        free( mtimes );
        free( buf );
    }


    /* Write the priorities and DND flags */
    if( TRUE )
    {
        int i;
        const int n = tor->info.fileCount;
        char * buf = tr_new0( char, n*2 );
        char * walk = buf;

        /* priorities */
        for( i=0; i<n; ++i ) {
            char ch;
            const int priority = tor->info.files[i].priority;
            switch( priority ) {
               case TR_PRI_LOW:   ch = 'l'; break; /* low */
               case TR_PRI_HIGH:  ch = 'h'; break; /* high */
               default:           ch = 'n'; break; /* normal */
            };
            *walk++ = ch;
        }

        /* dnd flags */
        for( i=0; i<n; ++i )
            *walk++ = tor->info.files[i].dnd ? 't' : 'f';

        /* write it */
        assert( walk - buf == 2*n );
        fastResumeWriteData( FR_ID_PRIORITY, buf, 1, walk-buf, file );

        /* cleanup */
        tr_free( buf );
    }


    /* Write the torrent ul/dl speed caps */
    if( TRUE )
    {
        const int len = FR_SPEED_LEN;
        char * buf = tr_new0( char, len );
        char * walk = buf;
        uint16_t i16;
        uint8_t i8;

        i16 = (uint16_t) tr_torrentGetSpeedLimit( tor, TR_DOWN );
        memcpy( walk, &i16, 2 ); walk += 2;
        i8 = (uint8_t) tr_torrentGetSpeedMode( tor, TR_DOWN );
        memcpy( walk, &i8, 1 ); walk += 1;
        i16 = (uint16_t) tr_torrentGetSpeedLimit( tor, TR_UP );
        memcpy( walk, &i16, 2 ); walk += 2;
        i8 = (uint8_t) tr_torrentGetSpeedMode( tor, TR_UP );
        memcpy( walk, &i8, 1 ); walk += 1;

        assert( walk - buf == len );
        fastResumeWriteData( FR_ID_SPEED, buf, 1, walk-buf, file );
        tr_free( buf );
    }

    if( TRUE ) /* FR_ID_RUN */
    {
        const char is_running = (tor->runStatus == TR_RUN_RUNNING) ? 't' : 'f';
        fastResumeWriteData( FR_ID_RUN, &is_running, 1, 1, file );
    }

    /* Write download and upload totals */
    total = tor->downloadedCur + tor->downloadedPrev;
    fastResumeWriteData( FR_ID_DOWNLOADED, &total, 8, 1, file );
    total = tor->uploadedCur + tor->uploadedPrev;
    fastResumeWriteData( FR_ID_UPLOADED, &total, 8, 1, file );

    if( !( TR_FLAG_PRIVATE & tor->info.flags ) )
    {
        /* Write IPs and ports of connectable peers, if any */
        int size;
        uint8_t * buf = NULL;
        if( ( size = tr_peerGetConnectable( tor, &buf ) ) > 0 )
        {
            fastResumeWriteData( FR_ID_PEERS, buf, size, 1, file );
            free( buf );
        }
    }

    fclose( file );

    tr_dbg( "Resume file '%s' written", path );
}

static int
loadSpeeds( tr_torrent_t * tor, FILE * file )
{
    const size_t len = FR_SPEED_LEN;
    char * buf = tr_new0( char, len );
    char * walk = buf;
    uint16_t i16;
    uint8_t i8;

    if( len != fread( buf, 1, len, file ) ) {
        tr_inf( "Couldn't read from resume file" );
        free( buf );
        return TR_ERROR_IO_OTHER;
    }

    memcpy( &i16, walk, 2 ); walk += 2;
    tr_torrentSetSpeedLimit( tor, TR_DOWN, i16 );
    memcpy( &i8, walk, 1 ); walk += 1;
    tr_torrentSetSpeedMode( tor, TR_DOWN, (tr_speedlimit_t)i8 );
    memcpy( &i16, walk, 2 ); walk += 2;
    tr_torrentSetSpeedLimit( tor, TR_UP, i16 );
    memcpy( &i8, walk, 1 ); walk += 1;
    tr_torrentSetSpeedMode( tor, TR_UP, (tr_speedlimit_t)i8 );

    tr_free( buf );
    return TR_OK;
}


static int
loadPriorities( tr_torrent_t * tor,
                FILE         * file )
{
    const size_t n = tor->info.fileCount;
    const size_t len = 2 * n;
    int *dnd = NULL, dndCount = 0;
    int *dl = NULL, dlCount = 0;
    char * buf = tr_new0( char, len );
    char * walk = buf;
    size_t i;

    if( len != fread( buf, 1, len, file ) ) {
        tr_inf( "Couldn't read from resume file" );
        free( buf );
        return TR_ERROR_IO_OTHER;
    }

    /* set file priorities */
    for( i=0; i<n; ++i ) {
       tr_priority_t priority;
       const char ch = *walk++;
       switch( ch ) {
           case 'l': priority = TR_PRI_LOW; break;
           case 'h': priority = TR_PRI_HIGH; break;
           default:  priority = TR_PRI_NORMAL; break;
       }
       tor->info.files[i].priority = priority;
    }

    /* set the dnd flags */
    dl = tr_new( int, len );
    dnd = tr_new( int, len );
    for( i=0; i<n; ++i )
        if( *walk++ == 't' ) /* 't' means the DND flag is true */
            dnd[dndCount++] = i;
        else
            dl[dlCount++] = i;

    if( dndCount )
        tr_torrentSetFileDLs ( tor, dnd, dndCount, FALSE );
    if( dlCount )
        tr_torrentSetFileDLs ( tor, dl, dlCount, TRUE );

    tr_free( dnd );
    tr_free( dl );
    tr_free( buf );
    return TR_OK;
}

static int
fastResumeLoadProgress( const tr_torrent_t  * tor,
                        tr_bitfield_t       * uncheckedPieces,
                        FILE                * file )
{
    int i;
    const size_t len = FR_PROGRESS_LEN( tor );
    uint8_t * buf = calloc( len, 1 );
    uint8_t * walk = buf;

    if( len != fread( buf, 1, len, file ) ) {
        tr_inf( "Couldn't read from resume file" );
        free( buf );
        return TR_ERROR_IO_OTHER;
    }

    /* compare file mtimes */
    if (1) {
        int n;
        tr_time_t * curMTimes = getMTimes( tor, &n );
        const tr_time_t * oldMTimes = (const tr_time_t *) walk;
        for( i=0; i<n; ++i ) {
            if ( curMTimes[i]!=oldMTimes[i] ) {
                const tr_file_t * file = &tor->info.files[i];
                tr_dbg( "File '%s' mtimes differ-- flagging pieces [%d..%d] for recheck",
                        file->name, file->firstPiece, file->lastPiece);
                tr_bitfieldAddRange( uncheckedPieces, 
                                     file->firstPiece, file->lastPiece+1 );
            }
        }
        free( curMTimes );
        walk += n * sizeof(tr_time_t);
    }

    /* get the completion bitfield */
    if (1) {
        tr_bitfield_t bitfield;
        memset( &bitfield, 0, sizeof bitfield );
        bitfield.len = FR_BLOCK_BITFIELD_LEN( tor );
        bitfield.bits = walk;
        tr_cpBlockBitfieldSet( tor->completion, &bitfield );
    }

    /* the files whose mtimes are wrong,
       remove from completion pending a recheck... */
    for( i=0; i<tor->info.pieceCount; ++i )
        if( tr_bitfieldHas( uncheckedPieces, i ) )
            tr_cpPieceRem( tor->completion, i );


    free( buf );
    return TR_OK;
}

static uint64_t
fastResumeLoadOld( tr_torrent_t   * tor,
                   tr_bitfield_t  * uncheckedPieces, 
                   FILE           * file )
{
    uint64_t ret = 0;

    /* Check the size */
    const int size = 4 + FR_PROGRESS_LEN( tor );
    fseek( file, 0, SEEK_END );
    if( ftell( file ) != size )
    {
        tr_inf( "Wrong size for resume file (%d bytes, %d expected)",
                (int)ftell( file ), size );
        fclose( file );
        return 1;
    }

    /* load progress information */
    fseek( file, 4, SEEK_SET );
    if( fastResumeLoadProgress( tor, uncheckedPieces, file ) )
    {
        fclose( file );
        return 1;
    }

    fclose( file );

    ret |= TR_FR_PROGRESS;
    tr_inf( "Fast resuming successful (version 0)" );

    return ret;
}

static uint64_t
fastResumeLoadImpl ( tr_torrent_t   * tor,
                     tr_bitfield_t  * uncheckedPieces )
{
    char      path[MAX_PATH_LENGTH];
    FILE    * file;
    int       version = 0;
    uint8_t   id;
    uint32_t  len;
    uint64_t  ret = 0;

    assert( tor != NULL );
    assert( uncheckedPieces != NULL );

    /* Open resume file */
    fastResumeFileName( path, sizeof path, tor, 1 );
    file = fopen( path, "rb" );
    if( !file )
    {
        if( ENOENT == errno )
        {
            fastResumeFileName( path, sizeof path, tor, 0 );
            file = fopen( path, "rb" );
            if( !file )
            {
                fastResumeFileName( path, sizeof path, tor, 1 );
                tr_inf( "Couldn't open '%s' for reading", path );
                return ret;
            }
        }
    }

    tr_dbg( "Resume file '%s' loaded", path );

    /* Check format version */
    fread( &version, 4, 1, file );
    if( 0 == version )
    {
        return fastResumeLoadOld( tor, uncheckedPieces, file );
    }
    if( 1 != version )
    {
        tr_inf( "Resume file has version %d, not supported", version );
        fclose( file );
        return ret;
    }

    /* read each block of data */
    while( 1 == fread( &id, 1, 1, file ) && 1 == fread( &len, 4, 1, file ) )
    {
        switch( id )
        {
            case FR_ID_PROGRESS:
                /* read progress data */
                if( (uint32_t)FR_PROGRESS_LEN( tor ) == len )
                {
                    const int rret = fastResumeLoadProgress( tor, uncheckedPieces, file );

                    if( rret && ( feof(file) || ferror(file) ) )
                    {
                        fclose( file );
                        return ret;
                    }

                    ret |= TR_FR_PROGRESS;
                    continue;
                }
                break;

            case FR_ID_PRIORITY:

                /* read priority data */
                if( len == (uint32_t)(2 * tor->info.fileCount) )
                {
                    const int rret = loadPriorities( tor, file );

                    if( rret && ( feof(file) || ferror(file) ) )
                    {
                        fclose( file );
                        return ret;
                    }

                    ret |= TR_FR_PRIORITY;
                    continue;
                }
                break;

            case FR_ID_SPEED:
                /*  read speed data */
                if( len == FR_SPEED_LEN )
                {
                    const int rret = loadSpeeds( tor, file );

                    if( rret && ( feof(file) || ferror(file) ) )
                    {
                        fclose( file );
                        return ret;
                    }

                    ret |= TR_FR_SPEEDLIMIT;
                    continue;
                }
                break;

            case FR_ID_RUN:
                {
                    char ch;
                    if( fread( &ch, 1, 1, file ) != 1 )
                    {
                        fclose( file );
                        return ret;
                    }
                    tor->runStatus = ch=='f' ? TR_RUN_STOPPED : TR_RUN_RUNNING;
                    ret |= TR_FR_RUN;
                    continue;
                }

            case FR_ID_DOWNLOADED:
                /* read download total */
                if( 8 == len)
                {
                    if( 1 != fread( &tor->downloadedPrev, 8, 1, file ) )
                    {
                        fclose( file );
                        return ret;
                    }
                    tor->downloadedCur = 0;
                    ret |= TR_FR_DOWNLOADED;
                    continue;
                }
                break;

            case FR_ID_UPLOADED:
                /* read upload total */
                if( 8 == len)
                {
                    if( 1 != fread( &tor->uploadedPrev, 8, 1, file ) )
                    {
                        fclose( file );
                        return ret;
                    }
                    tor->uploadedCur = 0;
                    ret |= TR_FR_UPLOADED;
                    continue;
                }
                break;

            case FR_ID_PEERS:
                if( !( TR_FLAG_PRIVATE & tor->info.flags ) )
                {
                    int used;
                    uint8_t * buf = malloc( len );
                    if( 1 != fread( buf, len, 1, file ) )
                    {
                        free( buf );
                        fclose( file );
                        return ret;
                    }
                    used = tr_torrentAddCompact( tor, TR_PEER_FROM_CACHE,
                                                 buf, len / 6 );
                    tr_dbg( "found %i peers in resume file, used %i",
                            len / 6, used );
                    free( buf );
                    ret |= TR_FR_PEERS;
                }
                continue;

            default:
                break;
        }

        /* if we didn't read the data, seek past it */
        tr_inf( "Skipping resume data type %02x, %u bytes", id, len );
        fseek( file, len, SEEK_CUR );
    }

    fclose( file );
    return ret;
}

uint64_t
tr_fastResumeLoad( tr_torrent_t   * tor,
                   tr_bitfield_t  * uncheckedPieces )
{
    const uint64_t ret = fastResumeLoadImpl( tor, uncheckedPieces );

    if( ! ( ret & TR_FR_PROGRESS ) )
        tr_bitfieldAddRange( uncheckedPieces, 0, tor->info.pieceCount );

    return ret;
}
