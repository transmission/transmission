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

#include <event.h>

#include "transmission.h"
#include "completion.h"
#include "fastresume.h"
#include "internal.h" /* tr_torrentInitFileDLs */
#include "peer-mgr.h"
#include "platform.h"
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

    /* destination of the torrent: zero-terminated string */
    FR_ID_DESTINATION = 12,

    /* pex flag
     * 't' if pex is enabled, 'f' if disabled */
    FR_ID_PEX = 13,

    /* max connected peers -- uint16_t */
    FR_ID_MAX_PEERS = 14,

    /* max unchoked peers -- uint8_t */
    FR_ID_MAX_UNCHOKED = 15
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
fastResumeFileName( char * buf, size_t buflen, const tr_torrent * tor, int tag )
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
getMTimes( const tr_torrent * tor, int * setme_n )
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
tr_fastResumeSave( const tr_torrent * tor )
{
    char      path[MAX_PATH_LENGTH];
    FILE    * file;
    const int version = 1;
    uint64_t  total;

    fastResumeFileName( path, sizeof path, tor, 1 );
    file = fopen( path, "wb+" );
    if( NULL == file ) {
        tr_err( "Couldn't open '%s' for writing", path );
        return;
    }
    
    /* Write format version */
    fwrite( &version, 4, 1, file );

    if( TRUE ) /* FR_ID_DESTINATION */
    {
        const char * d = tor->destination ? tor->destination : "";
        const int byteCount = strlen( d ) + 1;
        fastResumeWriteData( FR_ID_DESTINATION, d, 1, byteCount, file );
    }

    /* Write progress data */
    if (1) {
        int n;
        tr_time_t * mtimes;
        uint8_t * buf = malloc( FR_PROGRESS_LEN( tor ) );
        uint8_t * walk = buf;
        const tr_bitfield * bitfield;

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
        const char is_running = tor->isRunning ? 't' : 'f';
        fastResumeWriteData( FR_ID_RUN, &is_running, 1, 1, file );
    }

    /* Write download and upload totals */

    total = tor->downloadedCur + tor->downloadedPrev;
    fastResumeWriteData( FR_ID_DOWNLOADED, &total, 8, 1, file );

    total = tor->uploadedCur + tor->uploadedPrev;
    fastResumeWriteData( FR_ID_UPLOADED, &total, 8, 1, file );

    total = tor->corruptCur + tor->corruptPrev;
    fastResumeWriteData( FR_ID_CORRUPT, &total, 8, 1, file );

    fastResumeWriteData( FR_ID_MAX_PEERS,
                         &tor->maxConnectedPeers,
                         sizeof(uint16_t), 1, file );

    fastResumeWriteData( FR_ID_MAX_UNCHOKED,
                         &tor->maxUnchokedPeers,
                         sizeof(uint8_t), 1, file );

    if( !tor->info.isPrivate )
    {
        tr_pex * pex;
        const int count = tr_peerMgrGetPeers( tor->handle->peerMgr,
                                              tor->info.hash,
                                              &pex );
        if( count > 0 )
            fastResumeWriteData( FR_ID_PEERS, pex, sizeof(tr_pex), count, file );
        tr_free( pex );
    }

    fclose( file );

    tr_dbg( "Wrote resume file for '%s'", tor->info.name );
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
        case FR_ID_DOWNLOADED:     ret = TR_FR_DOWNLOADED;    break;
        case FR_ID_UPLOADED:       ret = TR_FR_UPLOADED;      break;
        case FR_ID_PROGRESS:       ret = TR_FR_PROGRESS;      break;
        case FR_ID_PRIORITY:       ret = TR_FR_PRIORITY;      break;
        case FR_ID_SPEED:          ret = TR_FR_SPEEDLIMIT;    break;
        case FR_ID_RUN:            ret = TR_FR_RUN;           break;
        case FR_ID_CORRUPT:        ret = TR_FR_CORRUPT;       break;
        case FR_ID_PEERS:          ret = TR_FR_PEERS;         break;
        case FR_ID_DESTINATION:    ret = TR_FR_DESTINATION;   break;
        case FR_ID_MAX_PEERS:      ret = TR_FR_MAX_PEERS;     break;
        case FR_ID_MAX_UNCHOKED:   ret = TR_FR_MAX_UNCHOKED;  break;
    }

    return ret;
}

