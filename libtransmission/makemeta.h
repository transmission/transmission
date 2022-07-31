// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <future>
#include <string>
#include <utility>
#include <vector>

#include "transmission.h"

#include "announce-list.h"
#include "block-info.h"
#include "file.h"
#include "torrent-files.h"

class tr_metainfo_builder
{
public:
    tr_metainfo_builder(std::string_view top_file_or_root_directory);

    /*
     * Checksums must be generated before calling benc() or save().
     * Since the process is slow -- it's equivalent to "verify local data" --
     * client code can either call it in a blocking call, or it can run in a
     * worker thread.
     *
     * If run async, it can be cancelled via `cancelAsyncChecksums()` and
     * its progress can be polled with `checksumProgress()`. When the task
     * is done, the future will resolve with an error, or with nullptr if no error.
     */

    bool makeChecksums(tr_error** error = nullptr);

    std::future<tr_error*> makeChecksumsAsync();

    // returns thet status of a `makeChecksumsAsync()` call:
    // the current and total number of pieces if active, or nullopt if done
    [[nodiscard]] std::pair<tr_piece_index_t, tr_piece_index_t> checksumStatus() const noexcept
    {
        return std::make_pair(checksum_piece_, block_info_.pieceCount());
    }

    void cancelAsyncChecksums() noexcept
    {
        cancel_ = true;
    }

    std::string benc(tr_error** error = nullptr) const;

    bool save(std::string_view filename, tr_error** error = nullptr) const;

    ///

    [[nodiscard]] auto const& files() const noexcept
    {
        return files_;
    }

    void setAnnounceList(tr_announce_list&& announce)
    {
        announce_ = std::move(announce);
    }

    [[nodiscard]] auto const& announceList() const noexcept
    {
        return announce_;
    }

    void setWebseeds(std::vector<std::string>&& webseeds)
    {
        webseeds_ = std::move(webseeds);
    }

    [[nodiscard]] auto const& webseeds() const noexcept
    {
        return webseeds_;
    }

    void setComment(std::string_view comment)
    {
        comment_ = comment;
    }

    [[nodiscard]] auto const& comment() const noexcept
    {
        return comment_;
    }

    void setSource(std::string_view source)
    {
        source_ = source;
    }

    [[nodiscard]] auto const& source() const noexcept
    {
        return source_;
    }

    void setPrivate(bool is_private)
    {
        is_private_ = is_private;
    }

    [[nodiscard]] auto const& isPrivate() const noexcept
    {
        return is_private_;
    }

    // whether or not to include User-Agent and creation time
    void setAnonymize(bool anonymize)
    {
        anonymize_ = anonymize;
    }

    [[nodiscard]] auto const& anonymize() const noexcept
    {
        return anonymize_;
    }

    static uint32_t defaultPieceSize(uint64_t total_size);

    // must be a power of two and >= 16 KiB
    static bool isLegalPieceSize(uint32_t x);

    bool setPieceSize(uint32_t piece_size);

    [[nodiscard]] auto const& blockInfo() const noexcept
    {
        return block_info_;
    }

    [[nodiscard]] auto const& top() const noexcept
    {
        return top_;
    }

    [[nodiscard]] auto name() const noexcept
    {
        return tr_sys_path_basename(top_);
    }

private:
    void makeChecksumsImpl(std::promise<tr_error*> promise);

    std::string top_;
    tr_torrent_files files_;
    tr_announce_list announce_;
    tr_block_info block_info_;
    std::vector<std::byte> piece_hashes_;
    std::vector<std::string> webseeds_;

    std::string comment_;
    std::string source_;

    tr_piece_index_t checksum_piece_ = 0;

    bool is_private_ = false;
    bool anonymize_ = false;
    bool cancel_ = false;
};

#if 0
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
    bool anonymize;

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
    bool anonymize,
    char const* source);
#endif
