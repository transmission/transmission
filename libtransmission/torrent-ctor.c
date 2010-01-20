/*
 * This file Copyright (C) 2007-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <errno.h> /* EINVAL */
#include "transmission.h"
#include "bencode.h"
#include "magnet.h"
#include "platform.h"
#include "session.h" /* tr_sessionFindTorrentFile() */
#include "torrent.h" /* tr_ctorGetSave() */
#include "utils.h" /* tr_new0 */

struct optional_args
{
    tr_bool         isSet_paused;
    tr_bool         isSet_connected;
    tr_bool         isSet_downloadDir;

    tr_bool         isPaused;
    uint16_t        peerLimit;
    char          * downloadDir;
};

/** Opaque class used when instantiating torrents.
 * @ingroup tr_ctor */
struct tr_ctor
{
    const tr_session *      session;
    tr_bool                 saveInOurTorrentsDir;
    tr_bool                 doDelete;

    tr_bool                 isSet_metainfo;
    tr_bool                 isSet_delete;
    tr_benc                 metainfo;
    char *                  sourceFile;

    tr_magnet_info        * magnetInfo;

    struct optional_args    optionalArgs[2];

    char                  * incompleteDir;

    tr_file_index_t       * want;
    tr_file_index_t         wantSize;
    tr_file_index_t       * notWant;
    tr_file_index_t         notWantSize;
    tr_file_index_t       * low;
    tr_file_index_t         lowSize;
    tr_file_index_t       * normal;
    tr_file_index_t         normalSize;
    tr_file_index_t       * high;
    tr_file_index_t         highSize;
};

/***
****
***/

static void
setSourceFile( tr_ctor *    ctor,
               const char * sourceFile )
{
    tr_free( ctor->sourceFile );
    ctor->sourceFile = tr_strdup( sourceFile );
}

static void
clearMetainfo( tr_ctor * ctor )
{
    if( ctor->isSet_metainfo )
    {
        ctor->isSet_metainfo = 0;
        tr_bencFree( &ctor->metainfo );
    }

    setSourceFile( ctor, NULL );
}

int
tr_ctorSetMetainfo( tr_ctor *       ctor,
                    const uint8_t * metainfo,
                    size_t          len )
{
    int err;

    clearMetainfo( ctor );
    err = tr_bencLoad( metainfo, len, &ctor->metainfo, NULL );
    ctor->isSet_metainfo = !err;
    return err;
}

const char*
tr_ctorGetSourceFile( const tr_ctor * ctor )
{
    return ctor->sourceFile;
}

int
tr_ctorSetMagnet( tr_ctor * ctor, const char * uri )
{
    int err;

    if( ctor->magnetInfo != NULL )
        tr_magnetFree( ctor->magnetInfo );

    ctor->magnetInfo = tr_magnetParse( uri );

    err = ctor->magnetInfo == NULL;
    return err;
}

int
tr_ctorSetMetainfoFromFile( tr_ctor *    ctor,
                            const char * filename )
{
    uint8_t * metainfo;
    size_t    len;
    int       err;

    metainfo = tr_loadFile( filename, &len );
    if( metainfo && len )
        err = tr_ctorSetMetainfo( ctor, metainfo, len );
    else
    {
        clearMetainfo( ctor );
        err = 1;
    }

    setSourceFile( ctor, filename );

    /* if no `name' field was set, then set it from the filename */
    if( ctor->isSet_metainfo )
    {
        tr_benc * info;
        if( tr_bencDictFindDict( &ctor->metainfo, "info", &info ) )
        {
            const char * name;
            if( !tr_bencDictFindStr( info, "name.utf-8", &name ) )
                if( !tr_bencDictFindStr( info, "name", &name ) )
                    name = NULL;
            if( !name || !*name )
            {
                char * base = tr_basename( filename );
                tr_bencDictAddStr( info, "name", base );
                tr_free( base );
            }
        }
    }

    tr_free( metainfo );
    return err;
}