static void
readBytes( void * target, const uint8_t ** source, size_t byteCount )
{
    memcpy( target, *source, byteCount );
    *source += byteCount;
}

static uint64_t
parseDownloaded( tr_torrent * tor, const uint8_t * buf, uint32_t len )
{
    if( len != sizeof(uint64_t) )
        return 0;
    readBytes( &tor->downloadedPrev, &buf, sizeof(uint64_t) );
    return TR_FR_DOWNLOADED;
}

static uint64_t
parseUploaded( tr_torrent * tor, const uint8_t * buf, uint32_t len )
{
    if( len != sizeof(uint64_t) )
        return 0;
    readBytes( &tor->uploadedPrev, &buf, sizeof(uint64_t) );
    return TR_FR_UPLOADED;
}

static uint64_t
parseCorrupt( tr_torrent * tor, const uint8_t * buf, uint32_t len )
{
    if( len != sizeof(uint64_t) )
        return 0;
    readBytes( &tor->corruptPrev, &buf, sizeof(uint64_t) );
    return TR_FR_CORRUPT;
}

static uint64_t
parseConnections( tr_torrent * tor, const uint8_t * buf, uint32_t len )
{
    if( len != sizeof(uint16_t) )
        return 0;
    readBytes( &tor->maxConnectedPeers, &buf, sizeof(uint16_t) );
    return TR_FR_MAX_PEERS;
}

static uint64_t
parseUnchoked( tr_torrent * tor, const uint8_t * buf, uint32_t len )
{
    if( len != sizeof(uint8_t) )
        return 0;
    readBytes( &tor->maxUnchokedPeers, &buf, sizeof(uint8_t) );
    return TR_FR_MAX_UNCHOKED;
}

static uint64_t
parseProgress( const tr_torrent  * tor,
               const uint8_t     * buf,
               uint32_t            len,
               tr_bitfield       * uncheckedPieces )
{
    int i;
    uint64_t ret = 0;
   
    if( len == FR_PROGRESS_LEN( tor ) )
    {
        int n;
        tr_bitfield bitfield;

        /* compare file mtimes */
        tr_time_t * curMTimes = getMTimes( tor, &n );
        const uint8_t * walk = buf;
        const tr_time_t * oldMTimes = (const tr_time_t *) walk;
        for( i=0; i<n; ++i ) {
            if ( !curMTimes[i] || ( curMTimes[i] != oldMTimes[i] ) ) {
                const tr_file * file = &tor->info.files[i];
                tr_dbg( "File '%s' mtimes differ-- flagging pieces [%d..%d] for recheck",
                        file->name, file->firstPiece, file->lastPiece);
                tr_bitfieldAddRange( uncheckedPieces, 
                                     file->firstPiece, file->lastPiece+1 );
            }
        }
        free( curMTimes );
        walk += n * sizeof(tr_time_t);

        /* get the completion bitfield */
        memset( &bitfield, 0, sizeof bitfield );
        bitfield.len = FR_BLOCK_BITFIELD_LEN( tor );
        bitfield.bits = (uint8_t*) walk;
        tr_cpBlockBitfieldSet( tor->completion, &bitfield );

        ret = TR_FR_PROGRESS;
    }

    /* the files whose mtimes are wrong,
       remove from completion pending a recheck... */
    for( i=0; i<tor->info.pieceCount; ++i )
        if( tr_bitfieldHas( uncheckedPieces, i ) )
            tr_cpPieceRem( tor->completion, i );

    return ret;
}

