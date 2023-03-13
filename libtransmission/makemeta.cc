// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno> // for ENOENT
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "transmission.h"

#include "crypto-utils.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "makemeta.h"
#include "session.h" // TR_NAME
#include "tr-assert.h"
#include "utils.h" // for _()
#include "variant.h"
#include "version.h"

using namespace std::literals;

namespace
{

namespace find_files_helpers
{

struct TorrentFile
{
    TorrentFile(std::string_view subpath, size_t size)
        : subpath_{ subpath }
        , lowercase_{ tr_strlower(subpath) }
        , size_{ size }
    {
    }

    [[nodiscard]] auto operator<(TorrentFile const& that) const noexcept
    {
        return lowercase_ < that.lowercase_;
    }

    std::string subpath_;
    std::string lowercase_;
    uint64_t size_ = 0;
};

void walkTree(std::string_view const top, std::string_view const subpath, std::set<TorrentFile>& files)
{
    TR_ASSERT(!std::empty(top));
    TR_ASSERT(!std::empty(subpath));

    if (std::empty(top) || std::empty(subpath))
    {
        return;
    }

    auto path = tr_pathbuf{ top, '/', subpath };
    tr_sys_path_native_separators(std::data(path));
    tr_error* error = nullptr;
    auto const info = tr_sys_path_get_info(path, 0, &error);
    if (error != nullptr)
    {
        tr_logAddWarn(fmt::format(
            _("Skipping '{path}': {error} ({error_code})"),
            fmt::arg("path", path),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_free(error);
    }
    if (!info)
    {
        return;
    }

    switch (info->type)
    {
    case TR_SYS_PATH_IS_DIRECTORY:
        if (tr_sys_dir_t odir = tr_sys_dir_open(path.c_str()); odir != TR_BAD_SYS_DIR)
        {
            for (;;)
            {
                char const* const name = tr_sys_dir_read_name(odir);

                if (name == nullptr)
                {
                    break;
                }

                if (name[0] == '.') // skip dotfiles
                {
                    continue;
                }

                if (!std::empty(subpath))
                {
                    walkTree(top, tr_pathbuf{ subpath, '/', name }, files);
                }
                else
                {
                    walkTree(top, name, files);
                }
            }

            tr_sys_dir_close(odir);
        }
        break;

    case TR_SYS_PATH_IS_FILE:
        files.emplace(subpath, info->size);
        break;

    default:
        break;
    }
}

} // namespace find_files_helpers

tr_torrent_files findFiles(std::string_view const top, std::string_view const subpath)
{
    using namespace find_files_helpers;

    auto tmp = std::set<TorrentFile>{};
    walkTree(top, subpath, tmp);
    auto files = tr_torrent_files{};
    for (auto const& file : tmp)
    {
        files.add(file.subpath_, file.size_);
    }
    return files;
}

} // namespace

tr_metainfo_builder::tr_metainfo_builder(std::string_view single_file_or_parent_directory)
    : top_{ single_file_or_parent_directory }
{
    files_ = findFiles(tr_sys_path_dirname(top_), tr_sys_path_basename(top_));
    block_info_ = tr_block_info{ files_.totalSize(), defaultPieceSize(files_.totalSize()) };
}

bool tr_metainfo_builder::isLegalPieceSize(uint32_t x)
{
    // It must be a power of two and at least 16KiB
    static auto constexpr MinSize = uint32_t{ 1024U * 16U };
    auto const is_power_of_two = (x & (x - 1)) == 0;
    return x >= MinSize && is_power_of_two;
}

bool tr_metainfo_builder::setPieceSize(uint32_t piece_size) noexcept
{
    if (!isLegalPieceSize(piece_size))
    {
        return false;
    }

    block_info_ = tr_block_info{ files_.totalSize(), piece_size };
    return true;
}

bool tr_metainfo_builder::blockingMakeChecksums(tr_error** error)
{
    checksum_piece_ = 0;
    cancel_ = false;

    if (totalSize() == 0U)
    {
        tr_error_set_from_errno(error, ENOENT);
        return false;
    }

    auto hashes = std::vector<std::byte>(std::size(tr_sha1_digest_t{}) * pieceCount());
    auto* walk = std::data(hashes);
    auto sha = tr_sha1::create();

    auto file_index = tr_file_index_t{ 0U };
    auto piece_index = tr_piece_index_t{ 0U };
    auto total_remain = totalSize();
    auto off = uint64_t{ 0U };

    auto buf = std::vector<char>(pieceSize());

    auto const parent = tr_sys_path_dirname(top_);
    auto fd = tr_sys_file_open(
        tr_pathbuf{ parent, '/', path(file_index) },
        TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL,
        0,
        error);
    if (fd == TR_BAD_SYS_FILE)
    {
        return false;
    }

    while (!cancel_ && (total_remain > 0U))
    {
        checksum_piece_ = piece_index;

        TR_ASSERT(piece_index < pieceCount());

        uint32_t const piece_size = block_info_.pieceSize(piece_index);
        buf.resize(piece_size);
        auto* bufptr = std::data(buf);

        auto left_in_piece = piece_size;
        while (left_in_piece > 0U)
        {
            auto const n_this_pass = std::min(fileSize(file_index) - off, uint64_t{ left_in_piece });
            auto n_read = uint64_t{};

            (void)tr_sys_file_read(fd, bufptr, n_this_pass, &n_read, error);
            bufptr += n_read;
            off += n_read;
            left_in_piece -= n_read;

            if (off == fileSize(file_index))
            {
                off = 0;
                tr_sys_file_close(fd);
                fd = TR_BAD_SYS_FILE;

                if (++file_index < fileCount())
                {
                    fd = tr_sys_file_open(
                        tr_pathbuf{ parent, '/', path(file_index) },
                        TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL,
                        0,
                        error);
                    if (fd == TR_BAD_SYS_FILE)
                    {
                        return false;
                    }
                }
            }
        }

        TR_ASSERT(bufptr - std::data(buf) == (int)piece_size);
        TR_ASSERT(left_in_piece == 0);
        sha->add(std::data(buf), std::size(buf));
        auto const digest = sha->finish();
        walk = std::copy(std::begin(digest), std::end(digest), walk);
        sha->clear();

        total_remain -= piece_size;
        ++piece_index;
    }

    TR_ASSERT(cancel_ || size_t(walk - std::data(hashes)) == std::size(hashes));
    TR_ASSERT(cancel_ || total_remain == 0U);

    if (fd != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(fd);
    }

    if (cancel_)
    {
        tr_error_set_from_errno(error, ECANCELED);
        return false;
    }

    piece_hashes_ = std::move(hashes);
    return true;
}

std::string tr_metainfo_builder::benc(tr_error** error) const
{
    TR_ASSERT_MSG(!std::empty(piece_hashes_), "did you forget to call makeChecksums() first?");

    auto const anonymize = this->anonymize();
    auto const& comment = this->comment();
    auto const& source = this->source();
    auto const& webseeds = this->webseeds();

    if (totalSize() == 0)
    {
        tr_error_set_from_errno(error, ENOENT);
        return {};
    }

    auto top = tr_variant{};
    tr_variantInitDict(&top, 8);

    // add the announce URLs
    if (!std::empty(announceList()))
    {
        tr_variantDictAddStrView(&top, TR_KEY_announce, announceList().at(0).announce.sv());
    }
    if (std::size(announceList()) > 1U)
    {
        auto* const announce_list = tr_variantDictAddList(&top, TR_KEY_announce_list, 0);
        tr_variant* tier_list = nullptr;
        auto prev_tier = std::optional<tr_tracker_tier_t>{};
        for (auto const& tracker : announceList())
        {
            if (!prev_tier || *prev_tier != tracker.tier)
            {
                tier_list = nullptr;
            }

            if (tier_list == nullptr)
            {
                prev_tier = tracker.tier;
                tier_list = tr_variantListAddList(announce_list, 0);
            }

            tr_variantListAddStr(tier_list, tracker.announce);
        }
    }

    // add the webseeds
    if (!std::empty(webseeds))
    {
        auto* const url_list = tr_variantDictAddList(&top, TR_KEY_url_list, std::size(webseeds));

        for (auto const& webseed : webseeds)
        {
            tr_variantListAddStr(url_list, webseed);
        }
    }

    // add the comment
    if (!std::empty(comment))
    {
        tr_variantDictAddStr(&top, TR_KEY_comment, comment);
    }

    // maybe add some optional metainfo
    if (!anonymize)
    {
        tr_variantDictAddStrView(&top, TR_KEY_created_by, TR_NAME "/" LONG_VERSION_STRING);
        tr_variantDictAddInt(&top, TR_KEY_creation_date, time(nullptr));
    }

    tr_variantDictAddStrView(&top, TR_KEY_encoding, "UTF-8");

    auto* const info_dict = tr_variantDictAddDict(&top, TR_KEY_info, 5);
    auto const base = tr_sys_path_basename(top_);

    // "There is also a key `length` or a key `files`, but not both or neither.
    // If length is present then the download represents a single file,
    // otherwise it represents a set of files which go in a directory structure."
    if (fileCount() == 1U && !tr_strvContains(path(0), '/'))
    {
        tr_variantDictAddInt(info_dict, TR_KEY_length, fileSize(0));
    }
    else
    {
        auto const n_files = fileCount();
        auto* const file_list = tr_variantDictAddList(info_dict, TR_KEY_files, n_files);

        for (tr_file_index_t i = 0; i < n_files; ++i)
        {
            auto* const file_dict = tr_variantListAddDict(file_list, 2);
            tr_variantDictAddInt(file_dict, TR_KEY_length, fileSize(i));

            auto subpath = std::string_view{ path(i) };
            if (!std::empty(base))
            {
                subpath.remove_prefix(std::size(base) + std::size("/"sv));
            }

            auto* const path_list = tr_variantDictAddList(file_dict, TR_KEY_path, 0);
            auto token = std::string_view{};
            while (tr_strvSep(&subpath, &token, '/'))
            {
                tr_variantListAddStr(path_list, token);
            }
        }
    }

    if (!std::empty(base))
    {
        tr_variantDictAddStr(info_dict, TR_KEY_name, base);
    }

    tr_variantDictAddInt(info_dict, TR_KEY_piece_length, pieceSize());
    tr_variantDictAddRaw(info_dict, TR_KEY_pieces, std::data(piece_hashes_), std::size(piece_hashes_));

    if (is_private_)
    {
        tr_variantDictAddInt(info_dict, TR_KEY_private, 1);
    }

    if (!std::empty(source))
    {
        tr_variantDictAddStr(info_dict, TR_KEY_source, source_);
    }

    auto ret = tr_variantToStr(&top, TR_VARIANT_FMT_BENC);
    tr_variantClear(&top);
    return ret;
}
