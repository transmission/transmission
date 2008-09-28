/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h> /* FILE, stderr */
#include <stdlib.h> /* qsort */
#include <string.h> /* strcmp, strlen */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h> /* dirname, basename */
#include <dirent.h>

#include "crypto.h" /* tr_sha1 */
#include "transmission.h"
#include "bencode.h"
#include "makemeta.h"
#include "platform.h" /* threads, locks */
#include "utils.h" /* buildpath */
#include "version.h"

/****
*****
****/

struct FileList
{
    struct FileList *  next;
    uint64_t           size;
    char *             filename;
};

static struct FileList*
getFiles( const char *      dir,
          const char *      base,
          struct FileList * list )
{
    int         i;
    char        buf[MAX_PATH_LENGTH];
    struct stat sb;
    DIR *       odir = NULL;

    sb.st_size = 0;

    tr_buildPath( buf, sizeof( buf ), dir, base, NULL );
    i = stat( buf, &sb );
    if( i )
    {
        tr_err( _(
                   "Torrent Creator is skipping file \"%s\": %s" ), buf,
               tr_strerror( errno ) );
        return list;
    }

    if( S_ISDIR( sb.st_mode ) && ( ( odir = opendir ( buf ) ) ) )
    {
        struct dirent *d;
        for( d = readdir( odir ); d != NULL; d = readdir( odir ) )
            if( d->d_name && d->d_name[0] != '.' ) /* skip dotfiles */
                list = getFiles( buf, d->d_name, list );
        closedir( odir );
    }
    else if( S_ISREG( sb.st_mode ) && ( sb.st_size > 0 ) )
    {
        struct FileList * node = tr_new( struct FileList, 1 );
        node->size = sb.st_size;
        if( ( buf[0] == '.' ) && ( buf[1] == '/' ) )
            node->filename = tr_strdup( buf + 2 );
        else
            node->filename = tr_strdup( buf );
        node->next = list;
        list = node;
    }

    return list;
}

static int
bestPieceSize( uint64_t totalSize )
{
    static const uint64_t GiB = 1073741824;
    static const uint64_t MiB = 1048576;
    static const uint64_t KiB = 1024;

    if( totalSize >=   ( 2 * GiB ) ) return 2 * MiB;
    if( totalSize >=   ( 1 * GiB ) ) return 1 * MiB;
    if( totalSize >= ( 512 * MiB ) ) return 512 * KiB;
    if( totalSize >= ( 350 * MiB ) ) return 256 * KiB;
    if( totalSize >= ( 150 * MiB ) ) return 128 * KiB;
    if( totalSize >=  ( 50 * MiB ) ) return 64 * KiB;
    return 32 * KiB;  /* less than 50 meg */
}

static int
builderFileCompare( const void * va,
                    const void * vb )
{
    const tr_metainfo_builder_file * a = va;
    const tr_metainfo_builder_file * b = vb;

    return strcmp( a->filename, b->filename );
}

tr_metainfo_builder*
tr_metaInfoBuilderCreate( tr_handle *  handle,
                          const char * topFile )
{
    int                   i;
    struct FileList *     files;
    struct FileList *     walk;
    tr_metainfo_builder * ret = tr_new0( tr_metainfo_builder, 1 );

    ret->top = tr_strdup( topFile );
    ret->handle = handle;
    {
        struct stat sb;
        stat( topFile, &sb );
        ret->isSingleFile = !S_ISDIR( sb.st_mode );
    }

    /* build a list of files containing topFile and,
       if it's a directory, all of its children */
    {
        char *dir, *base;
        char  dirbuf[MAX_PATH_LENGTH];
        char  basebuf[MAX_PATH_LENGTH];
        tr_strlcpy( dirbuf, topFile, sizeof( dirbuf ) );
        tr_strlcpy( basebuf, topFile, sizeof( basebuf ) );
        dir = dirname( dirbuf );
        base = basename( basebuf );
        files = getFiles( dir, base, NULL );
    }

    for( walk = files; walk != NULL; walk = walk->next )
        ++ret->fileCount;

    ret->files = tr_new0( tr_metainfo_builder_file, ret->fileCount );

    for( i = 0, walk = files; walk != NULL; ++i )
    {
        struct FileList *          tmp = walk;
        tr_metainfo_builder_file * file = &ret->files[i];
        walk = walk->next;
        file->filename = tmp->filename;
        file->size = tmp->size;
        ret->totalSize += tmp->size;
        tr_free( tmp );
    }

    qsort( ret->files,
           ret->fileCount,
           sizeof( tr_metainfo_builder_file ),
           builderFileCompare );

    ret->pieceSize = bestPieceSize( ret->totalSize );
    ret->pieceCount = ret->pieceSize
                      ? (int)( ret->totalSize / ret->pieceSize )
                      : 0;
    if( ret->totalSize % ret->pieceSize )
        ++ret->pieceCount;

    return ret;
}

