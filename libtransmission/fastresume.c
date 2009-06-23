/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

/*
 * NOTE: THIS FILE IS DEPRECATED
 *
 *  The fastresume file format is brittle and was replaced in Transmission 1.20
 *  with the benc-formatted ".resume" files implemented in resume.[ch].
 *
 *  This older format is kept only for reading older resume files for users
 *  who upgrade from older versions of Transmission, and may be removed
 *  after more time has passed.
 */  

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
#include <stdlib.h> /* calloc */
#include <string.h> /* strcpy, memset, memcmp */

#include <sys/types.h>
#include <sys/stat.h> /* stat */
#include <unistd.h> /* unlink */

#include "transmission.h"
#include "session.h"
#include "completion.h"
#include "fastresume.h"
#include "peer-mgr.h"
#include "platform.h"
#include "resume.h" /* TR_FR_ bitwise enum */
#include "torrent.h"
#include "utils.h"

/* time_t can be 32 or 64 bits... for consistency we'll hardwire 64 */
typedef uint64_t tr_time_t;

enum
{
    /* number of bytes downloaded */
    FR_ID_DOWNLOADED = 2,

    /* number of bytes uploaded */
    FR_ID_UPLOADED = 3,

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
     * uint32_t: dl's tr_speedlimit
     * uint32_t: ul speed rate to use when the mode is single
     * uint32_t: ul's tr_speedlimit
     */
    FR_ID_SPEED = 8,

    /* active
     * char: 't' if running, 'f' if paused
     */
    FR_ID_RUN = 9,

    /* number of corrupt bytes downloaded */
    FR_ID_CORRUPT = 10,

    /* IPs and ports of connectable peers */
    FR_ID_PEERS = 11,

    /* download dir of the torrent: zero-terminated string */
    FR_ID_DOWNLOAD_DIR = 12,

    /* pex flag
     * 't' if pex is enabled, 'f' if disabled */
    FR_ID_PEX = 13,

    /* max connected peers -- uint16_t */
    FR_ID_MAX_PEERS = 14
};


/* macros for the length of various pieces of the progress data */
#define FR_MTIME_LEN( t ) \
    ( sizeof( tr_time_t ) * ( t )->info.fileCount )
#define FR_BLOCK_BITFIELD_LEN( t ) \
    ( ( ( t )->blockCount + 7u ) / 8u )
#define FR_PROGRESS_LEN( t ) \
    ( FR_MTIME_LEN( t ) + FR_BLOCK_BITFIELD_LEN( t ) )
#define FR_SPEED_LEN ( 2 * ( sizeof( uint16_t ) + sizeof( uint8_t ) ) )

static tr_time_t*
getMTimes( const tr_torrent * tor,
           int *              setme_n )
{
    int         i;
    const int   n = tor->info.fileCount;
    tr_time_t * m = calloc( n, sizeof( tr_time_t ) );

    for( i = 0; i < n; ++i )
    {
        struct stat sb;
        char * fname = tr_buildPath( tor->downloadDir, tor->info.files[i].name, NULL );
        if( !stat( fname, &sb ) && S_ISREG( sb.st_mode ) )
        {
#ifdef SYS_DARWIN
            m[i] = sb.st_mtimespec.tv_sec;
#else
            m[i] = sb.st_mtime;
#endif
        }
        tr_free( fname );
    }

    *setme_n = n;
    return m;
}

/***
****
***/

static uint64_t
internalIdToPublicBitfield( uint8_t id )
{
    uint64_t ret = 0;

    switch( id )
    {
        case FR_ID_DOWNLOADED:
            ret = TR_FR_DOWNLOADED;    break;

        case FR_ID_UPLOADED:
            ret = TR_FR_UPLOADED;      break;

        case FR_ID_PROGRESS:
            ret = TR_FR_PROGRESS;      break;

        case FR_ID_PRIORITY:
            ret = TR_FR_FILE_PRIORITIES; break;

        case FR_ID_SPEED:
            ret = TR_FR_SPEEDLIMIT;    break;

        case FR_ID_RUN:
            ret = TR_FR_RUN;           break;

        case FR_ID_CORRUPT:
            ret = TR_FR_CORRUPT;       break;

        case FR_ID_PEERS:
            ret = TR_FR_PEERS;         break;

        case FR_ID_DOWNLOAD_DIR:
            ret = TR_FR_DOWNLOAD_DIR;  break;

        case FR_ID_MAX_PEERS:
            ret = TR_FR_MAX_PEERS;     break;
    }

    return ret;
}

