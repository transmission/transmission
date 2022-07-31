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
