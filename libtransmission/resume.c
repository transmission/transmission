/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <sys/types.h> /* stat */
#include <sys/stat.h> /* stat */
#include <unistd.h> /* unlink, stat */

#include <string.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "fastresume.h"
#include "peer-mgr.h" /* pex */
#include "platform.h" /* tr_getResumeDir */
#include "resume.h"
#include "torrent.h"
#include "utils.h" /* tr_buildPath */

#define KEY_CORRUPT     "corrupt"
#define KEY_DESTINATION "destination"
#define KEY_DOWNLOADED  "downloaded"
#define KEY_MAX_PEERS   "max-peers"
#define KEY_PAUSED      "paused"
#define KEY_PEERS       "peers"
#define KEY_PRIORITY    "priority"
#define KEY_PROGRESS    "progress"
#define KEY_SPEEDLIMIT  "speed-limit"
#define KEY_UPLOADED    "uploaded"

#define KEY_SPEEDLIMIT_DOWN_SPEED "down-speed"
#define KEY_SPEEDLIMIT_DOWN_MODE  "down-mode"
#define KEY_SPEEDLIMIT_UP_SPEED   "up-speed"
#define KEY_SPEEDLIMIT_UP_MODE    "up-mode"

#define KEY_PROGRESS_MTIMES "mtimes"
#define KEY_PROGRESS_BITFIELD "bitfield"

/***
****
***/

static time_t*
getMTimes( const tr_torrent * tor, int * setme_n )
{
    int i;
    const int n = tor->info.fileCount;
    time_t * m = tr_new( time_t, n );

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
getResumeFilename( char * buf, size_t buflen, const tr_torrent * tor )
{
    const char * dir = tr_getResumeDir( tor->handle );
    char base[4096];
    snprintf( base, sizeof( base ), "%s.%16.16s.resume", tor->info.name, tor->info.hashString );
    tr_buildPath( buf, buflen, dir, base, NULL );
}

/***
****
***/

static void
savePeers( tr_benc * dict, const tr_torrent * tor )
{
    tr_pex * pex;
    const int count = tr_peerMgrGetPeers( tor->handle->peerMgr, tor->info.hash, &pex );
    if( count > 0 ) {
        tr_benc * child = tr_bencDictAdd( dict, KEY_PEERS );
        tr_bencInitStrDupLen( child, (const char*)pex, sizeof(tr_pex)*count );
        tr_free( pex );
    }
}

static uint64_t
loadPeers( tr_benc * dict, tr_torrent * tor )
{
    uint64_t ret = 0;
    tr_benc * p;

    if(( p = tr_bencDictFindType( dict, KEY_PEERS, TYPE_STR )))
    {
        int i;
        const char * str = p->val.s.s;
        const size_t len = p->val.s.i;
        const int count = len / sizeof( tr_pex );
        for( i=0; i<count; ++i ) {
            tr_pex pex;
            memcpy( &pex, str + (i*sizeof(tr_pex)), sizeof(tr_pex) );
            tr_peerMgrAddPex( tor->handle->peerMgr, tor->info.hash, TR_PEER_FROM_CACHE, &pex );
        }
        tr_tordbg( tor, "Loaded %d peers from resume file", count );
        ret = TR_FR_PEERS;
    }

    return ret;
}

static void
savePriorities( tr_benc * dict, const tr_torrent * tor )
{
    const tr_info * inf = &tor->info;
    const tr_file_index_t n = inf->fileCount;
    tr_file_index_t i;
    tr_benc * list;

    list = tr_bencDictAddList( dict, KEY_PRIORITY, tor->info.fileCount );
    for( i=0; i<n; ++i )
        tr_bencInitInt( tr_bencListAdd( list ), inf->files[i].priority );
}

static uint64_t
loadPriorities( tr_benc * dict, tr_torrent * tor )
{
    uint64_t ret = 0;
    tr_info * inf = &tor->info;
    const tr_file_index_t n = inf->fileCount;
    tr_benc * list;

    if( tr_bencDictFindList( dict, KEY_PRIORITY, &list ) && ( list->val.l.count == (int)n ) )
    {
        int64_t tmp;
        tr_file_index_t i;
        for( i=0; i<n; ++i )
            if( tr_bencGetInt( &list->val.l.vals[i], &tmp ) )
                inf->files[i].priority = tmp;
        ret = TR_FR_PRIORITY;
    }

    return ret;
}

static void
saveSpeedLimits( tr_benc * dict, const tr_torrent * tor )
{
    tr_benc * d = tr_bencDictAddDict( dict, KEY_SPEEDLIMIT, 4 );
    tr_bencDictAddInt( d, KEY_SPEEDLIMIT_DOWN_SPEED,
                       tr_torrentGetSpeedLimit( tor, TR_DOWN ) );
    tr_bencDictAddInt( d, KEY_SPEEDLIMIT_DOWN_MODE,
                       tr_torrentGetSpeedMode( tor, TR_DOWN ) );
    tr_bencDictAddInt( d, KEY_SPEEDLIMIT_UP_SPEED,
                       tr_torrentGetSpeedLimit( tor, TR_UP ) );
    tr_bencDictAddInt( d, KEY_SPEEDLIMIT_UP_MODE,
                       tr_torrentGetSpeedMode( tor, TR_UP ) );
}

static uint64_t
loadSpeedLimits( tr_benc * dict, tr_torrent * tor )
{
    uint64_t ret = 0;
    tr_benc * d;

    if( tr_bencDictFindDict( dict, KEY_SPEEDLIMIT, &d ) )
    {
        int64_t i;
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_DOWN_SPEED, &i ) )
            tr_torrentSetSpeedLimit( tor, TR_DOWN, i );
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_DOWN_MODE, &i ) )
            tr_torrentSetSpeedMode( tor, TR_DOWN, i );
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_UP_SPEED, &i ) )
            tr_torrentSetSpeedLimit( tor, TR_UP, i );
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_UP_MODE, &i ) )
            tr_torrentSetSpeedMode( tor, TR_UP, i );
        ret = TR_FR_SPEEDLIMIT;
    }

    return ret;
}

