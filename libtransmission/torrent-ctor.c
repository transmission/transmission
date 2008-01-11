/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <libgen.h> /* basename */
#include "transmission.h"
#include "bencode.h"
#include "platform.h"
#include "trcompat.h" /* strlcpy */
#include "utils.h"

#define DEFAULT_MAX_UNCHOKED_PEERS 12
#define DEFAULT_MAX_CONNECTED_PEERS 50

struct optional_args
{
    unsigned int isSet_paused : 1;
    unsigned int isSet_unchoked : 1;
    unsigned int isSet_connected : 1;
    unsigned int isSet_destination : 1;

    unsigned int isPaused : 1;
    uint8_t maxUnchokedPeers;
    uint16_t maxConnectedPeers;
    char destination[MAX_PATH_LENGTH];
};

struct tr_ctor
{
    const tr_handle * handle;
    unsigned int saveInOurTorrentsDir : 1;

    unsigned int isSet_metainfo : 1;
    benc_val_t metainfo;

    struct optional_args optionalArgs[2];
};

/***
****
***/

static void
clearMetainfo( tr_ctor * ctor )
{
    if( ctor->isSet_metainfo ) {
        ctor->isSet_metainfo = 0;
        tr_bencFree( &ctor->metainfo );
    }
}

int
tr_ctorSetMetainfo( tr_ctor        * ctor,
                    const uint8_t  * metainfo,
                    size_t           len )
{
    int err;
    clearMetainfo( ctor );
    err = tr_bencLoad( metainfo, len, &ctor->metainfo, NULL );
    ctor->isSet_metainfo = !err;
    return err;
}

int
tr_ctorSetMetainfoFromFile( tr_ctor        * ctor,
                            const char     * filename )
{
    uint8_t * metainfo;
    size_t len;
    int err;

    metainfo = tr_loadFile( filename, &len );
    if( metainfo && len )
        err = tr_ctorSetMetainfo( ctor, metainfo, len );
    else {
        clearMetainfo( ctor );
        err = 1;
    }

    /* if no `name' field was set, then set it from the filename */
    if( ctor->isSet_metainfo ) {
        benc_val_t * info = tr_bencDictFindType( &ctor->metainfo, "info", TYPE_DICT );
        if( info != NULL ) {
            benc_val_t * name = tr_bencDictFindFirst( info, "name.utf-8", "name", NULL );
            if( name == NULL )
                name = tr_bencDictAdd( info, "name" );
            if( name->type!=TYPE_STR || !name->val.s.s || !*name->val.s.s ) {
                char * tmp = tr_strdup( filename );
                tr_bencInitStrDup( name, basename( tmp ) );
                tr_free( tmp );
            }
        }
    }

    tr_free( metainfo );
    return err;
}

int
tr_ctorSetMetainfoFromHash( tr_ctor        * ctor,
                            const char     * hashString )
{
    int err = -1;
    char basename[2048];
    char filename[MAX_PATH_LENGTH];

    if( err && ( ctor->handle->tag != NULL ) ) {
        snprintf( basename, sizeof(basename), "%s-%s", hashString, ctor->handle->tag );
        tr_buildPath( filename, sizeof(filename), tr_getTorrentsDirectory(), basename, NULL );
        err = tr_ctorSetMetainfoFromFile( ctor, filename );
    }

    if( err ) {
        tr_buildPath( filename, sizeof(filename), tr_getTorrentsDirectory(), hashString, NULL );
        err = tr_ctorSetMetainfoFromFile( ctor, filename );
    }

    return err;
}

/***
****
***/

void
tr_ctorSetSave( tr_ctor  * ctor,
                int        saveInOurTorrentsDir )
{
    ctor->saveInOurTorrentsDir = saveInOurTorrentsDir ? 1 : 0;
}

int
tr_ctorGetSave( const tr_ctor * ctor )
{
    return ctor && ctor->saveInOurTorrentsDir;
}