static void
readBytes( void *           target,
           const uint8_t ** source,
           size_t           byteCount )
{
    memcpy( target, *source, byteCount );
    *source += byteCount;
}

static uint64_t
parseDownloaded( tr_torrent *    tor,
                 const uint8_t * buf,
                 uint32_t        len )
{
    if( len != sizeof( uint64_t ) )
        return 0;
    readBytes( &tor->downloadedPrev, &buf, sizeof( uint64_t ) );
    return TR_FR_DOWNLOADED;
}

static uint64_t
parseUploaded( tr_torrent *    tor,
               const uint8_t * buf,
               uint32_t        len )
{
    if( len != sizeof( uint64_t ) )
        return 0;
    readBytes( &tor->uploadedPrev, &buf, sizeof( uint64_t ) );
    return TR_FR_UPLOADED;
}

static uint64_t
parseCorrupt( tr_torrent *    tor,
              const uint8_t * buf,
              uint32_t        len )
{
    if( len != sizeof( uint64_t ) )
        return 0;
    readBytes( &tor->corruptPrev, &buf, sizeof( uint64_t ) );
    return TR_FR_CORRUPT;
}

static uint64_t
parseConnections( tr_torrent *    tor,
                  const uint8_t * buf,
                  uint32_t        len )
{
    if( len != sizeof( uint16_t ) )
        return 0;
    readBytes( &tor->maxConnectedPeers, &buf, sizeof( uint16_t ) );
    return TR_FR_MAX_PEERS;
}

static uint64_t
parseProgress( tr_torrent *    tor,
               const uint8_t * buf,
               uint32_t        len )
{
    uint64_t ret = 0;

    if( len == FR_PROGRESS_LEN( tor ) )
    {
        int             i;
        int             n;
        tr_bitfield     bitfield;

        /* compare file mtimes */
        tr_time_t *     curMTimes = getMTimes( tor, &n );
        const uint8_t * walk = buf;
        tr_time_t       mtime;
        for( i = 0; i < n; ++i )
        {
            readBytes( &mtime, &walk, sizeof( tr_time_t ) );
            if( curMTimes[i] == mtime )
                tr_torrentSetFileChecked( tor, i, TRUE );
            else
            {
                tr_torrentSetFileChecked( tor, i, FALSE );
                tr_tordbg( tor, "Torrent needs to be verified" );
            }
        }
        free( curMTimes );

        /* get the completion bitfield */
        memset( &bitfield, 0, sizeof bitfield );
        bitfield.byteCount = FR_BLOCK_BITFIELD_LEN( tor );
        bitfield.bitCount = bitfield.byteCount * 8;
        bitfield.bits = (uint8_t*) walk;
        if( tr_cpBlockBitfieldSet( &tor->completion, &bitfield ) )
            ret = TR_FR_PROGRESS;
        else {
            tr_torrentUncheck( tor );
            tr_tordbg( tor, "Torrent needs to be verified" );
        }
    }

    /* the files whose mtimes are wrong,
       remove from completion pending a recheck... */
    {
        tr_piece_index_t i;
        for( i = 0; i < tor->info.pieceCount; ++i )
            if( !tr_torrentIsPieceChecked( tor, i ) )
                tr_cpPieceRem( &tor->completion, i );
    }

    return ret;
}