static void
saveProgress( tr_benc * dict, const tr_torrent * tor )
{
    int i;
    int n;
    time_t * mtimes;
    tr_benc * p;
    tr_benc * m;
    tr_benc * b;
    const tr_bitfield * bitfield;

    p = tr_bencDictAdd( dict, KEY_PROGRESS );
    tr_bencInitDict( p, 2 );

    /* add the mtimes */
    m = tr_bencDictAdd( p, KEY_PROGRESS_MTIMES );
    mtimes = getMTimes( tor, &n );
    tr_bencInitList( m, n );
    for( i=0; i<n; ++i ) {
        if( !tr_torrentIsFileChecked( tor, i ) )
            mtimes[i] = ~(time_t)0; /* force a recheck next time */
        tr_bencInitInt( tr_bencListAdd( m ), mtimes[i] );
    }

    /* add the bitfield */
    bitfield = tr_cpBlockBitfield( tor->completion );
    b = tr_bencDictAdd( p, KEY_PROGRESS_BITFIELD );
    tr_bencInitStrDupLen( b, (const char*)bitfield->bits, bitfield->len );

    /* cleanup */
    tr_free( mtimes );
}

static uint64_t
loadProgress( tr_benc * dict, tr_torrent * tor )
{
    uint64_t ret = 0;
    tr_benc * p;

    if( tr_bencDictFindDict( dict, KEY_PROGRESS, &p ) )
    {
        tr_benc * m;
        tr_benc * b;
        int n;
        time_t * curMTimes = getMTimes( tor, &n );

        if( tr_bencDictFindList( p, KEY_PROGRESS_MTIMES, &m )
            && ( m->val.l.count == (int64_t)tor->info.fileCount )
            && ( m->val.l.count == n ) )
        {
            int i;
            for( i=0; i<m->val.l.count; ++i ) 
            {
                int64_t tmp;
                const time_t t = tr_bencGetInt( &m->val.l.vals[i], &tmp )
                               ? tmp : ~(time_t)0;
                if( curMTimes[i] == t )
                    tr_torrentSetFileChecked( tor, i, TRUE );
                else {
                    tr_torrentSetFileChecked( tor, i, FALSE );
                    tr_tordbg( tor, "File #%d needs to be verified", i );
                }
            }
        }
        else
        {
            tr_torrentUncheck( tor );
            tr_tordbg( tor, "Torrent needs to be verified - unable to find mtimes" );
        }

        if(( b = tr_bencDictFindType( p, KEY_PROGRESS_BITFIELD, TYPE_STR )))
        {
            tr_bitfield tmp;
            tmp.len = b->val.s.i;
            tmp.bits = (uint8_t*) b->val.s.s;
            if( tr_cpBlockBitfieldSet( tor->completion, &tmp ) ) {
                tr_torrentUncheck( tor );
                tr_tordbg( tor, "Torrent needs to be verified - error loading bitfield" );
            }
        }
        else
        {
            tr_torrentUncheck( tor );
            tr_tordbg( tor, "Torrent needs to be verified - unable to find bitfield" );
        }
       
        tr_free( curMTimes );
        ret = TR_FR_PROGRESS;
    }

    return ret;
}