static uint64_t
parsePriorities( tr_torrent * tor, const uint8_t * buf, uint32_t len )
{
    uint64_t ret = 0;

    if( len == (uint32_t)(2 * tor->info.fileCount) )
    {
        const size_t n = tor->info.fileCount;
        const size_t len = 2 * n;
        int *dnd = NULL, dndCount = 0;
        int *dl = NULL, dlCount = 0;
        size_t i;
        const uint8_t * walk = buf;

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
            tr_torrentInitFileDLs ( tor, dnd, dndCount, FALSE );
        if( dlCount )
            tr_torrentInitFileDLs ( tor, dl, dlCount, TRUE );

        tr_free( dnd );
        tr_free( dl );

        ret = TR_FR_PRIORITY;
    }

    return ret;
}

static uint64_t
parseSpeedLimit( tr_torrent * tor, const uint8_t * buf, uint32_t len )
{
    uint64_t ret = 0;

    if( len == FR_SPEED_LEN )
    {
        uint8_t i8;
        uint16_t i16;

        readBytes( &i16, &buf, sizeof(i16) );
        tr_torrentSetSpeedLimit( tor, TR_DOWN, i16 );
        readBytes( &i8, &buf, sizeof(i8) );
        tr_torrentSetSpeedMode( tor, TR_DOWN, (tr_speedlimit)i8 );
        readBytes( &i16, &buf, sizeof(i16) );
        tr_torrentSetSpeedLimit( tor, TR_UP, i16 );
        readBytes( &i8, &buf, sizeof(i8) );
        tr_torrentSetSpeedMode( tor, TR_UP, (tr_speedlimit)i8 );

        ret = TR_FR_SPEEDLIMIT;
    }

    return ret;
}

static uint64_t
parseRun( tr_torrent * tor, const uint8_t * buf, uint32_t len )
{
    if( len != 1 )
        return 0;
    tor->isRunning = *buf=='t';
    return TR_FR_RUN;
}

static uint64_t
parsePeers( tr_torrent * tor, const uint8_t * buf, uint32_t len )
{
    uint64_t ret = 0;

    if( !tor->info.isPrivate )
    {
        const int count = len / sizeof(tr_pex);
        tr_peerMgrAddPex( tor->handle->peerMgr,
                          tor->info.hash,
                          TR_PEER_FROM_CACHE,
                          (tr_pex*)buf, count );
        tr_dbg( "found %i peers in resume file", count );
        ret = TR_FR_PEERS;
    }

    return ret;
}

static uint64_t
parseDestination( tr_torrent * tor, const uint8_t * buf, uint32_t len )
{
    uint64_t ret = 0;

    if( buf && *buf && len ) {
        tr_free( tor->destination );
        tor->destination = tr_strndup( (char*)buf, len );
        ret = TR_FR_DESTINATION;
    }

    return ret;
}

static uint64_t
parseVersion1( tr_torrent * tor, const uint8_t * buf, const uint8_t * end,
               uint64_t fieldsToLoad,
               tr_bitfield  * uncheckedPieces )
{
    uint64_t ret = 0;

    while( end-buf >= 5 )
    {
        uint8_t id;
        uint32_t len;
        readBytes( &id, &buf, sizeof(id) );
        readBytes( &len, &buf, sizeof(len) );

        if( fieldsToLoad & internalIdToPublicBitfield( id ) ) switch( id )
        {
            case FR_ID_DOWNLOADED:   ret |= parseDownloaded( tor, buf, len ); break;
            case FR_ID_UPLOADED:     ret |= parseUploaded( tor, buf, len ); break;
            case FR_ID_PROGRESS:     ret |= parseProgress( tor, buf, len, uncheckedPieces ); break;
            case FR_ID_PRIORITY:     ret |= parsePriorities( tor, buf, len ); break;
            case FR_ID_SPEED:        ret |= parseSpeedLimit( tor, buf, len ); break;
            case FR_ID_RUN:          ret |= parseRun( tor, buf, len ); break;
            case FR_ID_CORRUPT:      ret |= parseCorrupt( tor, buf, len ); break;
            case FR_ID_PEERS:        ret |= parsePeers( tor, buf, len ); break;
            case FR_ID_MAX_PEERS:    ret |= parseConnections( tor, buf, len ); break;
            case FR_ID_MAX_UNCHOKED: ret |= parseUnchoked( tor, buf, len ); break;
            case FR_ID_DESTINATION:  ret |= parseDestination( tor, buf, len ); break;
            default:                 tr_dbg( "Skipping unknown resume code %d", (int)id ); break;
        }

        buf += len;
    }

    return ret;
}

