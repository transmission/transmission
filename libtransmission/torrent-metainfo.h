// This file Copyright © 2005-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint32_t, uint64_t
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "block-info.h"
#include "magnet-metainfo.h"
#include "torrent-files.h"
#include "tr-strbuf.h"

struct tr_error;

struct tr_torrent_metainfo : public tr_magnet_metainfo
{
public:
    [[nodiscard]] TR_CONSTEXPR20 auto empty() const noexcept
    {
        return std::empty(files_);
    }

    bool parseBenc(std::string_view benc, tr_error** error = nullptr);

    // Helper function wrapper around parseBenc().
    // If you're looping through several files, passing in a non-nullptr
    // `contents` can reduce the number of memory allocations needed to
    // load multiple files.
    bool parseTorrentFile(std::string_view benc_filename, std::vector<char>* contents = nullptr, tr_error** error = nullptr);

    // FILES

    [[nodiscard]] constexpr auto const& files() const noexcept
    {
        return files_;
    }
    [[nodiscard]] TR_CONSTEXPR20 auto fileCount() const noexcept
    {
        return files().fileCount();
    }
    [[nodiscard]] TR_CONSTEXPR20 auto fileSize(tr_file_index_t i) const
    {
        return files().fileSize(i);
    }
    [[nodiscard]] TR_CONSTEXPR20 auto const& fileSubpath(tr_file_index_t i) const
    {
        return files().path(i);
    }

    void setFileSubpath(tr_file_index_t i, std::string_view subpath)
    {
        files_.setPath(i, subpath);
    }

    /// BLOCK INFO

    [[nodiscard]] constexpr auto const& blockInfo() const noexcept
    {
        return block_info_;
    }

    [[nodiscard]] constexpr auto blockCount() const noexcept
    {
        return blockInfo().blockCount();
    }
    [[nodiscard]] constexpr auto byteLoc(uint64_t nth_byte) const noexcept
    {
        return blockInfo().byteLoc(nth_byte);
    }
    [[nodiscard]] constexpr auto blockLoc(tr_block_index_t block) const noexcept
    {
        return blockInfo().blockLoc(block);
    }
    [[nodiscard]] constexpr auto pieceLoc(tr_piece_index_t piece, uint32_t offset = 0, uint32_t length = 0) const noexcept
    {
        return blockInfo().pieceLoc(piece, offset, length);
    }
    [[nodiscard]] constexpr auto blockSize(tr_block_index_t block) const noexcept
    {
        return blockInfo().blockSize(block);
    }
    [[nodiscard]] constexpr auto blockSpanForPiece(tr_piece_index_t piece) const noexcept
    {
        return blockInfo().blockSpanForPiece(piece);
    }
    [[nodiscard]] constexpr auto pieceCount() const noexcept
    {
        return blockInfo().pieceCount();
    }
    [[nodiscard]] constexpr auto pieceSize() const noexcept
    {
        return blockInfo().pieceSize();
    }
    [[nodiscard]] constexpr auto pieceSize(tr_piece_index_t piece) const noexcept
    {
        return blockInfo().pieceSize(piece);
    }
    [[nodiscard]] constexpr auto totalSize() const noexcept
    {
        return blockInfo().totalSize();
    }

    // OTHER PROPERTIES

    [[nodiscard]] constexpr auto const& comment() const noexcept
    {
        return comment_;
    }
    [[nodiscard]] constexpr auto const& creator() const noexcept
    {
        return creator_;
    }
    [[nodiscard]] constexpr auto const& source() const noexcept
    {
        return source_;
    }

    [[nodiscard]] constexpr auto const& isPrivate() const noexcept
    {
        return is_private_;
    }

    [[nodiscard]] TR_CONSTEXPR20 tr_sha1_digest_t const& pieceHash(tr_piece_index_t piece) const
    {
        return pieces_[piece];
    }

    [[nodiscard]] TR_CONSTEXPR20 bool hasV1Metadata() const noexcept
    {
        // need 'pieces' field and 'files' or 'length'
        // TODO check for 'files' or 'length'
        return !std::empty(pieces_);
    }

    [[nodiscard]] constexpr bool hasV2Metadata() const noexcept
    {
        return is_v2_;
    }

    [[nodiscard]] constexpr auto const& dateCreated() const noexcept
    {
        return date_created_;
    }

    [[nodiscard]] constexpr auto infoDictSize() const noexcept
    {
        return info_dict_size_;
    }

    [[nodiscard]] constexpr auto infoDictOffset() const noexcept
    {
        return info_dict_offset_;
    }

    [[nodiscard]] constexpr auto piecesOffset() const noexcept
    {
        return pieces_offset_;
    }

    // UTILS

    [[nodiscard]] auto torrentFile(std::string_view torrent_dir) const
    {
        return makeFilename(torrent_dir, name(), infoHashString(), BasenameFormat::Hash, ".torrent");
    }

    [[nodiscard]] auto magnetFile(std::string_view torrent_dir) const
    {
        return makeFilename(torrent_dir, name(), infoHashString(), BasenameFormat::Hash, ".magnet");
    }

    [[nodiscard]] auto resumeFile(std::string_view resume_dir) const
    {
        return makeFilename(resume_dir, name(), infoHashString(), BasenameFormat::Hash, ".resume");
    }

    static bool migrateFile(
        std::string_view dirname,
        std::string_view name,
        std::string_view info_hash_string,
        std::string_view suffix);

    static void removeFile(
        std::string_view dirname,
        std::string_view name,
        std::string_view info_hash_string,
        std::string_view suffix);

private:
    friend struct MetainfoHandler;
    static bool parseImpl(tr_torrent_metainfo& setme, std::string_view benc, tr_error** error);
    // static bool parsePath(std::string_view root, tr_variant* path, std::string& setme);
    static std::string fixWebseedUrl(tr_torrent_metainfo const& tm, std::string_view url);
    // static std::string_view parseFiles(tr_torrent_metainfo& setme, tr_variant* info_dict, uint64_t* setme_total_size);
    // static std::string_view parseAnnounce(tr_torrent_metainfo& setme, tr_variant* meta);
    // static void parseWebseeds(tr_torrent_metainfo& setme, tr_variant* meta);

    enum class BasenameFormat
    {
        Hash,
        NameAndPartialHash
    };

    [[nodiscard]] static tr_pathbuf makeFilename(
        std::string_view dirname,
        std::string_view name,
        std::string_view info_hash_string,
        BasenameFormat format,
        std::string_view suffix);

    [[nodiscard]] auto makeFilename(std::string_view dirname, BasenameFormat format, std::string_view suffix) const
    {
        return makeFilename(dirname, name(), infoHashString(), format, suffix);
    }

    tr_block_info block_info_ = tr_block_info{ 0, 0 };

    tr_torrent_files files_;

    std::vector<tr_sha1_digest_t> pieces_;

    std::string comment_;
    std::string creator_;
    std::string source_;

    time_t date_created_ = 0;

    // Offset + size of the bencoded info dict subset of the bencoded data.
    // Used when loading pieces of it to sent to magnet peers.
    // See http://bittorrent.org/beps/bep_0009.html
    uint64_t info_dict_size_ = 0;
    uint64_t info_dict_offset_ = 0;

    // Offset of the bencoded 'pieces' checksums subset of the bencoded data.
    // Used when loading piece checksums on demand.
    uint64_t pieces_offset_ = 0;

    bool has_magnet_info_hash_ = false;
    bool is_private_ = false;
    bool is_v2_ = false;
};