void
tr_metaInfoBuilderFree( tr_metainfo_builder * builder )
{
    if( builder )
    {
        tr_file_index_t t;
        int             i;
        for( t = 0; t < builder->fileCount; ++t )
            tr_free( builder->files[t].filename );
        tr_free( builder->files );
        tr_free( builder->top );
        tr_free( builder->comment );
        for( i = 0; i < builder->trackerCount; ++i )
            tr_free( builder->trackers[i].announce );
        tr_free( builder->trackers );
        tr_free( builder->outputFile );
        tr_free( builder );
    }
}

/****
*****
****/

static uint8_t*
getHashInfo( tr_metainfo_builder * b )
{
    uint32_t fileIndex = 0;
    uint8_t *ret = tr_new0( uint8_t, SHA_DIGEST_LENGTH * b->pieceCount );
    uint8_t *walk = ret;
    uint8_t *buf;
    uint64_t totalRemain;
    uint64_t off = 0;
    FILE *   fp;

    if( !b->totalSize )
        return ret;

    buf = tr_new( uint8_t, b->pieceSize );
    b->pieceIndex = 0;
    totalRemain = b->totalSize;
    fp = fopen( b->files[fileIndex].filename, "rb" );
    if( !fp )
    {
        b->my_errno = errno;
        tr_snprintf( b->errfile, sizeof( b->errfile ),
                     b->files[fileIndex].filename );
        b->result = TR_MAKEMETA_IO_READ;
        tr_free( buf );
        tr_free( ret );
        return NULL;
    }
    while( totalRemain )
    {
        uint8_t *      bufptr = buf;
        const uint64_t thisPieceSize =
            MIN( (uint32_t)b->pieceSize, totalRemain );
        uint64_t       pieceRemain = thisPieceSize;

        assert( b->pieceIndex < b->pieceCount );

        while( pieceRemain )
        {
            const uint64_t n_this_pass =
                MIN( ( b->files[fileIndex].size - off ), pieceRemain );
            fread( bufptr, 1, n_this_pass, fp );
            bufptr += n_this_pass;
            off += n_this_pass;
            pieceRemain -= n_this_pass;
            if( off == b->files[fileIndex].size )
            {
                off = 0;
                fclose( fp );
                fp = NULL;
                if( ++fileIndex < b->fileCount )
                {
                    fp = fopen( b->files[fileIndex].filename, "rb" );
                    if( !fp )
                    {
                        b->my_errno = errno;
                        tr_snprintf( b->errfile, sizeof( b->errfile ),
                                     b->files[fileIndex].filename );
                        b->result = TR_MAKEMETA_IO_READ;
                        tr_free( buf );
                        tr_free( ret );
                        return NULL;
                    }
                }
            }
        }

        assert( bufptr - buf == (int)thisPieceSize );
        assert( pieceRemain == 0 );
        tr_sha1( walk, buf, thisPieceSize, NULL );
        walk += SHA_DIGEST_LENGTH;

        if( b->abortFlag )
        {
            b->result = TR_MAKEMETA_CANCELLED;
            break;
        }

        totalRemain -= thisPieceSize;
        ++b->pieceIndex;
    }

    assert( b->abortFlag
          || ( walk - ret == (int)( SHA_DIGEST_LENGTH * b->pieceCount ) ) );
    assert( b->abortFlag || !totalRemain );

    if( fp )
        fclose( fp );

    tr_free( buf );
    return ret;
}