int
tr_ctorSetMetainfoFromHash( tr_ctor *    ctor,
                            const char * hashString )
{
    int          err;
    const char * filename;

    filename = tr_sessionFindTorrentFile( ctor->session, hashString );
    if( !filename )
        err = EINVAL;
    else
        err = tr_ctorSetMetainfoFromFile( ctor, filename );

    return err;
}

/***
****
***/

void
tr_ctorSetFilePriorities( tr_ctor                * ctor,
                          const tr_file_index_t  * files,
                          tr_file_index_t          fileCount,
                          tr_priority_t            priority )
{
    tr_file_index_t ** myfiles;
    tr_file_index_t * mycount;

    switch( priority ) {
        case TR_PRI_LOW: myfiles = &ctor->low; mycount = &ctor->lowSize; break;
        case TR_PRI_HIGH: myfiles = &ctor->high; mycount = &ctor->highSize; break;
        default /*TR_PRI_NORMAL*/: myfiles = &ctor->normal; mycount = &ctor->normalSize; break;
    }

    tr_free( *myfiles );
    *myfiles = tr_memdup( files, sizeof(tr_file_index_t)*fileCount );
    *mycount = fileCount;
}

void
tr_ctorInitTorrentPriorities( const tr_ctor * ctor, tr_torrent * tor )
{
    tr_file_index_t i;

    for( i=0; i<ctor->lowSize; ++i )
        tr_torrentInitFilePriority( tor, ctor->low[i], TR_PRI_LOW );
    for( i=0; i<ctor->normalSize; ++i )
        tr_torrentInitFilePriority( tor, ctor->normal[i], TR_PRI_NORMAL );
    for( i=0; i<ctor->highSize; ++i )
        tr_torrentInitFilePriority( tor, ctor->high[i], TR_PRI_HIGH );
}

void
tr_ctorSetFilesWanted( tr_ctor                * ctor,
                       const tr_file_index_t  * files,
                       tr_file_index_t          fileCount,
                       tr_bool                  wanted )
{
    tr_file_index_t ** myfiles = wanted ? &ctor->want : &ctor->notWant;
    tr_file_index_t * mycount = wanted ? &ctor->wantSize : &ctor->notWantSize;

    tr_free( *myfiles );
    *myfiles = tr_memdup( files, sizeof(tr_file_index_t)*fileCount );
    *mycount = fileCount;
}

void
tr_ctorInitTorrentWanted( const tr_ctor * ctor, tr_torrent * tor )
{
    if( ctor->notWantSize )
        tr_torrentInitFileDLs( tor, ctor->notWant, ctor->notWantSize, FALSE );
    if( ctor->wantSize )
        tr_torrentInitFileDLs( tor, ctor->want, ctor->wantSize, TRUE );
}

/***
****
***/

void
tr_ctorSetDeleteSource( tr_ctor * ctor,
                        tr_bool   deleteSource )
{
    ctor->doDelete = deleteSource != 0;
    ctor->isSet_delete = 1;
}

int
tr_ctorGetDeleteSource( const tr_ctor * ctor,
                        uint8_t *       setme )
{
    int err = 0;

    if( !ctor->isSet_delete )
        err = 1;
    else if( setme )
        *setme = ctor->doDelete ? 1 : 0;

    return err;
}

/***
****
***/

void
tr_ctorSetSave( tr_ctor * ctor,
                tr_bool   saveInOurTorrentsDir )
{
    ctor->saveInOurTorrentsDir = saveInOurTorrentsDir != 0;
}

int
tr_ctorGetSave( const tr_ctor * ctor )
{
    return ctor && ctor->saveInOurTorrentsDir;
}

void
tr_ctorSetPaused( tr_ctor *   ctor,
                  tr_ctorMode mode,
                  tr_bool     isPaused )
{
    struct optional_args * args = &ctor->optionalArgs[mode];

    args->isSet_paused = 1;
    args->isPaused = isPaused ? 1 : 0;
}

void
tr_ctorSetPeerLimit( tr_ctor *   ctor,
                     tr_ctorMode mode,
                     uint16_t    peerLimit )
{
    struct optional_args * args = &ctor->optionalArgs[mode];

    args->isSet_connected = 1;
    args->peerLimit = peerLimit;
}

