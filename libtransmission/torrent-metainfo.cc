// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>

#include "transmission.h"

#include "benc.h"
#include "crypto-utils.h"
#include "error-types.h"
#include "error.h"
#include "file-info.h"
#include "file.h"
#include "log.h"
#include "quark.h"
#include "torrent-metainfo.h"
#include "tr-assert.h"
#include "utils.h"
#include "web-utils.h"

using namespace std::literals;

//// C BINDINGS

#if 0
/// Lifecycle

tr_torrent_metainfo* tr_torrentMetainfoNewFromData(char const* data, size_t data_len, struct tr_error** error)
{
    auto* tm = new tr_torrent_metainfo{};
    if (!tm->parseBenc(std::string_view{ data, data_len }, error))
    {
        delete tm;
        return nullptr;
    }

    return tm;
}

tr_torrent_metainfo* tr_torrentMetainfoNewFromFile(char const* filename, struct tr_error** error)
{
    auto* tm = new tr_torrent_metainfo{};
    if (!tm->parseBencFromFile(filename ? filename : "", nullptr, error))
    {
        delete tm;
        return nullptr;
    }

    return tm;
}

void tr_torrentMetainfoFree(tr_torrent_metainfo* tm)
{
    delete tm;
}

////  Accessors

char* tr_torrentMetainfoMagnet(struct tr_torrent_metainfo const* tm)
{
    return tr_strvDup(tm->magnet());
}

/// Info

tr_torrent_metainfo_info* tr_torrentMetainfoGet(tr_torrent_metainfo const* tm, tr_torrent_metainfo_info* setme)
{
    setme->comment = tm->comment.c_str();
    setme->creator = tm->creator.c_str();
    setme->info_hash = tm->info_hash;
    setme->info_hash_string = std::data(tm->info_hash_chars);
    setme->is_private = tm->is_private;
    setme->n_pieces = tm->n_pieces;
    setme->name = tm->name.c_str();
    setme->source = tm->source.c_str();
    setme->time_created = tm->time_created;
    setme->total_size = tm->total_size;
    return setme;
}

/// Files

size_t tr_torrentMetainfoFileCount(tr_torrent_metainfo const* tm)
{
    return std::size(tm->files);
}

tr_torrent_metainfo_file_info* tr_torrentMetainfoFile(
    tr_torrent_metainfo const* tm,
    size_t n,
    tr_torrent_metainfo_file_info* setme)
{
    auto& file = tm->files[n];
    setme->path = file.path.c_str();
    setme->size = file.size;
    return setme;
}

/// Trackers

size_t tr_torrentMetainfoTrackerCount(tr_torrent_metainfo const* tm)
{
    return std::size(tm->trackers);
}

tr_torrent_metainfo_tracker_info* tr_torrentMetainfoTracker(
    tr_torrent_metainfo const* tm,
    size_t n,
    tr_torrent_metainfo_tracker_info* setme)
{
    auto it = std::begin(tm->trackers);
    std::advance(it, n);
    auto const& tracker = it->second;
    setme->announce_url = tr_quark_get_string(tracker.announce_url);
    setme->scrape_url = tr_quark_get_string(tracker.scrape_url);
    setme->tier = tracker.tier;
    return setme;
}
#endif

/***
****
***/

/**
 * @brief Ensure that the URLs for multfile torrents end in a slash.
 *
 * See http://bittorrent.org/beps/bep_0019.html#metadata-extension
 * for background on how the trailing slash is used for "url-list"
 * fields.
 *
 * This function is to workaround some .torrent generators, such as
 * mktorrent and very old versions of utorrent, that don't add the
 * trailing slash for multifile torrents if omitted by the end user.
 */
std::string tr_torrent_metainfo::fixWebseedUrl(tr_torrent_metainfo const& tm, std::string_view url)
{
    url = tr_strvStrip(url);

    if (std::size(tm.files_) > 1 && !std::empty(url) && url.back() != '/')
    {
        return std::string{ url } + '/';
    }

    return std::string{ url };
}

static auto constexpr MaxBencDepth = 32;
static auto constexpr PathMax = 4096;
using tr_membuf = fmt::basic_memory_buffer<char, 4096>;

