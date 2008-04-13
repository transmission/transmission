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

#include <string.h>
#include <unistd.h> /* unlink */

#include "transmission.h"
#include "fastresume.h"
#include "platform.h" /* tr_getResumeDir */
#include "resume.h"
#include "torrent.h"
#include "utils.h" /* tr_buildPath */

static void
getResumeFilename( char * buf, size_t buflen, const tr_torrent * tor )
{
    const char * dir = tr_getResumeDir( tor->handle );
    char base[4096];
    snprintf( base, sizeof( base ), "%s.%10.10s.resume", tor->info.name, tor->info.hashString );
    tr_buildPath( buf, buflen, dir, base, NULL );
fprintf( stderr, "filename is [%s]\n", buf );
}

uint64_t
tr_torrentLoadResume( tr_torrent    * tor,
                      uint64_t        fieldsToLoad,
                      const tr_ctor * ctor )
{
    uint64_t fieldsLoaded = 0;
    uint8_t * content;
    size_t contentLen;
    char filename[MAX_PATH_LENGTH];

    getResumeFilename( filename, sizeof( filename ), tor );
    content = tr_loadFile( filename, &contentLen );
    if( content )
    {
        tr_free( content );
    }
    else
    {
        fieldsLoaded = tr_fastResumeLoad( tor, fieldsToLoad, ctor );
    }

    return fieldsLoaded;
}

void
tr_torrentSaveResume( const tr_torrent * tor )
{
    char filename[MAX_PATH_LENGTH];
    getResumeFilename( filename, sizeof( filename ), tor );

    /* (temporary) */
    tr_fastResumeSave( tor );
}

void
tr_torrentRemoveResume( const tr_torrent * tor )
{
    char filename[MAX_PATH_LENGTH];
    getResumeFilename( filename, sizeof( filename ), tor );
    unlink( filename );
    tr_fastResumeRemove( tor );
}