void
tr_ctorSetDownloadDir( tr_ctor *    ctor,
                       tr_ctorMode  mode,
                       const char * directory )
{
    struct optional_args * args = &ctor->optionalArgs[mode];

    tr_free( args->downloadDir );
    args->downloadDir = NULL;
    args->isSet_downloadDir = 0;

    if( directory )
    {
        args->isSet_downloadDir = 1;
        args->downloadDir = tr_strdup( directory );
    }
}

void
tr_ctorSetIncompleteDir( tr_ctor * ctor, const char * directory )
{
    tr_free( ctor->incompleteDir );
    ctor->incompleteDir = tr_strdup( directory );
}

int
tr_ctorGetPeerLimit( const tr_ctor * ctor,
                     tr_ctorMode     mode,
                     uint16_t *      setmeCount )
{
    int err = 0;
    const struct optional_args * args = &ctor->optionalArgs[mode];

    if( !args->isSet_connected )
        err = 1;
    else if( setmeCount )
        *setmeCount = args->peerLimit;

    return err;
}

int
tr_ctorGetPaused( const tr_ctor * ctor,
                  tr_ctorMode     mode,
                  uint8_t *       setmeIsPaused )
{
    int                          err = 0;
    const struct optional_args * args = &ctor->optionalArgs[mode];

    if( !args->isSet_paused )
        err = 1;
    else if( setmeIsPaused )
        *setmeIsPaused = args->isPaused ? 1 : 0;

    return err;
}

int
tr_ctorGetDownloadDir( const tr_ctor * ctor,
                       tr_ctorMode     mode,
                       const char **   setmeDownloadDir )
{
    int                          err = 0;
    const struct optional_args * args = &ctor->optionalArgs[mode];

    if( !args->isSet_downloadDir )
        err = 1;
    else if( setmeDownloadDir )
        *setmeDownloadDir = args->downloadDir;

    return err;
}

int
tr_ctorGetIncompleteDir( const tr_ctor  * ctor,
                         const char    ** setmeIncompleteDir )
{
    int err = 0;

    if( ctor->incompleteDir == NULL )
        err = 1;
    else
        *setmeIncompleteDir = ctor->incompleteDir;

    return err;
}

int
tr_ctorGetMagnet( const tr_ctor * ctor, const tr_magnet_info ** setme )
{
    int err = 0;

    if( ctor->magnetInfo == NULL )
        err = 1;
    else
        *setme = ctor->magnetInfo;

    return err;
}

int
tr_ctorGetMetainfo( const tr_ctor *  ctor,
                    const tr_benc ** setme )
{
    int err = 0;

    if( !ctor->isSet_metainfo )
        err = 1;
    else if( setme )
        *setme = &ctor->metainfo;

    return err;
}

tr_session*
tr_ctorGetSession( const tr_ctor * ctor )
{
    return (tr_session*) ctor->session;
}

/***
****
***/

tr_ctor*
tr_ctorNew( const tr_session * session )
{
    tr_ctor * ctor = tr_new0( struct tr_ctor, 1 );

    ctor->session = session;
    tr_ctorSetPaused( ctor, TR_FALLBACK, FALSE );
    if( session != NULL ) {
        tr_ctorSetPeerLimit( ctor, TR_FALLBACK, session->peerLimitPerTorrent );
        tr_ctorSetDownloadDir( ctor, TR_FALLBACK, session->downloadDir );
    }
    tr_ctorSetSave( ctor, TRUE );
    return ctor;
}

void
tr_ctorFree( tr_ctor * ctor )
{
    clearMetainfo( ctor );
    tr_free( ctor->optionalArgs[1].downloadDir );
    tr_free( ctor->optionalArgs[0].downloadDir );
    tr_free( ctor->incompleteDir );
    tr_free( ctor->want );
    tr_free( ctor->notWant );
    tr_free( ctor->low );
    tr_free( ctor->high );
    tr_free( ctor->normal );
    tr_free( ctor );
}
