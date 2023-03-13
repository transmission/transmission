// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm> // std::move
#include <cstddef> // std::byte
#include <cstdint>
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
#include "utils.h" // for tr_saveFile()

class tr_metainfo_builder
{
public:
    explicit tr_metainfo_builder(std::string_view single_file_or_parent_directory);

    tr_metainfo_builder(tr_metainfo_builder&&) = delete;
    tr_metainfo_builder(tr_metainfo_builder const&) = delete;
    tr_metainfo_builder& operator=(tr_metainfo_builder&&) = delete;
    tr_metainfo_builder& operator=(tr_metainfo_builder const&) = delete;

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

    // generate the metainfo
    [[nodiscard]] std::string benc(tr_error** error = nullptr) const;

    // generate the metainfo and save it to a torrent file
    bool save(std::string_view filename, tr_error** error = nullptr) const
    {
        return tr_saveFile(filename, benc(error), error);
    }

    /// setters

    void setAnnounceList(tr_announce_list announce)
    {
        announce_ = std::move(announce);
    }

    // whether or not to include User-Agent and creation time
    constexpr void setAnonymize(bool anonymize) noexcept
    {
        anonymize_ = anonymize;
    }

    void setComment(std::string_view comment)
    {
        comment_ = comment;
    }

    bool setPieceSize(uint32_t piece_size) noexcept;

    constexpr void setPrivate(bool is_private) noexcept
    {
        is_private_ = is_private;
    }

    void setSource(std::string_view source)
    {
        source_ = source;
    }

    void setWebseeds(std::vector<std::string> webseeds)
    {
        webseeds_ = std::move(webseeds);
    }

    /// getters

    [[nodiscard]] constexpr auto const& announceList() const noexcept
    {
        return announce_;
    }

    [[nodiscard]] constexpr auto const& anonymize() const noexcept
    {
        return anonymize_;
    }

    [[nodiscard]] constexpr auto const& comment() const noexcept
    {
        return comment_;
    }

    [[nodiscard]] TR_CONSTEXPR20 auto fileCount() const noexcept
    {
        return files_.fileCount();
    }

    [[nodiscard]] TR_CONSTEXPR20 auto fileSize(tr_file_index_t i) const noexcept
    {
        return files_.fileSize(i);
    }

    [[nodiscard]] constexpr auto const& isPrivate() const noexcept
    {
        return is_private_;
    }

    [[nodiscard]] auto name() const noexcept
    {
        return tr_sys_path_basename(top_);
    }

    [[nodiscard]] auto const& path(tr_file_index_t i) const noexcept
    {
        return files_.path(i);
    }

    [[nodiscard]] constexpr auto pieceSize() const noexcept
    {
        return block_info_.pieceSize();
    }

    [[nodiscard]] constexpr auto pieceCount() const noexcept
    {
        return block_info_.pieceCount();
    }

    [[nodiscard]] constexpr auto const& source() const noexcept
    {
        return source_;
    }

    [[nodiscard]] constexpr auto const& top() const noexcept
    {
        return top_;
    }

    [[nodiscard]] constexpr auto totalSize() const noexcept
    {
        return files_.totalSize();
    }

    [[nodiscard]] constexpr auto const& webseeds() const noexcept
    {
        return webseeds_;
    }

    ///

    [[nodiscard]] constexpr static uint32_t defaultPieceSize(uint64_t total_size) noexcept
    {
        uint32_t const KiB = 1024;
        uint32_t const MiB = 1048576;
        uint32_t const GiB = 1073741824;

        if (total_size >= 2 * GiB)
        {
            return 2 * MiB;
        }

        if (total_size >= 1 * GiB)
        {
            return 1 * MiB;
        }

        if (total_size >= 512 * MiB)
        {
            return 512 * KiB;
        }

        if (total_size >= 350 * MiB)
        {
            return 256 * KiB;
        }

        if (total_size >= 150 * MiB)
        {
            return 128 * KiB;
        }

        if (total_size >= 50 * MiB)
        {
            return 64 * KiB;
        }

        return 32 * KiB; /* less than 50 meg */
    }

    // must be a power of two and >= 16 KiB
    [[nodiscard]] static bool isLegalPieceSize(uint32_t x);

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