static void
getFileInfo( const char *                     topFile,
             const tr_metainfo_builder_file * file,
             tr_benc *                        uninitialized_length,
             tr_benc *                        uninitialized_path )
{
    const char * pch, *prev;
    const size_t topLen = strlen( topFile ) + 1; /* +1 for '/' */
    int          n;

    /* get the file size */
    tr_bencInitInt( uninitialized_length, file->size );

    /* the path list */
    n = 1;
    for( pch = file->filename + topLen; *pch; ++pch )
        if( *pch == TR_PATH_DELIMITER )
            ++n;
    tr_bencInitList( uninitialized_path, n );
    for( prev = pch = file->filename + topLen; ; ++pch )
    {
        char buf[MAX_PATH_LENGTH];

        if( *pch && *pch != TR_PATH_DELIMITER )
            continue;

        memcpy( buf, prev, pch - prev );
        buf[pch - prev] = '\0';

        tr_bencListAddStr( uninitialized_path, buf );

        prev = pch + 1;
        if( !*pch )
            break;
    }
}

static void
makeInfoDict( tr_benc *             dict,
              tr_metainfo_builder * builder )
{
    uint8_t * pch;
    char      base[MAX_PATH_LENGTH];

    tr_bencDictReserve( dict, 5 );

    if( builder->isSingleFile )
        tr_bencDictAddInt( dict, "length", builder->files[0].size );
    else
    {
        uint32_t  i;
        tr_benc * list = tr_bencDictAddList( dict, "files",
                                             builder->fileCount );
        for( i = 0; i < builder->fileCount; ++i )
        {
            tr_benc * dict = tr_bencListAddDict( list, 2 );
            tr_benc * length = tr_bencDictAdd( dict, "length" );
            tr_benc * pathVal = tr_bencDictAdd( dict, "path" );
            getFileInfo( builder->top, &builder->files[i], length, pathVal );
        }
    }

    tr_strlcpy( base, builder->top, sizeof( base ) );
    tr_bencDictAddStr( dict, "name", basename( base ) );

    tr_bencDictAddInt( dict, "piece length", builder->pieceSize );

    if( ( pch = getHashInfo( builder ) ) )
    {
        tr_bencDictAddRaw( dict, "pieces", pch,
                           SHA_DIGEST_LENGTH * builder->pieceCount );
        tr_free( pch );
    }

    tr_bencDictAddInt( dict, "private", builder->isPrivate ? 1 : 0 );
}

