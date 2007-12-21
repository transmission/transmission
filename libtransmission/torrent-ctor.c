/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include "transmission.h"
#include "bencode.h"
#include "platform.h"
#include "trcompat.h" /* strlcpy */
#include "utils.h"

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
    tr_handle * handle;

    unsigned int isSet_metadata : 1;
    benc_val_t metadata;

    struct optional_args optionalArgs[2];
};

tr_ctor*
tr_ctorNew( tr_handle * handle )
{
    tr_ctor * ctor = tr_new0( struct tr_ctor, 1 );
    ctor->handle = handle;
    tr_ctorSetMaxConnectedPeers( ctor, TR_FALLBACK, 50 );
    tr_ctorSetMaxUnchokedPeers( ctor, TR_FALLBACK, 16 );
    tr_ctorSetPaused( ctor, TR_FALLBACK, FALSE );
    return ctor;
}

void
tr_ctorFree( tr_ctor * ctor )
{
    tr_free( ctor );
}

static void
clearMetadata( tr_ctor * ctor )
{
    if( ctor->isSet_metadata ) {
        ctor->isSet_metadata = 0;
        tr_bencFree( &ctor->metadata );
    }
}

int
tr_ctorSetMetadata( tr_ctor        * ctor,
                    const uint8_t  * metadata,
                    size_t           len )
{
    int err;
    clearMetadata( ctor );
    err = tr_bencLoad( metadata, len, &ctor->metadata, NULL );
    ctor->isSet_metadata = !err;
    return err;
}

int
tr_ctorSetMetadataFromFile( tr_ctor        * ctor,
                            const char     * filename )
{
    uint8_t * metadata;
    size_t len;
    int err;

    metadata = tr_loadFile( filename, &len );
    if( metadata && len )
        err = tr_ctorSetMetadata( ctor, metadata, len );
    else {
        clearMetadata( ctor );
        err = 1;
    }

    tr_free( metadata );
    return err;
}

int
tr_ctorSetMetadataFromHash( tr_ctor        * ctor,
                            const char     * hashString )
{
    int err = -1;
    char basename[2048];
    char filename[MAX_PATH_LENGTH];

    if( err && ( ctor->handle->tag != NULL ) ) {
        snprintf( basename, sizeof(basename), "%s-%s", hashString, ctor->handle->tag );
        tr_buildPath( filename, sizeof(filename), tr_getTorrentsDirectory(), basename );
        err = tr_ctorSetMetadataFromFile( ctor, filename );
    }

    if( err ) {
        tr_buildPath( filename, sizeof(filename), tr_getTorrentsDirectory(), hashString );
        err = tr_ctorSetMetadataFromFile( ctor, filename );
    }

    return err;
}

/***
****
***/

void
tr_ctorSetPaused( tr_ctor        * ctor,
                  tr_ctorMode      mode,
                  int              isPaused )
{
    struct optional_args * args = &ctor->optionalArgs[mode];
    args->isSet_paused = 1;
    args->isPaused = isPaused;
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
    const struct optional_args * args = &ctor->optionalArgs[mode];
    const int isSet = args->isSet_connected;
    if( isSet )
        *setmeCount = args->maxConnectedPeers;
    return isSet;
}

int
tr_ctorGetMaxUnchokedPeers( const tr_ctor  * ctor,
                            tr_ctorMode      mode,
                            uint8_t        * setmeCount )
{
    const struct optional_args * args = &ctor->optionalArgs[mode];
    const int isSet = args->isSet_unchoked;
    if( isSet )
        *setmeCount = args->maxUnchokedPeers;
    return isSet;
}

int
tr_ctorGetIsPaused( const tr_ctor  * ctor,
                    tr_ctorMode      mode,
                    int            * setmeIsPaused )
{
    const struct optional_args * args = &ctor->optionalArgs[mode];
    const int isSet = args->isSet_paused;
    if( isSet )
        *setmeIsPaused = args->isPaused;
    return isSet;
}

int
tr_ctorGetDestination( const tr_ctor  * ctor,
                       tr_ctorMode      mode,
                       const char    ** setmeDestination )
{
    const struct optional_args * args = &ctor->optionalArgs[mode];
    const int isSet = args->isSet_destination;
    if( isSet )
        *setmeDestination = args->destination;
    return isSet;
}

int
tr_ctorGetMetadata( const tr_ctor              * ctor,
                    const struct benc_val_s   ** setme )
{
    const int isSet = ctor->isSet_metadata;
    if( isSet )
        *setme = &ctor->metadata;
    return isSet;
}