struct MetainfoHandler final : public transmission::benc::BasicHandler<MaxBencDepth>
{
    using BasicHandler = transmission::benc::BasicHandler<MaxBencDepth>;

    tr_torrent_metainfo& tm_;
    int64_t piece_size_ = 0;
    int64_t length_ = 0;
    std::string encoding_ = "UTF-8";
    std::string_view info_dict_begin_;
    tr_tracker_tier_t tier_ = 0;
    // TODO: can we have a recycled std::string to avoid excess heap allocation
    std::vector<std::string> file_tree_;
    std::string_view pieces_root_;
    int64_t file_length_ = 0;

    enum class State
    {
        Top,
        Info,
        FileTree,
        Files,
        FilesIgnored,
        PieceLayers,
    };
    State state_ = State::Top;

    explicit MetainfoHandler(tr_torrent_metainfo& tm)
        : tm_{ tm }
    {
    }

    bool Key(std::string_view key, Context const& context) override
    {
        BasicHandler::Key(key, context);
        return true;
    }

    bool StartDict(Context const& context) override
    {
        BasicHandler::StartDict(context);

        if (state_ == State::FileTree)
        {
            if (auto token = tr_file_info::sanitizePath(key(depth() - 1)); !std::empty(token))
            {
                file_tree_.emplace_back(std::move(token));
            }
        }
        else if (state_ == State::Top && depth() == 2)
        {
            if (key(1) == "info"sv)
            {
                info_dict_begin_ = context.raw();
                tm_.info_dict_offset_ = context.tokenSpan().first;
                state_ = State::Info;
            }
            else if (key(1) == "piece layers"sv)
            {
                state_ = State::PieceLayers;
            }
        }
        else if (state_ == State::Info && key(depth() - 1) == "file tree"sv)
        {
            state_ = State::FileTree;
            file_tree_.clear();
            file_length_ = 0;
        }

        return true;
    }

    bool EndDict(Context const& context) override
    {
        BasicHandler::EndDict(context);

        if (depth() == 0) // top
        {
            return finish(context);
        }

        if (state_ == State::Info && key(depth()) == "info"sv)
        {
            state_ = State::Top;
            return finishInfoDict(context);
        }

        if (state_ == State::PieceLayers && key(depth()) == "piece layers"sv)
        {
            state_ = State::Top;
            return true;
        }

        if (state_ == State::FileTree) // bittorrent v2 format
        {
            if (!addFile(context))
            {
                return false;
            }

            if (auto const n = std::size(file_tree_); n > 0)
            {
                file_tree_.resize(n - 1);
            }

            if (key(depth()) == "file tree"sv)
            {
                state_ = State::Info;
            }

            return true;
        }

        if (state_ == State::Files) // bittorrent v1 format
        {
            if (!addFile(context))
            {
                return false;
            }

            file_tree_.clear();
            return true;
        }

        return depth() > 0;
    }

    bool StartArray(Context const& context) override
    {
        BasicHandler::StartArray(context);

        if (state_ == State::Info && key(depth() - 1) == "files"sv)
        {
            if (!std::empty(tm_.files_))
            {
                state_ = State::FilesIgnored;
            }
            else
            {
                state_ = State::Files;
                file_tree_.clear();
                file_length_ = 0;
            }
        }
        return true;
    }

    bool EndArray(Context const& context) override
    {
        BasicHandler::EndArray(context);

        if ((state_ == State::Files || state_ == State::FilesIgnored) && key(depth()) == "files"sv) // bittorrent v1 format
        {
            state_ = State::Info;
            return true;
        }

        if (depth() == 2 && key(1) == "announce-list")
        {
            ++tier_;
        }

        return true;
    }