static uint64_t
parsePriorities( tr_torrent *    tor,
                 const uint8_t * buf,
                 uint32_t        len )
{
    uint64_t ret = 0;

    if( len == (uint32_t)( 2 * tor->info.fileCount ) )
    {
        const size_t     n = tor->info.fileCount;
        const uint8_t *  walk = buf;
        tr_file_index_t *dnd = NULL, dndCount = 0;
        tr_file_index_t *dl = NULL, dlCount = 0;
        size_t           i;

        len = 2 * n;

        /* set file priorities */
        for( i = 0; i < n; ++i )
        {
            tr_priority_t priority;
            const char    ch = *walk++;
            switch( ch )
            {
                case 'l':
                    priority = TR_PRI_LOW; break;

                case 'h':
                    priority = TR_PRI_HIGH; break;

                default:
                    priority = TR_PRI_NORMAL; break;
            }
            tr_torrentInitFilePriority( tor, i, priority );
        }

        /* set the dnd flags */
        dl = tr_new( tr_file_index_t, len );
        dnd = tr_new( tr_file_index_t, len );
        for( i = 0; i < n; ++i )
            if( *walk++ == 't' ) /* 't' means the DND flag is true */
                dnd[dndCount++] = i;
            else
                dl[dlCount++] = i;

        if( dndCount )
            tr_torrentInitFileDLs ( tor, dnd, dndCount, FALSE );
        if( dlCount )
            tr_torrentInitFileDLs ( tor, dl, dlCount, TRUE );

        tr_free( dnd );
        tr_free( dl );

        ret = TR_FR_FILE_PRIORITIES;
    }

    return ret;
}

static uint64_t
parseSpeedLimit( tr_torrent *    tor,
                 const uint8_t * buf,
                 uint32_t        len )
{
    uint64_t ret = 0;

    if( len == FR_SPEED_LEN )
    {
        uint8_t  i8;
        uint16_t i16;

        readBytes( &i16, &buf, sizeof( i16 ) );
        tr_torrentSetSpeedLimit( tor, TR_DOWN, i16 );
        readBytes( &i8, &buf, sizeof( i8 ) );
        /*tr_torrentSetSpeedMode( tor, TR_DOWN, (tr_speedlimit)i8 );*/
        readBytes( &i16, &buf, sizeof( i16 ) );
        tr_torrentSetSpeedLimit( tor, TR_UP, i16 );
        readBytes( &i8, &buf, sizeof( i8 ) );
        /*tr_torrentSetSpeedMode( tor, TR_UP, (tr_speedlimit)i8 );*/

        ret = TR_FR_SPEEDLIMIT;
    }

    return ret;
}

static uint64_t
parseRun( tr_torrent *    tor,
          const uint8_t * buf,
          uint32_t        len )
{
    if( len != 1 )
        return 0;
    tor->isRunning = *buf == 't';
    return TR_FR_RUN;
}

static uint64_t
parsePeers( tr_torrent *    tor,
            const uint8_t * buf,
            uint32_t        len )
{
    uint64_t ret = 0;

    if( !tor->info.isPrivate )
    {
        int       i;
        const int count = len / sizeof( tr_pex );

        for( i = 0; i < count; ++i )
        {
            tr_pex pex;
            readBytes( &pex, &buf, sizeof( tr_pex ) );
            tr_peerMgrAddPex( tor, TR_PEER_FROM_CACHE, &pex );
        }

        tr_tordbg( tor, "Loaded %d peers from resume file", count );
        ret = TR_FR_PEERS;
    }

    return ret;
}

static uint64_t
parseDownloadDir( tr_torrent *    tor,
                  const uint8_t * buf,
                  uint32_t        len )
{
    uint64_t ret = 0;

    if( buf && *buf && len )
    {
        tr_free( tor->downloadDir );
        tor->downloadDir = tr_strndup( (char*)buf, len );
        ret = TR_FR_DOWNLOAD_DIR;
    }

    return ret;
}

