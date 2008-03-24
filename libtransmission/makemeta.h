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

#ifndef TR_MAKEMETA_H
#define TR_MAKEMETA_H 1

typedef struct tr_metainfo_builder_file
{
    char * filename;
    uint64_t size;
}
tr_metainfo_builder_file;

typedef enum
{
    TR_MAKEMETA_OK,
    TR_MAKEMETA_URL,
    TR_MAKEMETA_CANCELLED,
    TR_MAKEMETA_IO_READ,   /* see builder.errfile, builder.errno */
    TR_MAKEMETA_IO_WRITE   /* see builder.errfile, builder.errno */
}
tr_metainfo_builder_err;

    

typedef struct tr_metainfo_builder
{
    /**
    ***  These are set by tr_makeMetaInfoBuilderCreate()
    ***  and cleaned up by tr_metaInfoBuilderFree()
    **/

    char * top;
    tr_metainfo_builder_file * files;
    uint64_t totalSize;
    uint32_t fileCount;
    uint32_t pieceSize;
    uint32_t pieceCount;
    int isSingleFile;
    tr_handle * handle;

    /**
    ***  These are set inside tr_makeMetaInfo()
    ***  by copying the arguments passed to it,
    ***  and cleaned up by tr_metaInfoBuilderFree()
    **/

    char * announce;
    char * comment;
    char * outputFile;
    int isPrivate;

    /**
    ***  These are set inside tr_makeMetaInfo() so the client
    ***  can poll periodically to see what the status is.
    ***  The client can also set abortFlag to nonzero to
    ***  tell tr_makeMetaInfo() to abort and clean up after itself.
    **/

    uint32_t pieceIndex;
    int abortFlag;
    int isDone;
    tr_metainfo_builder_err result;

    /* file in use when result was set to _IO_READ or _IO_WRITE */
    char errfile[2048];

    /* errno encountered when result was set to _IO_READ or _IO_WRITE */
    int my_errno;

    /**
    ***  This is an implementation detail.
    ***  The client should never use these fields.
    **/

    struct tr_metainfo_builder * nextBuilder;
}
tr_metainfo_builder;


tr_metainfo_builder*
tr_metaInfoBuilderCreate( tr_handle   * handle,
                          const char  * topFile );

void
tr_metaInfoBuilderFree( tr_metainfo_builder* );

/**
 * 'outputFile' if NULL, builder->top + ".torrent" will be used.
 *
 * This is actually done in a worker thread, not the main thread!
 * Otherwise the client's interface would lock up while this runs.
 *
 * It is the caller's responsibility to poll builder->isDone
 * from time to time!  When the worker thread sets that flag,
 * the caller must pass the builder to tr_metaInfoBuilderFree().
 */
void
tr_makeMetaInfo( tr_metainfo_builder  * builder,
                 const char           * outputFile,
                 const char           * announce,
                 const char           * comment,
                 int                    isPrivate );


#endif