static uint8_t* 
loadResumeFile( const tr_torrent * tor, size_t * len )
{
    uint8_t * ret = NULL;
    char path[MAX_PATH_LENGTH];
    const char * cacheDir = tr_getCacheDirectory ();
    const char * hash = tor->info.hashString;

    if( !ret && tor->handle->tag )
    {
        char base[1024];
        snprintf( base, sizeof(base), "%s-%s", hash, tor->handle->tag );
        tr_buildPath( path, sizeof(path), cacheDir, base, NULL );
        ret = tr_loadFile( path, len );
    }

    if( !ret )
    {
        tr_buildPath( path, sizeof(path), cacheDir, hash, NULL );
        ret = tr_loadFile( path, len );
    }

    return ret;
}

static uint64_t
fastResumeLoadImpl ( tr_torrent   * tor,
                     uint64_t       fieldsToLoad,
                     tr_bitfield  * uncheckedPieces )
{
    uint64_t ret = 0;
    size_t size = 0;
    uint8_t * buf = loadResumeFile( tor, &size );

    if( !buf )
        tr_inf( "Couldn't read resume file for '%s'", tor->info.name );
    else {
        const uint8_t * walk = buf;
        const uint8_t * end = walk + size;
        if( end - walk >= 4 ) {
            uint32_t version;
            readBytes( &version, &walk, sizeof(version) );
            if( version == 1 )
                ret |= parseVersion1 ( tor, walk, end, fieldsToLoad, uncheckedPieces );
            else
                tr_inf( "Unsupported resume file %d for '%s'", version, tor->info.name );
        }

        tr_free( buf );
    }

    return ret;
}

static uint64_t
setFromCtor( tr_torrent * tor, uint64_t fields, const tr_ctor * ctor, int mode )
{
    uint64_t ret = 0;

    if( fields & TR_FR_MAX_UNCHOKED )
        if( !tr_ctorGetMaxUnchokedPeers( ctor, mode, &tor->maxUnchokedPeers ) )
            ret |= TR_FR_MAX_UNCHOKED;

    if( fields & TR_FR_MAX_PEERS ) 
        if( !tr_ctorGetMaxConnectedPeers( ctor, mode, &tor->maxConnectedPeers ) )
            ret |= TR_FR_MAX_PEERS;

    if( fields & TR_FR_DESTINATION ) {
        const char * destination;
        if( !tr_ctorGetDestination( ctor, mode, &destination ) ) {
            ret |= TR_FR_DESTINATION;
            tr_free( tor->destination );
            tor->destination = tr_strdup( destination );
        }
    }

    return ret;
}

static uint64_t
useManditoryFields( tr_torrent * tor, uint64_t fields, const tr_ctor * ctor )
{
    return setFromCtor( tor, fields, ctor, TR_FORCE );
}

static uint64_t
useFallbackFields( tr_torrent * tor, uint64_t fields, const tr_ctor * ctor )
{
    return setFromCtor( tor, fields, ctor, TR_FALLBACK );
}

uint64_t
tr_fastResumeLoad( tr_torrent     * tor,
                   uint64_t         fieldsToLoad,
                   tr_bitfield    * uncheckedPieces,
                   const tr_ctor  * ctor )
{
    uint64_t ret = 0;

    ret |= useManditoryFields( tor, fieldsToLoad, ctor );
    fieldsToLoad &= ~ret;
    ret |= fastResumeLoadImpl( tor, fieldsToLoad, uncheckedPieces );
    fieldsToLoad &= ~ret;
    ret |= useFallbackFields( tor, fieldsToLoad, ctor );

    return ret;
}