void
tr_torrentSaveResume( const tr_torrent * tor )
{
    tr_benc top;
    char * encoded;
    int len;

    /* populate the bencoded data */
    tr_bencInitDict( &top, 10 );
    tr_bencDictAddInt( &top, KEY_CORRUPT, tor->corruptPrev + tor->corruptCur );
    tr_bencDictAddStr( &top, KEY_DESTINATION, tor->destination );
    tr_bencDictAddInt( &top, KEY_DOWNLOADED, tor->downloadedPrev + tor->downloadedCur );
    tr_bencDictAddInt( &top, KEY_UPLOADED, tor->uploadedPrev + tor->uploadedCur );
    tr_bencDictAddInt( &top, KEY_MAX_PEERS, tor->maxConnectedPeers );
    tr_bencDictAddInt( &top, KEY_PAUSED, tor->isRunning?0:1 );
    savePeers( &top, tor );
    savePriorities( &top, tor );
    saveProgress( &top, tor );
    saveSpeedLimits( &top, tor );

    /* save the bencoded data */
    if(( encoded = tr_bencSave( &top, &len )))
    {
        char filename[MAX_PATH_LENGTH];
        FILE * fp;
        getResumeFilename( filename, sizeof( filename ), tor );
        fp = fopen( filename, "wb+" );
        fwrite( encoded, len, 1, fp );
        fclose( fp );
        tr_free( encoded );
    }

    /* cleanup */
    tr_bencFree( &top );
}

uint64_t
tr_torrentLoadResume( tr_torrent    * tor,
                      uint64_t        fieldsToLoad,
                      const tr_ctor * ctor )
{
    int64_t i;
    const char * str;
    int benc_loaded = FALSE;
    uint64_t fieldsLoaded = 0;
    uint8_t * content = NULL;
    size_t contentLen;
    char filename[MAX_PATH_LENGTH];
    tr_benc top;

    getResumeFilename( filename, sizeof( filename ), tor );

    content = tr_loadFile( filename, &contentLen );
    benc_loaded = content && !tr_bencLoad( content, contentLen, &top, NULL );
    if( !benc_loaded )
    {
        tr_free( content );
        tr_tordbg( tor, "Couldn't read \"%s\"; trying old resume file format.", filename );
        fieldsLoaded = tr_fastResumeLoad( tor, fieldsToLoad, ctor );

        if( ( fieldsLoaded != 0 ) && ( fieldsToLoad == ~(uint64_t)0 ) )
        {
            tr_torrentSaveResume( tor );
            tr_fastResumeRemove( tor );
            tr_tordbg( tor, "Migrated resume file to \"%s\"", filename );
        }

        return fieldsLoaded;
    }

    tr_tordbg( tor, "Read resume file \"%s\"", filename );

    if( tr_bencDictFindInt( &top, KEY_CORRUPT, &i ) ) {
        tor->corruptPrev = i;
        fieldsLoaded |= TR_FR_CORRUPT;
    }

    if( tr_bencDictFindStr( &top, KEY_DESTINATION, &str ) ) {
        tr_free( tor->destination );
        tor->destination = tr_strdup( str );
        fieldsLoaded |= TR_FR_DESTINATION;
    }

    if( tr_bencDictFindInt( &top, KEY_DOWNLOADED, &i ) ) {
        tor->downloadedPrev = i;
        fieldsLoaded |= TR_FR_DOWNLOADED;
    }

    if( tr_bencDictFindInt( &top, KEY_UPLOADED, &i ) ) {
        tor->uploadedPrev = i;
        fieldsLoaded |= TR_FR_UPLOADED;
    }

    if( tr_bencDictFindInt( &top, KEY_MAX_PEERS, &i ) ) {
        tor->maxConnectedPeers = i;
        fieldsLoaded |= TR_FR_MAX_PEERS;
    }

    if( tr_bencDictFindInt( &top, KEY_PAUSED, &i ) ) {
        tor->isRunning = i ? 0 : 1;
        fieldsLoaded |= TR_FR_RUN;
    }

    fieldsLoaded |= loadPeers( &top, tor );
    fieldsLoaded |= loadPriorities( &top, tor );
    fieldsLoaded |= loadProgress( &top, tor );
    fieldsLoaded |= loadSpeedLimits( &top, tor );

    tr_bencFree( &top );
    return fieldsLoaded;
}

void
tr_torrentRemoveResume( const tr_torrent * tor )
{
    char filename[MAX_PATH_LENGTH];
    getResumeFilename( filename, sizeof( filename ), tor );
    unlink( filename );
    tr_fastResumeRemove( tor );
}