static void
tr_realMakeMetaInfo( tr_metainfo_builder * builder )
{
    int     i;
    tr_benc top;

    /* allow an empty set, but if URLs *are* listed, verify them. #814, #971 */
    for( i = 0; i < builder->trackerCount && !builder->result; ++i )
        if( !tr_httpIsValidURL( builder->trackers[i].announce ) )
            builder->result = TR_MAKEMETA_URL;

    tr_bencInitDict( &top, 6 );

    if( !builder->result && builder->trackerCount )
    {
        int       prevTier = -1;
        tr_benc * tier = NULL;

        if( builder->trackerCount > 1 )
        {
            tr_benc * annList = tr_bencDictAddList( &top, "announce-list",
                                                    0 );
            for( i = 0; i < builder->trackerCount; ++i )
            {
                if( prevTier != builder->trackers[i].tier )
                {
                    prevTier = builder->trackers[i].tier;
                    tier = tr_bencListAddList( annList, 0 );
                }
                tr_bencListAddStr( tier, builder->trackers[i].announce );
            }
        }

        tr_bencDictAddStr( &top, "announce", builder->trackers[0].announce );
    }

    if( !builder->result && !builder->abortFlag )
    {
        if( builder->comment && *builder->comment )
            tr_bencDictAddStr( &top, "comment", builder->comment );
        tr_bencDictAddStr( &top, "created by",
                           TR_NAME "/" LONG_VERSION_STRING );
        tr_bencDictAddInt( &top, "creation date", time( NULL ) );
        tr_bencDictAddStr( &top, "encoding", "UTF-8" );
        makeInfoDict( tr_bencDictAddDict( &top, "info", 666 ), builder );
    }

    /* save the file */
    if( !builder->result && !builder->abortFlag )
    {
        if( tr_bencSaveFile( builder->outputFile, &top ) )
        {
            builder->my_errno = errno;
            tr_strlcpy( builder->errfile, builder->outputFile,
                       sizeof( builder->errfile ) );
            builder->result = TR_MAKEMETA_IO_WRITE;
        }
    }

    /* cleanup */
    tr_bencFree( &top );
    if( builder->abortFlag )
        builder->result = TR_MAKEMETA_CANCELLED;
    builder->isDone = 1;
}

/***
****
****  A threaded builder queue
****
***/

static tr_metainfo_builder * queue = NULL;

static tr_thread *           workerThread = NULL;

static tr_lock*
getQueueLock( tr_handle * h )
{
    static tr_lock * lock = NULL;

    tr_globalLock( h );
    if( !lock )
        lock = tr_lockNew( );
    tr_globalUnlock( h );

    return lock;
}

static void
makeMetaWorkerFunc( void * user_data )
{
    tr_handle * handle = (tr_handle *) user_data;

    for( ; ; )
    {
        tr_metainfo_builder * builder = NULL;

        /* find the next builder to process */
        tr_lock *             lock = getQueueLock ( handle );
        tr_lockLock( lock );
        if( queue )
        {
            builder = queue;
            queue = queue->nextBuilder;
        }
        tr_lockUnlock( lock );

        /* if no builders, this worker thread is done */
        if( builder == NULL )
            break;

        tr_realMakeMetaInfo ( builder );
    }

    workerThread = NULL;
}

void
tr_makeMetaInfo( tr_metainfo_builder *   builder,
                 const char *            outputFile,
                 const tr_tracker_info * trackers,
                 int                     trackerCount,
                 const char *            comment,
                 int                     isPrivate )
{
    int       i;
    tr_lock * lock;

    /* free any variables from a previous run */
    for( i = 0; i < builder->trackerCount; ++i )
        tr_free( builder->trackers[i].announce );
    tr_free( builder->trackers );
    tr_free( builder->comment );
    tr_free( builder->outputFile );

    /* initialize the builder variables */
    builder->abortFlag = 0;
    builder->isDone = 0;
    builder->trackerCount = trackerCount;
    builder->trackers = tr_new0( tr_tracker_info, builder->trackerCount );
    for( i = 0; i < builder->trackerCount; ++i )
    {
        builder->trackers[i].tier = trackers[i].tier;
        builder->trackers[i].announce = tr_strdup( trackers[i].announce );
    }
    builder->comment = tr_strdup( comment );
    builder->isPrivate = isPrivate;
    if( outputFile && *outputFile )
        builder->outputFile = tr_strdup( outputFile );
    else
    {
        char out[MAX_PATH_LENGTH];
        tr_snprintf( out, sizeof( out ), "%s.torrent", builder->top );
        builder->outputFile = tr_strdup( out );
    }

    /* enqueue the builder */
    lock = getQueueLock ( builder->handle );
    tr_lockLock( lock );
    builder->nextBuilder = queue;
    queue = builder;
    if( !workerThread )
        workerThread = tr_threadNew( makeMetaWorkerFunc, builder->handle );
    tr_lockUnlock( lock );
}