    bool Int64(int64_t value, Context const& /*context*/) override
    {
        auto const curdepth = depth();
        auto const curkey = currentKey();
        auto unhandled = bool{ false };

        if (state_ == State::FilesIgnored)
        {
            // no-op
        }
        else if (curdepth == 1)
        {
            if (curkey == "creation date"sv)
            {
                tm_.date_created_ = value;
            }
            else if (curkey == "private"sv)
            {
                tm_.is_private_ = value != 0;
            }
            else if (curkey == "piece length"sv)
            {
                piece_size_ = value;
            }
            else
            {
                unhandled = true;
            }
        }
        else if (curdepth == 2 && key(1) == "info"sv)
        {
            if (curkey == "piece length"sv)
            {
                piece_size_ = value;
            }
            else if (curkey == "private"sv)
            {
                tm_.is_private_ = value != 0;
            }
            else if (curkey == "length"sv)
            {
                length_ = value;
            }
            else if (curkey == "meta version")
            {
                // currently unused. TODO support for bittorrent v2
                // TODO https://github.com/transmission/transmission/issues/458
            }
            else
            {
                unhandled = true;
            }
        }
        else if (state_ == State::FileTree || state_ == State::Files)
        {
            if (curkey == "length"sv)
            {
                file_length_ = value;
            }
            else
            {
                unhandled = true;
            }
        }
        else
        {
            unhandled = true;
        }

        if (unhandled)
        {
            tr_logAddWarn(fmt::format("unexpected: key '{}', int '{}'", curkey, value));
        }

        return true;
    }

    bool String(std::string_view value, Context const& context) override
    {
        auto const curdepth = depth();
        auto const curkey = currentKey();
        auto unhandled = bool{ false };

        if (state_ == State::FilesIgnored)
        {
            // no-op
        }
        else if (state_ == State::FileTree)
        {
            if (curkey == "attr"sv || curkey == "pieces root"sv)
            {
                // currently unused. TODO support for bittorrent v2
                // TODO https://github.com/transmission/transmission/issues/458
            }
            else
            {
                unhandled = true;
            }
        }
        else if (state_ == State::Files)
        {
            if (curdepth > 1 && key(curdepth - 1) == "path"sv)
            {
                file_tree_.emplace_back(tr_file_info::sanitizePath(value));
            }
            else if (curkey == "attr"sv)
            {
                // currently unused. TODO support for bittorrent v2
                // TODO https://github.com/transmission/transmission/issues/458
            }
            else
            {
                unhandled = true;
            }
        }
        else
        {
            switch (curdepth)
            {
            case 1:
                if (curkey == "comment"sv || curkey == "comment.utf-8"sv)
                {
                    tr_strvUtf8Clean(value, tm_.comment_);
                }
                else if (curkey == "created by"sv || curkey == "created by.utf-8"sv)
                {
                    tr_strvUtf8Clean(value, tm_.creator_);
                }
                else if (curkey == "source"sv)
                {
                    tr_strvUtf8Clean(value, tm_.source_);
                }
                else if (curkey == "announce"sv)
                {
                    tm_.announceList().add(value, tier_);
                }
                else if (curkey == "encoding"sv)
                {
                    encoding_ = tr_strvStrip(value);
                }
                else if (curkey == "url-list"sv)
                {
                    tm_.addWebseed(value);
                }
                else
                {
                    unhandled = true;
                }
                break;

            case 2:
                if (key(1) == "info"sv && curkey == "source"sv)
                {
                    tr_strvUtf8Clean(value, tm_.source_);
                }
                else if (key(1) == "httpseeds"sv || key(1) == "url-list"sv)
                {
                    tm_.addWebseed(value);
                }
                else if (key(1) == "info"sv && curkey == "pieces"sv)
                {
                    auto const n = std::size(value) / sizeof(tr_sha1_digest_t);
                    tm_.pieces_.resize(n);
                    std::copy_n(std::data(value), std::size(value), reinterpret_cast<char*>(std::data(tm_.pieces_)));
                    tm_.pieces_offset_ = context.tokenSpan().first;
                }
                else if (key(1) == "info"sv && (curkey == "name"sv || curkey == "name.utf-8"sv))
                {
                    tr_strvUtf8Clean(value, tm_.name_);

                    auto membuf = tr_membuf{};
                    auto const token = tr_file_info::sanitizePath(value);
                    if (!std::empty(token))
                    {
                        for (auto& file : tm_.files_)
                        {
                            membuf.clear();
                            membuf.append(token);
                            membuf.append("/"sv);
                            membuf.append(file.path());
                            file.setSubpath(fmt::to_string(membuf));
                        }
                    }
                }
                else if (key(1) == "piece layers"sv)
                {
                    // currently unused. TODO support for bittorrent v2
                    // TODO https://github.com/transmission/transmission/issues/458
                }
                else
                {
                    unhandled = true;
                }
                break;

            case 3:
                if (key(1) == "announce-list")
                {
                    tm_.announceList().add(value, tier_);
                }
                else
                {
                    unhandled = true;
                }
                break;

            default:
                unhandled = true;
                break;
            }
        }

        if (unhandled)
        {
            tr_logAddWarn(fmt::format("unexpected: key '{}', str '{}'", curkey, value));
        }

        return true;
    }

private:
    [[nodiscard]] bool addFile(Context const& context)
    {
        bool ok = true;

        if (file_length_ == 0)
        {
            return ok;
        }

        // Check to see if we already added this file. This is a safeguard for
        // hybrid torrents with duplicate info between "file tree" and "files"
        if (auto const filename = buildPath(); std::empty(filename))
        {
            auto const errmsg = tr_strvJoin("invalid path ["sv, filename, "]"sv);
            tr_error_set(context.error, EINVAL, errmsg);
            ok = false;
        }
        else
        {
            tm_.files_.emplace_back(filename, file_length_);
        }

        file_length_ = 0;
        pieces_root_ = {};
        // NB: let caller decide how to clear file_tree_.
        // if we're in "files" mode we clear it; if in "file tree" we pop it
        return ok;
    }

