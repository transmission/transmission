// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint32_t, uint64_t

#include "transmission.h"

struct tr_metainfo_builder_file
{
    char* filename;
    uint64_t size;
    bool is_portable;
};

enum class TrMakemetaResult
{
    OK,
    CANCELLED,
    ERR_URL, // invalid announce URL
    ERR_IO_READ, // see builder.errfile, builder.my_errno
    ERR_IO_WRITE // see builder.errfile, builder.my_errno
};

struct tr_tracker_info
{
    int tier;
    char* announce;
};

struct tr_metainfo_builder
{
    /**
    ***  These are set by tr_makeMetaInfoBuilderCreate()
    ***  and cleaned up by tr_metaInfoBuilderFree()
    **/

    char* top;
    tr_metainfo_builder_file* files;
    uint64_t totalSize;
    uint32_t fileCount;
    uint32_t pieceSize;
    uint32_t pieceCount;
    bool isFolder;

    /**
    ***  These are set inside tr_makeMetaInfo()
    ***  by copying the arguments passed to it,
    ***  and cleaned up by tr_metaInfoBuilderFree()
    **/

    tr_tracker_info* trackers;
    int trackerCount;

    char** webseeds;
    int webseedCount;

    char* comment;
    char* outputFile;
    bool isPrivate;
    char* source;

    /**
    ***  These are set inside tr_makeMetaInfo() so the client
    ***  can poll periodically to see what the status is.
    ***  The client can also set abortFlag to nonzero to
    ***  tell tr_makeMetaInfo() to abort and clean up after itself.
    **/

    uint32_t pieceIndex;
    bool abortFlag;
    bool isDone;
    TrMakemetaResult result;

    /* file in use when result was set to _IO_READ or _IO_WRITE,
     * or the URL in use when the result was set to _URL */
    char errfile[2048];

    /* errno encountered when result was set to _IO_READ or _IO_WRITE */
    int my_errno;

    /**
    ***  This is an implementation detail.
    ***  The client should never use these fields.
    **/

    struct tr_metainfo_builder* nextBuilder;
};

tr_metainfo_builder* tr_metaInfoBuilderCreate(char const* topFile);

/**
 * Call this before tr_makeMetaInfo() to override the builder.pieceSize
 * and builder.pieceCount values that were set by tr_metainfoBuilderCreate()
 *
 * @return false if the piece size isn't valid; eg, isn't a power of two.
 */
bool tr_metaInfoBuilderSetPieceSize(tr_metainfo_builder* builder, uint32_t bytes);

void tr_metaInfoBuilderFree(tr_metainfo_builder*);

/**
 * @brief create a new torrent file
 *
 * This is actually done in a worker thread, not the main thread!
 * Otherwise the client's interface would lock up while this runs.
 *
 * It is the caller's responsibility to poll builder->isDone
 * from time to time!  When the worker thread sets that flag,
 * the caller must pass the builder to tr_metaInfoBuilderFree().
 *
 * @param outputFile if nullptr, builder->top + ".torrent" will be used.

 * @param trackers An array of trackers, sorted by tier from first to last.
 *                 NOTE: only the `tier' and `announce' fields are used.
 *
 * @param trackerCount size of the `trackers' array
 */
void tr_makeMetaInfo(
    tr_metainfo_builder* builder,
    char const* output_file,
    tr_tracker_info const* trackers,
    int n_trackers,
    char const** webseeds,
    int n_webseeds,
    char const* comment,
    bool is_private,
    char const* source);
