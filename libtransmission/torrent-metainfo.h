// This file Copyright © 2005-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "block-info.h"
#include "magnet-metainfo.h"

struct tr_error;

struct tr_torrent_metainfo : public tr_magnet_metainfo
{
public:
    [[nodiscard]] auto empty() const
    {
        return std::empty(files_);
    }

    bool parseBenc(std::string_view benc, tr_error** error = nullptr);

    // Helper function wrapper around parseBenc().
    // If you're looping through several files, passing in a non-nullptr
    // `buffer` can reduce the number of memory allocations needed to
    // load multiple files.
    bool parseTorrentFile(std::string_view benc_filename, std::vector<char>* buffer = nullptr, tr_error** error = nullptr);

    /// BLOCK INFO

    [[nodiscard]] constexpr auto const& blockInfo() const
    {
        return block_info_;
    }

    [[nodiscard]] constexpr auto blockCount() const
    {
        return blockInfo().blockCount();
    }
    [[nodiscard]] auto byteLoc(uint64_t nth_byte) const
    {
        return blockInfo().byteLoc(nth_byte);
    }
    [[nodiscard]] auto blockLoc(tr_block_index_t block) const
    {
        return blockInfo().blockLoc(block);
    }
    [[nodiscard]] auto pieceLoc(tr_piece_index_t piece, uint32_t offset = 0, uint32_t length = 0) const
    {
        return blockInfo().pieceLoc(piece, offset, length);
    }
    [[nodiscard]] constexpr auto blockSize(tr_block_index_t block) const
    {
        return blockInfo().blockSize(block);
    }
    [[nodiscard]] constexpr auto blockSpanForPiece(tr_piece_index_t piece) const
    {
        return blockInfo().blockSpanForPiece(piece);
    }
    [[nodiscard]] constexpr auto pieceCount() const
    {
        return blockInfo().pieceCount();
    }
    [[nodiscard]] constexpr auto pieceSize() const
    {
        return blockInfo().pieceSize();
    }
    [[nodiscard]] constexpr auto pieceSize(tr_piece_index_t piece) const
    {
        return blockInfo().pieceSize(piece);
    }
    [[nodiscard]] constexpr auto totalSize() const
    {
        return blockInfo().totalSize();
    }

    [[nodiscard]] auto const& comment() const
    {
        return comment_;
    }
    [[nodiscard]] auto const& creator() const
    {
        return creator_;
    }
    [[nodiscard]] auto const& source() const
    {
        return source_;
    }

    [[nodiscard]] auto fileCount() const
    {
        return std::size(files_);
    }

    [[nodiscard]] std::string const& fileSubpath(tr_file_index_t i) const;

    void setFileSubpath(tr_file_index_t i, std::string_view subpath);

    [[nodiscard]] uint64_t fileSize(tr_file_index_t i) const;

    [[nodiscard]] auto const& isPrivate() const
    {
        return is_private_;
    }

    [[nodiscard]] tr_sha1_digest_t const& pieceHash(tr_piece_index_t piece) const;

    [[nodiscard]] auto const& dateCreated() const
    {
        return date_created_;
    }

    [[nodiscard]] std::string benc() const;

    [[nodiscard]] auto infoDictSize() const
    {
        return info_dict_size_;
    }

    [[nodiscard]] auto infoDictOffset() const
    {
        return info_dict_offset_;
    }

    [[nodiscard]] auto piecesOffset() const
    {
        return pieces_offset_;
    }

    [[nodiscard]] std::string torrentFile(std::string_view torrent_dir) const
    {
        return makeFilename(torrent_dir, name(), infoHashString(), BasenameFormat::Hash, ".torrent");
    }

    [[nodiscard]] std::string magnetFile(std::string_view torrent_dir) const
    {
        return makeFilename(torrent_dir, name(), infoHashString(), BasenameFormat::Hash, ".magnet");
    }

    [[nodiscard]] std::string resumeFile(std::string_view resume_dir) const
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

    static std::string makeFilename(
        std::string_view dirname,
        std::string_view name,
        std::string_view info_hash_string,
        BasenameFormat format,
        std::string_view suffix);

    [[nodiscard]] std::string makeFilename(std::string_view dirname, BasenameFormat format, std::string_view suffix) const
    {
        return makeFilename(dirname, name(), infoHashString(), format, suffix);
    }

    struct file_t
    {
    public:
        [[nodiscard]] std::string const& path() const
        {
            return path_;
        }

        void setSubpath(std::string_view subpath)
        {
            path_ = subpath;
        }

        [[nodiscard]] uint64_t size() const
        {
            return size_;
        }

        file_t(std::string_view path, uint64_t size)
            : path_{ path }
            , size_{ size }
        {
        }

    private:
        std::string path_;
        uint64_t size_ = 0;
    };

    tr_block_info block_info_ = tr_block_info{ 0, 0 };

    std::vector<tr_sha1_digest_t> pieces_;
    std::vector<file_t> files_;

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

    bool is_private_ = false;
};
