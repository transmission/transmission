/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h> /* FILE, snprintf, stderr */
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "trcompat.h" /* for strlcpy */
#include "transmission.h"
#include "internal.h" /* for tr_torrent_t */
#include "bencode.h"
#include "makemeta.h"
#include "platform.h" /* threads, locks */
#include "shared.h" /* shared lock */
#include "utils.h" /* buildpath */
#include "version.h"

/****
*****
****/

struct FileList
{
    struct FileList * next;
    uint64_t size;
    char * filename;
};

static struct FileList*
getFiles( const char        * dir,
          const char        * base,
          struct FileList   * list )
{
    int i;
    char buf[MAX_PATH_LENGTH];
    struct stat sb;
    DIR * odir = NULL;
    sb.st_size = 0;

    tr_buildPath( buf, sizeof(buf), dir, base, NULL );
    i = stat( buf, &sb );
    if( i ) {
        tr_err("makemeta couldn't stat \"%s\"; skipping. (%s)", buf, strerror(errno));
        return list;
    }

    if ( S_ISDIR( sb.st_mode ) && (( odir = opendir ( buf ) )) )
    {
        struct dirent *d;
        for (d = readdir( odir ); d!=NULL; d=readdir( odir ) )
            if( d->d_name && d->d_name[0]!='.' ) /* skip dotfiles, ., and .. */
                list = getFiles( buf, d->d_name, list );
        closedir( odir );
    }
    else if( S_ISREG( sb.st_mode ) )
    {
        struct FileList * node = tr_new( struct FileList, 1 );
        node->size = sb.st_size;
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

    if( totalSize >= (8*GiB) )
        return MiB;

    if( totalSize <= (8*MiB) )
        return 256 * KiB;

    return 512 * KiB;
}

static int
builderFileCompare ( const void * va, const void * vb)
{
    const tr_metainfo_builder_file_t * a = (const tr_metainfo_builder_file_t*) va;
    const tr_metainfo_builder_file_t * b = (const tr_metainfo_builder_file_t*) vb;
    return strcmp( a->filename, b->filename );
}

tr_metainfo_builder_t*
tr_metaInfoBuilderCreate( tr_handle_t * handle, const char * topFile )
{
    int i;
    struct FileList * files;
    struct FileList * walk;
    tr_metainfo_builder_t * ret = tr_new0( tr_metainfo_builder_t, 1 );
    ret->top = tr_strdup( topFile );
    ret->handle = handle; 
    if (1) {
        struct stat sb;
        stat( topFile, &sb );
        ret->isSingleFile = !S_ISDIR( sb.st_mode );
    }

    /* build a list of files containing topFile and,
       if it's a directory, all of its children */
    if (1) {
        char *dir, *base;
        char dirbuf[MAX_PATH_LENGTH];
        char basebuf[MAX_PATH_LENGTH];
        strlcpy( dirbuf, topFile, sizeof( dirbuf ) );
        strlcpy( basebuf, topFile, sizeof( basebuf ) );
        dir = dirname( dirbuf );
        base = basename( basebuf );
        files = getFiles( dir, base, NULL );
    }

    for( walk=files; walk!=NULL; walk=walk->next )
        ++ret->fileCount;

    ret->files = tr_new0( tr_metainfo_builder_file_t, ret->fileCount );

    for( i=0, walk=files; walk!=NULL; ++i )
    {
        struct FileList * tmp = walk;
        tr_metainfo_builder_file_t * file = &ret->files[i];
        walk = walk->next;
        file->filename = tmp->filename;
        file->size = tmp->size;
        ret->totalSize += tmp->size;
        tr_free( tmp );
    }

    qsort( ret->files,
           ret->fileCount,
           sizeof(tr_metainfo_builder_file_t),
           builderFileCompare );

    ret->pieceSize = bestPieceSize( ret->totalSize );
    ret->pieceCount = ret->pieceSize
        ? (int)( ret->totalSize / ret->pieceSize)
        : 0;
    if( ret->totalSize % ret->pieceSize )
        ++ret->pieceCount;

    return ret;
}

void
tr_metaInfoBuilderFree( tr_metainfo_builder_t * builder )
{
    if( builder != NULL )
    {
        int i;
        for( i=0; i<builder->fileCount; ++i )
            tr_free( builder->files[i].filename );
        tr_free( builder->files );
        tr_free( builder->top );
        tr_free( builder->comment );
        tr_free( builder->announce );
        tr_free( builder->outputFile );
        tr_free( builder );
    }
}

/****
*****
****/

static uint8_t*
getHashInfo ( tr_metainfo_builder_t * b )
{
    int fileIndex = 0;
    uint8_t *ret = tr_new0( uint8_t, SHA_DIGEST_LENGTH * b->pieceCount );
    uint8_t *walk = ret;
    uint8_t *buf;
    uint64_t totalRemain;
    uint64_t off = 0;
    FILE * fp;

    if( !b->totalSize )
        return ret;

    buf = tr_new( uint8_t, b->pieceSize );
    b->pieceIndex = 0;
    totalRemain = b->totalSize;
    fp = fopen( b->files[fileIndex].filename, "rb" );
    while ( totalRemain )
    {
        uint8_t *bufptr = buf;
        const uint64_t thisPieceSize =
            MIN( (uint32_t)b->pieceSize, totalRemain );
        uint64_t pieceRemain = thisPieceSize;

        assert( b->pieceIndex < b->pieceCount );

        while( pieceRemain )
        {
            const uint64_t n_this_pass =
                MIN( (b->files[fileIndex].size - off), pieceRemain );
            fread( bufptr, 1, n_this_pass, fp );
            bufptr += n_this_pass;
            off += n_this_pass;
            pieceRemain -= n_this_pass;
            if( off == b->files[fileIndex].size ) {
                off = 0;
                fclose( fp );
                fp = NULL;
                if( ++fileIndex < b->fileCount ) {
                    fp = fopen( b->files[fileIndex].filename, "rb" );
                }
            }
        }

        assert( bufptr-buf == (int)thisPieceSize );
        assert( pieceRemain == 0 );
        SHA1( buf, thisPieceSize, walk );
        walk += SHA_DIGEST_LENGTH;

        if( b->abortFlag ) {
            b->failed = 1;
            break;
        }

        totalRemain -= thisPieceSize;
        ++b->pieceIndex;
    }
    assert( b->abortFlag || (walk-ret == (int)(SHA_DIGEST_LENGTH*b->pieceCount)) );
    assert( b->abortFlag || !totalRemain );
    assert( b->abortFlag || fp == NULL );

    if( fp != NULL )
        fclose( fp );

    tr_free( buf );
    return ret;
}

static void
getFileInfo( const char                        * topFile,
             const tr_metainfo_builder_file_t  * file,
             benc_val_t                        * uninitialized_length,
             benc_val_t                        * uninitialized_path )
{
    benc_val_t *sub;
    const char *pch, *prev;
    const size_t topLen = strlen(topFile) + 1; /* +1 for '/' */
    int n;

    /* get the file size */
    tr_bencInitInt( uninitialized_length, file->size );

    /* the path list */
    n = 1;
    for( pch=file->filename+topLen; *pch; ++pch )
        if (*pch == TR_PATH_DELIMITER)
            ++n;
    tr_bencInit( uninitialized_path, TYPE_LIST );
    tr_bencListReserve( uninitialized_path, n );
    for( prev=pch=file->filename+topLen; ; ++pch )
    {
        char buf[MAX_PATH_LENGTH];

        if (*pch && *pch!=TR_PATH_DELIMITER )
            continue;

        memcpy( buf, prev, pch-prev );
        buf[pch-prev] = '\0';

        sub = tr_bencListAdd( uninitialized_path );
        tr_bencInitStrDup( sub, buf );

        prev = pch + 1;
        if (!*pch)
           break;
    }
}

static void
makeFilesList( benc_val_t                 * list,
               const tr_metainfo_builder_t  * builder )
{
    int i = 0;

    tr_bencListReserve( list, builder->fileCount );

    for( i=0; i<builder->fileCount; ++i )
    {
        benc_val_t * dict = tr_bencListAdd( list );
        benc_val_t *length, *pathVal;

        tr_bencInit( dict, TYPE_DICT );
        tr_bencDictReserve( dict, 2 );
        length = tr_bencDictAdd( dict, "length" );
        pathVal = tr_bencDictAdd( dict, "path" );
        getFileInfo( builder->top, &builder->files[i], length, pathVal );
    }
}

static void
makeInfoDict ( benc_val_t             * dict,
               tr_metainfo_builder_t  * builder )
{
    uint8_t * pch;
    benc_val_t * val;
    char base[MAX_PATH_LENGTH];

    tr_bencDictReserve( dict, 5 );
    
    if ( builder->isSingleFile )
    {
        val = tr_bencDictAdd( dict, "length" );
        tr_bencInitInt( val, builder->files[0].size );
    }
    else
    {
        val = tr_bencDictAdd( dict, "files" );
        tr_bencInit( val, TYPE_LIST );
        makeFilesList( val, builder );
    }

    val = tr_bencDictAdd( dict, "name" );
    strlcpy( base, builder->top, sizeof( base ) );
    tr_bencInitStrDup ( val, basename( base ) );

    val = tr_bencDictAdd( dict, "piece length" );
    tr_bencInitInt( val, builder->pieceSize );

    pch = getHashInfo( builder );
    val = tr_bencDictAdd( dict, "pieces" );
    tr_bencInitStr( val, pch, SHA_DIGEST_LENGTH * builder->pieceCount, 0 );

    val = tr_bencDictAdd( dict, "private" );
    tr_bencInitInt( val, builder->isPrivate ? 1 : 0 );
}

static void tr_realMakeMetaInfo ( tr_metainfo_builder_t * builder )
{
    int n = 5;
    benc_val_t top, *val;

    tr_bencInit ( &top, TYPE_DICT );
    if ( builder->comment && *builder->comment ) ++n;
    tr_bencDictReserve( &top, n );

    val = tr_bencDictAdd( &top, "announce" );
    tr_bencInitStrDup( val, builder->announce );
    
    if( builder->comment && *builder->comment ) {
        val = tr_bencDictAdd( &top, "comment" );
        tr_bencInitStrDup( val, builder->comment );
    }

    val = tr_bencDictAdd( &top, "created by" );
    tr_bencInitStrDup( val, TR_NAME "/" LONG_VERSION_STRING );

    val = tr_bencDictAdd( &top, "creation date" );
    tr_bencInitInt( val, time(0) );

    val = tr_bencDictAdd( &top, "encoding" );
    tr_bencInitStrDup( val, "UTF-8" );

    val = tr_bencDictAdd( &top, "info" );
    tr_bencInit( val, TYPE_DICT );
    tr_bencDictReserve( val, 666 );
    makeInfoDict( val, builder );

    /* save the file */
    if ( !builder->abortFlag ) {
        size_t nmemb;
        char * pch = tr_bencSaveMalloc( &top, &n );
        FILE * fp = fopen( builder->outputFile, "wb+" );
        nmemb = n;
        if( fp == NULL )
            builder->failed = 1;
        else if( fwrite( pch, 1, nmemb, fp ) != nmemb )
            builder->failed = 1;
        tr_free( pch );
        fclose( fp );
    }

    /* cleanup */
    tr_bencFree( & top );
    builder->failed |= builder->abortFlag;
    builder->isDone = 1;
}

/***
****
****  A threaded builder queue
****
***/

static tr_metainfo_builder_t * queue = NULL;

static tr_thread_t * workerThread = NULL;

static tr_lock_t* getQueueLock( tr_handle_t * h )
{
    static tr_lock_t * lock = NULL;

    tr_sharedLock( h->shared );
    if( !lock )
         lock = tr_lockNew( );
    tr_sharedUnlock( h->shared );

    return lock;
}

static void workerFunc( void * user_data )
{
    tr_handle_t * handle = (tr_handle_t *) user_data;

    for (;;)
    {
        tr_metainfo_builder_t * builder = NULL;

        /* find the next builder to process */
        tr_lock_t * lock = getQueueLock ( handle );
        tr_lockLock( lock );
        if( queue != NULL ) {
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
tr_makeMetaInfo( tr_metainfo_builder_t  * builder,
                 const char             * outputFile,
                 const char             * announce,
                 const char             * comment,
                 int                      isPrivate )
{
    tr_lock_t * lock;

    builder->abortFlag = 0;
    builder->isDone = 0;
    builder->announce = tr_strdup( announce );
    builder->comment = tr_strdup( comment );
    builder->isPrivate = isPrivate;
    if( outputFile && *outputFile )
        builder->outputFile = tr_strdup( outputFile );
    else {
        char out[MAX_PATH_LENGTH];
        snprintf( out, sizeof(out), "%s.torrent", builder->top);
        builder->outputFile = tr_strdup( out );
    }

    /* enqueue the builder */
    lock = getQueueLock ( builder->handle );
    tr_lockLock( lock );
    builder->nextBuilder = queue;
    queue = builder;
    if( !workerThread )
         workerThread = tr_threadNew( workerFunc, builder->handle, "makeMeta" );
    tr_lockUnlock( lock );
}