    [[nodiscard]] std::string buildPath() const
    {
        auto path = tr_membuf{};

        for (auto const& token : file_tree_)
        {
            path.append(token);

            if (!std::empty(token))
            {
                path.append("/"sv);
            }
        }

        auto const n = std::size(path);
        if (n > 0)
        {
            path.resize(n - 1);
        }

        return fmt::to_string(path);
    }

    bool finishInfoDict(Context const& context)
    {
        if (std::empty(info_dict_begin_))
        {
            tr_error_set(context.error, EINVAL, "no info_dict found");
            return false;
        }

        TR_ASSERT(info_dict_begin_[0] == 'd');
        TR_ASSERT(context.raw().back() == 'e');
        char const* const begin = &info_dict_begin_.front();
        char const* const end = &context.raw().back() + 1;
        auto const info_dict_benc = std::string_view{ begin, size_t(end - begin) };
        auto const hash = tr_sha1(info_dict_benc);
        if (!hash)
        {
            tr_error_set(context.error, EINVAL, "bad info_dict checksum");
        }
        tm_.info_hash_ = *hash;
        tm_.info_hash_str_ = tr_sha1_to_string(tm_.info_hash_);
        tm_.info_dict_size_ = std::size(info_dict_benc);

        return true;
    }

    bool finish(Context const& context)
    {
        // bittorrent 1.0 spec
        // http://bittorrent.org/beps/bep_0003.html
        //
        // "There is also a key length or a key files, but not both or neither.
        //
        // "If length is present then the download represents a single file,
        // otherwise it represents a set of files which go in a directory structure.
        // In the single file case, length maps to the length of the file in bytes.
        if (tm_.fileCount() == 0 && length_ != 0 && !std::empty(tm_.name_))
        {
            tm_.files_.emplace_back(tm_.name_, length_);
        }

        if (tm_.fileCount() == 0)
        {
            if (!tr_error_is_set(context.error))
            {
                tr_error_set(context.error, EINVAL, "no files found");
            }
            return false;
        }

        if (piece_size_ == 0)
        {
            auto const errmsg = tr_strvJoin("invalid piece size: ", std::to_string(piece_size_));
            if (!tr_error_is_set(context.error))
            {
                tr_error_set(context.error, EINVAL, errmsg);
            }
            return false;
        }

        auto const total_size = std::accumulate(
            std::begin(tm_.files_),
            std::end(tm_.files_),
            uint64_t{ 0 },
            [](auto const sum, auto const& file) { return sum + file.size(); });
        tm_.block_info_.initSizes(total_size, piece_size_);
        return true;
    }
};

