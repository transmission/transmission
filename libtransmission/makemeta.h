// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm> // std::move
#include <cstddef> // std::byte
#include <future>
#include <string>
#include <string_view>
#include <utility> // std::pair
#include <vector>

#include "transmission.h"

#include "announce-list.h"
#include "block-info.h"
#include "file.h"
#include "torrent-files.h"

class tr_metainfo_builder
{
public:
    tr_metainfo_builder(std::string_view single_file_or_parent_directory);

    // Generate piece checksums asynchronously.
    // - This must be done before calling `benc()` or `save()`.
    // - Runs in a worker thread because it can be time-consuming.
    // - Can be cancelled with `cancelChecksums()` and polled with `checksumStatus()`
    // - Resolves with a `tr_error*` which is set on failure or nullptr on success.
    std::future<tr_error*> makeChecksums()
    {
        return std::async(
            std::launch::async,
            [this]()
            {
                tr_error* error = nullptr;
                blockingMakeChecksums(&error);
                return error;
            });
    }

    // Returns the status of a `makeChecksums()` call:
    // The current piece being tested and the total number of pieces in the torrent.
    [[nodiscard]] constexpr std::pair<tr_piece_index_t, tr_piece_index_t> checksumStatus() const noexcept
    {
        return std::make_pair(checksum_piece_, block_info_.pieceCount());
    }

    // Tell the `makeChecksums()` worker thread to cleanly exit ASAP.
    constexpr void cancelChecksums() noexcept
    {
        cancel_ = true;
    }

    std::string benc(tr_error** error = nullptr) const;

    bool save(std::string_view filename, tr_error** error = nullptr) const
    {
        return tr_saveFile(filename, benc(error), error);
    }

    ///

    [[nodiscard]] auto fileCount() const noexcept
    {
        return files_.fileCount();
    }

    [[nodiscard]] constexpr auto totalSize() const noexcept
    {
        return files_.totalSize();
    }

    [[nodiscard]] auto const& path(tr_file_index_t i) const noexcept
    {
        return files_.path(i);
    }

    [[nodiscard]] auto fileSize(tr_file_index_t i) const noexcept
    {
        return files_.fileSize(i);
    }

    void setAnnounceList(tr_announce_list&& announce)
    {
        announce_ = std::move(announce);
    }

    [[nodiscard]] constexpr auto const& announceList() const noexcept
    {
        return announce_;
    }

    void setWebseeds(std::vector<std::string>&& webseeds)
    {
        webseeds_ = std::move(webseeds);
    }

    [[nodiscard]] constexpr auto const& webseeds() const noexcept
    {
        return webseeds_;
    }

    void setComment(std::string_view comment)
    {
        comment_ = comment;
    }

    [[nodiscard]] constexpr auto const& comment() const noexcept
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

    constexpr void setPrivate(bool is_private) noexcept
    {
        is_private_ = is_private;
    }

    [[nodiscard]] constexpr auto const& isPrivate() const noexcept
    {
        return is_private_;
    }

    // whether or not to include User-Agent and creation time
    constexpr void setAnonymize(bool anonymize)
    {
        anonymize_ = anonymize;
    }

    [[nodiscard]] constexpr auto const& anonymize() const noexcept
    {
        return anonymize_;
    }

    static uint32_t defaultPieceSize(uint64_t total_size);

    // must be a power of two and >= 16 KiB
    static bool isLegalPieceSize(uint32_t x);

    bool setPieceSize(uint32_t piece_size);

    [[nodiscard]] constexpr auto pieceCount() const noexcept
    {
        return block_info_.pieceCount();
    }

    [[nodiscard]] constexpr auto pieceSize() const noexcept
    {
        return block_info_.pieceSize();
    }

    [[nodiscard]] constexpr auto const& top() const noexcept
    {
        return top_;
    }

    [[nodiscard]] auto name() const noexcept
    {
        return tr_sys_path_basename(top_);
    }

private:
    bool blockingMakeChecksums(tr_error** error = nullptr);

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