void
tr_ctorSetPaused( tr_ctor        * ctor,
                  tr_ctorMode      mode,
                  uint8_t          isPaused )
{
    struct optional_args * args = &ctor->optionalArgs[mode];
    args->isSet_paused = 1;
    args->isPaused = isPaused ? 1 : 0;
}

void
tr_ctorSetMaxUnchokedPeers( tr_ctor        * ctor,
                            tr_ctorMode      mode,
                            uint8_t          maxUnchokedPeers)
{
    struct optional_args * args = &ctor->optionalArgs[mode];
    args->isSet_unchoked = 1;
    args->maxUnchokedPeers = maxUnchokedPeers;
}

void
tr_ctorSetMaxConnectedPeers( tr_ctor        * ctor,
                             tr_ctorMode      mode,
                             uint16_t         maxConnectedPeers )
{
    struct optional_args * args = &ctor->optionalArgs[mode];
    args->isSet_connected = 1;
    args->maxConnectedPeers = maxConnectedPeers;
}

void
tr_ctorSetDestination( tr_ctor        * ctor,
                       tr_ctorMode      mode,
                       const char     * directory )
{
    struct optional_args * args = &ctor->optionalArgs[mode];
    args->isSet_destination = 1;
    strlcpy( args->destination, directory, sizeof( args->destination ) );
}

int
tr_ctorGetMaxConnectedPeers( const tr_ctor  * ctor,
                             tr_ctorMode      mode,
                             uint16_t       * setmeCount )
{
    int err = 0;
    const struct optional_args * args = &ctor->optionalArgs[mode];

    if( args->isSet_connected )
        *setmeCount = args->maxConnectedPeers;
    else
        err = 1;

    return err;
}

int
tr_ctorGetMaxUnchokedPeers( const tr_ctor  * ctor,
                            tr_ctorMode      mode,
                            uint8_t        * setmeCount )
{
    int err = 0;
    const struct optional_args * args = &ctor->optionalArgs[mode];

    if( args->isSet_unchoked )
        *setmeCount = args->maxUnchokedPeers;
    else
        err = 1;

    return err;
}

int
tr_ctorGetIsPaused( const tr_ctor  * ctor,
                    tr_ctorMode      mode,
                    uint8_t        * setmeIsPaused )
{
    int err = 0;
    const struct optional_args * args = &ctor->optionalArgs[mode];

    if( args->isSet_paused )
        *setmeIsPaused = args->isPaused ? 1 : 0;
    else
        err = 1;

    return err;
}

int
tr_ctorGetDestination( const tr_ctor  * ctor,
                       tr_ctorMode      mode,
                       const char    ** setmeDestination )
{
    int err = 0;
    const struct optional_args * args = &ctor->optionalArgs[mode];

    if( args->isSet_destination )
        *setmeDestination = args->destination;
    else
        err = 1;

    return err;
}

int
tr_ctorGetMetainfo( const tr_ctor              * ctor,
                    const struct benc_val_s   ** setme )
{
    int err = 0;

    if( ctor->isSet_metainfo )
        *setme = &ctor->metainfo;
    else
        err = 1;

    return err;
}

/***
****
***/

tr_ctor*
tr_ctorNew( const tr_handle * handle )
{
    tr_ctor * ctor = tr_new0( struct tr_ctor, 1 );
    ctor->handle = handle;
    tr_ctorSetMaxConnectedPeers( ctor, TR_FALLBACK, DEFAULT_MAX_CONNECTED_PEERS );
    tr_ctorSetMaxUnchokedPeers( ctor, TR_FALLBACK, DEFAULT_MAX_UNCHOKED_PEERS );
    tr_ctorSetPaused( ctor, TR_FALLBACK, FALSE );
    tr_ctorSetSave( ctor, TRUE );
    return ctor;
}

void
tr_ctorFree( tr_ctor * ctor )
{
    clearMetainfo( ctor );
    tr_free( ctor );
}