static uint64_t
parseVersion1( tr_torrent *    tor,
               const uint8_t * buf,
               const uint8_t * end,
               uint64_t        fieldsToLoad )
{
    uint64_t ret = 0;

    while( end - buf >= 5 )
    {
        uint8_t  id;
        uint32_t len;
        readBytes( &id, &buf, sizeof( id ) );
        readBytes( &len, &buf, sizeof( len ) );

        if( buf + len > end )
        {
            tr_torerr( tor, "Resume file seems to be corrupt.  Skipping." );
        }
        else if( fieldsToLoad &
                internalIdToPublicBitfield( id ) ) switch( id )
            {
                case FR_ID_DOWNLOADED:
                    ret |= parseDownloaded( tor, buf, len ); break;

                case FR_ID_UPLOADED:
                    ret |= parseUploaded( tor, buf, len ); break;

                case FR_ID_PROGRESS:
                    ret |= parseProgress( tor, buf, len ); break;

                case FR_ID_PRIORITY:
                    ret |= parsePriorities( tor, buf, len ); break;

                case FR_ID_SPEED:
                    ret |= parseSpeedLimit( tor, buf, len ); break;

                case FR_ID_RUN:
                    ret |= parseRun( tor, buf, len ); break;

                case FR_ID_CORRUPT:
                    ret |= parseCorrupt( tor, buf, len ); break;

                case FR_ID_PEERS:
                    ret |= parsePeers( tor, buf, len ); break;

                case FR_ID_MAX_PEERS:
                    ret |= parseConnections( tor, buf, len ); break;

                case FR_ID_DOWNLOAD_DIR:
                    ret |= parseDownloadDir( tor, buf, len ); break;

                default:
                    tr_tordbg( tor, "Skipping unknown resume code %d",
                               (int)id ); break;
            }

        buf += len;
    }

    return ret;
}

static uint8_t*
loadResumeFile( const tr_torrent * tor,
                size_t *           len )
{
    uint8_t *    ret = NULL;
    const char * cacheDir = tr_getResumeDir( tor->session );
    const char * hash = tor->info.hashString;

    if( !ret && tor->session->tag )
    {
        char * path = tr_strdup_printf( "%s" TR_PATH_DELIMITER_STR "%s-%s", cacheDir, hash, tor->session->tag );
        ret = tr_loadFile( path, len );
        tr_free( path );
    }
    if( !ret )
    {
        char * path = tr_buildPath( cacheDir, hash, NULL );
        ret = tr_loadFile( path, len );
        tr_free( path );
    }

    return ret;
}

static uint64_t
fastResumeLoadImpl( tr_torrent * tor,
                    uint64_t     fieldsToLoad )
{
    uint64_t  ret = 0;
    size_t    size = 0;
    uint8_t * buf = loadResumeFile( tor, &size );

    if( !buf )
        /* %s is the torrent name */
        tr_torinf( tor, "%s", _( "Couldn't read resume file" ) );
    else
    {
        const uint8_t * walk = buf;
        const uint8_t * end = walk + size;
        if( end - walk >= 4 )
        {
            uint32_t version;
            readBytes( &version, &walk, sizeof( version ) );
            if( version == 1 )
                ret |= parseVersion1 ( tor, walk, end, fieldsToLoad );
            else
                /* %s is the torrent name */
                tr_torinf( tor, "%s", _( "Couldn't read resume file" ) );
        }

        tr_free( buf );
    }

    return ret;
}

uint64_t
tr_fastResumeLoad( tr_torrent * tor,
                   uint64_t     fieldsToLoad )
{
    return fastResumeLoadImpl( tor, fieldsToLoad );
}

void
tr_fastResumeRemove( const tr_torrent * tor )
{
    const char * cacheDir = tr_getResumeDir( tor->session );
    const char * hash = tor->info.hashString;

    if( tor->session->tag )
    {
        char * path = tr_strdup_printf( "%s" TR_PATH_DELIMITER_STR "%s-%s", cacheDir, hash, tor->session->tag );
        unlink( path );
        tr_free( path );
    }
    else
    {
        char * path = tr_buildPath( cacheDir, hash, NULL );
        unlink( path );
        tr_free( path );
    }
}