bool tr_torrent_metainfo::parseBenc(std::string_view benc, tr_error** error)
{
    auto stack = transmission::benc::ParserStack<MaxBencDepth>{};
    auto handler = MetainfoHandler{ *this };

    tr_error* my_error = nullptr;

    if (error == nullptr)
    {
        error = &my_error;
    }
    auto const ok = transmission::benc::parse(benc, stack, handler, nullptr, error);

    if (tr_error_is_set(error))
    {
        tr_logAddError(fmt::format("{} ({})", (*error)->message, (*error)->code));
    }

    if (!ok)
    {
        return false;
    }

    if (std::empty(name_))
    {
        // TODO from first file
    }

    return true;
}

bool tr_torrent_metainfo::parseTorrentFile(std::string_view filename, std::vector<char>* contents, tr_error** error)
{
    auto local_contents = std::vector<char>{};

    if (contents == nullptr)
    {
        contents = &local_contents;
    }

    auto const sz_filename = std::string{ filename };
    return tr_loadFile(*contents, sz_filename, error) && parseBenc({ std::data(*contents), std::size(*contents) }, error);
}

tr_sha1_digest_t const& tr_torrent_metainfo::pieceHash(tr_piece_index_t piece) const
{
    return this->pieces_[piece];
}

std::string tr_torrent_metainfo::makeFilename(
    std::string_view dirname,
    std::string_view name,
    std::string_view info_hash_string,
    BasenameFormat format,
    std::string_view suffix)
{
    // `${dirname}/${name}.${info_hash}${suffix}`
    // `${dirname}/${info_hash}${suffix}`
    return format == BasenameFormat::Hash ? tr_strvJoin(dirname, "/"sv, info_hash_string, suffix) :
                                            tr_strvJoin(dirname, "/"sv, name, "."sv, info_hash_string.substr(0, 16), suffix);
}

bool tr_torrent_metainfo::migrateFile(
    std::string_view dirname,
    std::string_view name,
    std::string_view info_hash_string,
    std::string_view suffix)
{
    auto const old_filename = makeFilename(dirname, name, info_hash_string, BasenameFormat::NameAndPartialHash, suffix);
    auto const old_filename_exists = tr_sys_path_exists(old_filename.c_str(), nullptr);
    auto const new_filename = makeFilename(dirname, name, info_hash_string, BasenameFormat::Hash, suffix);
    auto const new_filename_exists = tr_sys_path_exists(new_filename.c_str(), nullptr);

    if (old_filename_exists && new_filename_exists)
    {
        tr_sys_path_remove(old_filename.c_str(), nullptr);
        return false;
    }

    if (new_filename_exists)
    {
        return false;
    }

    if (old_filename_exists && tr_sys_path_rename(old_filename.c_str(), new_filename.c_str(), nullptr))
    {
        tr_logAddError(
            fmt::format(
                _("Migrated torrent file from '{old_path}' to '{path}'"),
                fmt::arg("old_path", old_filename),
                fmt::arg("path", new_filename)),
            name);
        return true;
    }

    return false; // neither file exists
}

void tr_torrent_metainfo::removeFile(
    std::string_view dirname,
    std::string_view name,
    std::string_view info_hash_string,
    std::string_view suffix)
{
    auto filename = makeFilename(dirname, name, info_hash_string, BasenameFormat::NameAndPartialHash, suffix);
    tr_sys_path_remove(filename.c_str(), nullptr);

    filename = makeFilename(dirname, name, info_hash_string, BasenameFormat::Hash, suffix);
    tr_sys_path_remove(filename.c_str(), nullptr);
}

std::string const& tr_torrent_metainfo::fileSubpath(tr_file_index_t i) const
{
    TR_ASSERT(i < fileCount());

    return files_.at(i).path();
}

void tr_torrent_metainfo::setFileSubpath(tr_file_index_t i, std::string_view subpath)
{
    TR_ASSERT(i < fileCount());

    files_.at(i).setSubpath(subpath);
}

uint64_t tr_torrent_metainfo::fileSize(tr_file_index_t i) const
{
    TR_ASSERT(i < fileCount());

    return files_.at(i).size();
}
