/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#ifndef TR_MAKEMETA_H
#define TR_MAKEMETA_H 1

typedef struct tr_metainfo_builder_file_s
{
    char * filename;
    uint64_t size;
}
tr_metainfo_builder_file_t;

typedef struct tr_metainfo_builder_s
{
    /**
    ***  These are set by tr_makeMetaInfo()
    ***  and cleaned up by tr_metaInfoBuilderFree()
    **/

    char * top;
    tr_metainfo_builder_file_t * files;
    uint64_t totalSize;
    int fileCount;
    int pieceSize;
    int pieceCount;
    int isSingleFile;
    tr_handle_t * handle;

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

    int pieceIndex;
    int abortFlag;
    int isDone;
    int failed; /* only meaningful if isDone is set */

    /**
    ***  This is an implementation detail.
    ***  The client should never use these fields.
    **/

    struct tr_metainfo_builder_s * nextBuilder;
}
tr_metainfo_builder_t;




tr_metainfo_builder_t*
tr_metaInfoBuilderCreate( tr_handle_t  * handle,
                          const char   * topFile );

void
tr_metaInfoBuilderFree( tr_metainfo_builder_t* );

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
tr_makeMetaInfo( tr_metainfo_builder_t  * builder,
                 const char             * outputFile,
                 const char             * announce,
                 const char             * comment,
                 int                      isPrivate );


#endif
