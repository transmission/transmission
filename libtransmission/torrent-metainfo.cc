// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno> // for EINVAL
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
#include "file.h"
#include "log.h"
#include "quark.h"
#include "torrent-metainfo.h"
#include "tr-assert.h"
#include "tr-strbuf.h"
#include "utils.h"
#include "web-utils.h"

using namespace std::literals;

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

    if (tm.fileCount() > 1U && !std::empty(url) && url.back() != '/')
    {
        return std::string{ url } + '/';
    }

    return std::string{ url };
}

namespace
{
auto constexpr MaxBencDepth = 32;

bool tr_error_is_set(tr_error const* const* error)
{
    return (error != nullptr) && (*error != nullptr);
}
} // namespace

struct MetainfoHandler final : public transmission::benc::BasicHandler<MaxBencDepth>
{
    using BasicHandler = transmission::benc::BasicHandler<MaxBencDepth>;

    tr_torrent_metainfo& tm_;
    int64_t piece_size_ = 0;
    int64_t length_ = 0;
    std::string encoding_ = "UTF-8";
    std::string_view info_dict_begin_;
    tr_tracker_tier_t tier_ = 0;
    tr_pathbuf file_subpath_;
    std::string_view pieces_root_;
    int64_t file_length_ = 0;

    enum class State
    {
        UsePath,
        FileTree,
        Files,
        FilesIgnored,
        PieceLayers,
    };
    State state_ = State::UsePath;

    explicit MetainfoHandler(tr_torrent_metainfo& tm)
        : tm_{ tm }
    {
    }

    bool Key(std::string_view key, Context const& context) override
    {
        return BasicHandler::Key(key, context);
    }

    bool StartDict(Context const& context) override
    {
        if (state_ == State::FileTree)
        {
            if (!std::empty(file_subpath_))
            {
                file_subpath_ += '/';
            }
            tr_torrent_files::makeSubpathPortable(currentKey(), file_subpath_);
        }
        else if (pathIs(InfoKey))
        {
            info_dict_begin_ = context.raw();
            tm_.info_dict_offset_ = context.tokenSpan().first;
        }
        else if (pathIs(InfoKey, FileTreeKey))
        {
            state_ = State::FileTree;
            file_subpath_.clear();
            file_length_ = 0;
        }
        else if (pathIs(PieceLayersKey))
        {
            state_ = State::PieceLayers;
        }

        return BasicHandler::StartDict(context);
    }

    bool EndDict(Context const& context) override
    {
        BasicHandler::EndDict(context);

        if (pathIs(InfoKey))
        {
            return finishInfoDict(context);
        }

        if (depth() == 0) // top
        {
            return finish(context);
        }

        if (state_ == State::FileTree) // bittorrent v2 format
        {
            // v2, ignore for today
            tr_logAddInfo("'file tree' is ignored");
            state_ = State::UsePath;
        }
        else if (state_ == State::Files) // bittorrent v1 format
        {
            if (!addFile(context))
            {
                return false;
            }

            file_subpath_.clear();
        }
        else if (state_ == State::PieceLayers)
        {
            state_ = State::UsePath;
        }

        return depth() > 0;
    }

    bool StartArray(Context const& context) override
    {
        if (pathIs(InfoKey, FilesKey))
        {
            state_ = std::empty(tm_.files_) ? State::Files : State::FilesIgnored;
            file_subpath_.clear();
            file_length_ = 0;
        }
        else if (pathStartsWith(InfoKey, FilesKey, ""sv, PathUtf8Key))
        {
            // torrent has a utf8 path, drop the other one due to probable non-utf8 encoding
            file_subpath_.clear();
        }

        return BasicHandler::StartArray(context);
    }

    bool EndArray(Context const& context) override
    {
        BasicHandler::EndArray(context);

        if ((state_ == State::Files || state_ == State::FilesIgnored) && currentKey() == FilesKey) // bittorrent v1 format
        {
            state_ = State::UsePath;
            return true;
        }

        if (depth() == 2 && key(1) == AnnounceListKey)
        {
            ++tier_;
        }

        return true;
    }

    bool Int64(int64_t value, Context const& /*context*/) override
    {
        auto unhandled = bool{ false };

        if (state_ == State::FilesIgnored)
        {
            // no-op
        }
        else if (state_ == State::FileTree || state_ == State::Files)
        {
            if (currentKey() == LengthKey)
            {
                file_length_ = value;
            }
            else if (pathIs(InfoKey, FilesKey, ""sv, MtimeKey))
            {
                // unused by Transmission
            }
            else
            {
                unhandled = true;
            }
        }
        else if (pathIs(CreationDateKey) || pathIs(InfoKey, CreationDateKey))
        {
            tm_.date_created_ = value;
        }
        else if (pathIs(PrivateKey) || pathIs(InfoKey, PrivateKey))
        {
            tm_.is_private_ = value != 0;
        }
        else if (pathIs(PieceLengthKey) || pathIs(InfoKey, PieceLengthKey))
        {
            piece_size_ = value;
        }
        else if (pathIs(InfoKey, LengthKey))
        {
            length_ = value;
        }
        else if (pathIs(InfoKey, MetaVersionKey))
        {
            // currently unused. TODO support for bittorrent v2
            // TODO https://github.com/transmission/transmission/issues/458
            tm_.is_v2_ = value == 2;
        }
        else if (
            pathIs(DurationKey) || //
            pathIs(EncodedRateKey) || //
            pathIs(HeightKey) || //
            pathIs(InfoKey, EntropyKey) || //
            pathIs(InfoKey, UniqueKey) || //
            pathIs(ProfilesKey, HeightKey) || //
            pathIs(ProfilesKey, WidthKey) || //
            pathIs(WidthKey) || //
            pathStartsWith(AzureusPropertiesKey) || //
            pathStartsWith(InfoKey, FileDurationKey) || //
            pathStartsWith(InfoKey, FileMediaKey) || //
            pathStartsWith(InfoKey, ProfilesKey) || //
            pathStartsWith(LibtorrentResumeKey) || //
            pathStartsWith(NodesKey))
        {
            // unused by Transmission
        }
        else
        {
            unhandled = true;
        }

        if (unhandled)
        {
            tr_logAddWarn(fmt::format("unexpected: path '{}', int '{}'", path(), value));
        }

        return true;
    }

    bool String(std::string_view value, Context const& context) override
    {
        auto const curdepth = depth();
        auto const current_key = currentKey();
        auto unhandled = bool{ false };

        if (state_ == State::FilesIgnored)
        {
            // no-op
        }
        else if (state_ == State::FileTree)
        {
            if (current_key == AttrKey || current_key == PiecesRootKey)
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
            if (curdepth > 1 && (key(curdepth - 1) == PathKey || key(curdepth - 1) == PathUtf8Key))
            {
                if (!std::empty(file_subpath_))
                {
                    file_subpath_ += '/';
                }
                tr_torrent_files::makeSubpathPortable(value, file_subpath_);
            }
            else if (current_key == AttrKey)
            {
                // currently unused. TODO support for bittorrent v2
                // TODO https://github.com/transmission/transmission/issues/458
            }
            else if (
                pathIs(InfoKey, FilesKey, ""sv, Crc32Key) || //
                pathIs(InfoKey, FilesKey, ""sv, Ed2kKey) || //
                pathIs(InfoKey, FilesKey, ""sv, FilehashKey) || //
                pathIs(InfoKey, FilesKey, ""sv, Md5Key) || //
                pathIs(InfoKey, FilesKey, ""sv, Md5sumKey) || //
                pathIs(InfoKey, FilesKey, ""sv, MtimeKey) || // (why a string?)
                pathIs(InfoKey, FilesKey, ""sv, Sha1Key))
            {
                // unused by Transmission
            }
            else
            {
                unhandled = true;
            }
        }
        else if (pathIs(CommentKey) || pathIs(CommentUtf8Key))
        {
            tm_.comment_ = tr_strv_convert_utf8(value);
        }
        else if (pathIs(CreatedByKey) || pathIs(CreatedByUtf8Key))
        {
            tm_.creator_ = tr_strv_convert_utf8(value);
        }
        else if (
            pathIs(SourceKey) || pathIs(InfoKey, SourceKey) || //
            pathIs(PublisherKey) || pathIs(InfoKey, PublisherKey) || //
            pathIs(PublisherUtf8Key) || pathIs(InfoKey, PublisherUtf8Key))
        {
            // “publisher” is rare, but used by BitComet and appears
            // to have the same use as the 'source' key
            // http://wiki.bitcomet.com/inside_bitcomet

            tm_.source_ = tr_strv_convert_utf8(value);
        }
        else if (pathIs(AnnounceKey))
        {
            tm_.announceList().add(value, tier_);
        }
        else if (pathIs(EncodingKey))
        {
            encoding_ = tr_strvStrip(value);
        }
        else if (pathIs(UrlListKey))
        {
            tm_.addWebseed(value);
        }
        else if (pathIs(InfoKey, NameKey) || pathIs(InfoKey, NameUtf8Key))
        {
            tm_.setName(value);
        }
        else if (pathIs(InfoKey, PiecesKey))
        {
            if (std::size(value) % sizeof(tr_sha1_digest_t) == 0)
            {
                auto const n = std::size(value) / sizeof(tr_sha1_digest_t);
                tm_.pieces_.resize(n);
                std::copy_n(std::data(value), std::size(value), reinterpret_cast<char*>(std::data(tm_.pieces_)));
                tm_.pieces_offset_ = context.tokenSpan().first;
            }
            else
            {
                tr_error_set(context.error, EINVAL, fmt::format("invalid piece size: {}", std::size(value)));
                unhandled = true;
            }
        }
        else if (pathStartsWith(PieceLayersKey))
        {
            // currently unused. TODO support for bittorrent v2
            // TODO https://github.com/transmission/transmission/issues/458
        }
        else if (pathStartsWith(AnnounceListKey))
        {
            tm_.announceList().add(value, tier_);
        }
        else if (curdepth == 2 && (pathStartsWith(HttpSeedsKey) || pathStartsWith(UrlListKey)))
        {
            tm_.addWebseed(value);
        }
        else if (pathIs(MagnetInfoKey, DisplayNameKey) && std::empty(tm_.name()))
        {
            // compatibility with Transmission <= 3.0
            tm_.setName(value);
        }
        else if (pathIs(MagnetInfoKey, InfoHashKey))
        {
            // compatibility with Transmission <= 3.0
            if (value.length() == sizeof(tr_sha1_digest_t))
            {
                std::copy_n(std::data(value), sizeof(tr_sha1_digest_t), reinterpret_cast<char*>(std::data(tm_.info_hash_)));
                tm_.info_hash_str_ = tr_sha1_to_string(tm_.info_hash_);
                tm_.has_magnet_info_hash_ = true;
            }
        }
        else if (
            pathIs(ChecksumKey) || //
            pathIs(ErrCallbackKey) || //
            pathIs(InfoKey, CrossSeedEntryKey) || //
            pathIs(InfoKey, Ed2kKey) || //
            pathIs(InfoKey, EntropyKey) || //
            pathIs(InfoKey, Md5sumKey) || //
            pathIs(InfoKey, PublisherUrlKey) || //
            pathIs(InfoKey, PublisherUrlUtf8Key) || //
            pathIs(InfoKey, Sha1Key) || //
            pathIs(InfoKey, UniqueKey) || //
            pathIs(InfoKey, XCrossSeedKey) || //
            pathIs(LocaleKey) || //
            pathIs(LogCallbackKey) || //
            pathIs(PublisherUrlKey) || //
            pathIs(PublisherUrlUtf8Key) || //
            pathIs(TitleKey) || //
            pathIs(UidKey) || //
            pathStartsWith(AzureusPrivatePropertiesKey) || //
            pathStartsWith(AzureusPropertiesKey) || //
            pathStartsWith(InfoKey, CollectionsKey) || //
            pathStartsWith(InfoKey, FileDurationKey) || //
            pathStartsWith(InfoKey, ProfilesKey) || //
            pathStartsWith(LibtorrentResumeKey) || //
            pathStartsWith(MagnetInfoKey) || //
            pathStartsWith(NodesKey))
        {
            // unused by Transmission
        }
        else
        {
            unhandled = true;
        }

        if (unhandled)
        {
            tr_logAddWarn(fmt::format("unexpected: path '{}', str '{}'", path(), value));
        }

        return true;
    }

private:
    template<typename... Args>
    [[nodiscard]] bool pathStartsWith(Args... args) const noexcept
    {
        auto i = 1U;
        return (depth() >= sizeof...(args)) && ((key(i++) == args) && ...);
    }

    template<typename... Args>
    [[nodiscard]] bool pathIs(Args... args) const noexcept
    {
        auto i = 1U;
        return (depth() == sizeof...(args)) && ((key(i++) == args) && ...);
    }

    [[nodiscard]] bool addFile(Context const& context)
    {
        bool ok = true;

        if (file_length_ == 0)
        {
            return ok;
        }

        // FIXME: Check to see if we already added this file. This is a safeguard
        // for hybrid torrents with duplicate info between "file tree" and "files"
        if (std::empty(file_subpath_))
        {
            tr_error_set(context.error, EINVAL, fmt::format("invalid path [{:s}]", file_subpath_));
            ok = false;
        }
        else
        {
            tm_.files_.add(file_subpath_, file_length_);
        }

        file_length_ = 0;
        pieces_root_ = {};
        // NB: let caller decide how to clear file_tree_.
        // if we're in "files" mode we clear it; if in "file tree" we pop it
        return ok;
    }

    bool finishInfoDict(Context const& context)
    {
        if (std::empty(info_dict_begin_))
        {
            tr_error_set(context.error, EINVAL, "no info_dict found");
            return false;
        }

        auto root = tr_pathbuf{};
        tr_torrent_files::makeSubpathPortable(tm_.name_, root);
        if (!std::empty(root))
        {
            tm_.files_.insertSubpathPrefix(root);
        }

        TR_ASSERT(info_dict_begin_[0] == 'd');
        TR_ASSERT(context.raw().back() == 'e');
        char const* const begin = &info_dict_begin_.front();
        char const* const end = &context.raw().back() + 1;
        auto const info_dict_benc = std::string_view{ begin, size_t(end - begin) };
        auto const hash = tr_sha1::digest(info_dict_benc);
        auto const hash2 = tr_sha256::digest(info_dict_benc);

        tm_.info_hash_ = hash;
        tm_.info_hash_str_ = tr_sha1_to_string(tm_.info_hash_);
        tm_.info_hash2_ = hash2;
        tm_.info_hash2_str_ = tr_sha256_to_string(tm_.info_hash2_);
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
            tm_.files_.add(tm_.name_, length_);
        }

        if (auto const has_metainfo = tm_.infoDictSize() != 0U; has_metainfo)
        {
            // do some sanity checks to make sure the torrent looks sane
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
                if (!tr_error_is_set(context.error))
                {
                    tr_error_set(context.error, EINVAL, fmt::format("invalid piece size: {}", piece_size_));
                }
                return false;
            }

            tm_.block_info_.initSizes(tm_.files_.totalSize(), piece_size_);
            return true;
        }

        // no metainfo; might be a Transmission 3.00-style magnet file
        auto const ok = tm_.has_magnet_info_hash_;
        return ok;
    }

    static constexpr std::string_view AcodecKey = "acodec"sv;
    static constexpr std::string_view AnnounceKey = "announce"sv;
    static constexpr std::string_view AnnounceListKey = "announce-list"sv;
    static constexpr std::string_view AttrKey = "attr"sv;
    static constexpr std::string_view AzureusPrivatePropertiesKey = "azureus_private_properties"sv;
    static constexpr std::string_view AzureusPropertiesKey = "azureus_properties"sv;
    static constexpr std::string_view ChecksumKey = "checksum"sv;
    static constexpr std::string_view CollectionsKey = "collections"sv;
    static constexpr std::string_view CommentKey = "comment"sv;
    static constexpr std::string_view CommentUtf8Key = "comment.utf-8"sv;
    static constexpr std::string_view Crc32Key = "crc32"sv;
    static constexpr std::string_view CreatedByKey = "created by"sv;
    static constexpr std::string_view CreatedByUtf8Key = "created by.utf-8"sv;
    static constexpr std::string_view CreationDateKey = "creation date"sv;
    static constexpr std::string_view CrossSeedEntryKey = "cross_seed_entry"sv;
    static constexpr std::string_view DisplayNameKey = "display-name"sv;
    static constexpr std::string_view DurationKey = "duration"sv;
    static constexpr std::string_view Ed2kKey = "ed2k"sv;
    static constexpr std::string_view EncodedRateKey = "encoded rate"sv;
    static constexpr std::string_view EncodingKey = "encoding"sv;
    static constexpr std::string_view EntropyKey = "entropy"sv;
    static constexpr std::string_view ErrCallbackKey = "err_callback"sv;
    static constexpr std::string_view FileDurationKey = "file-duration"sv;
    static constexpr std::string_view FileMediaKey = "file-media"sv;
    static constexpr std::string_view FileTreeKey = "file tree"sv;
    static constexpr std::string_view FilehashKey = "filehash"sv;
    static constexpr std::string_view FilesKey = "files"sv;
    static constexpr std::string_view HeightKey = "height"sv;
    static constexpr std::string_view HttpSeedsKey = "httpseeds"sv;
    static constexpr std::string_view InfoKey = "info"sv;
    static constexpr std::string_view InfoHashKey = "info_hash"sv;
    static constexpr std::string_view LengthKey = "length"sv;
    static constexpr std::string_view LibtorrentResumeKey = "libtorrent_resume"sv;
    static constexpr std::string_view LocaleKey = "locale"sv;
    static constexpr std::string_view LogCallbackKey = "log_callback"sv;
    static constexpr std::string_view MagnetInfoKey = "magnet-info"sv;
    static constexpr std::string_view Md5Key = "md5"sv;
    static constexpr std::string_view Md5sumKey = "md5sum"sv;
    static constexpr std::string_view MetaVersionKey = "meta version"sv;
    static constexpr std::string_view MtimeKey = "mtime"sv;
    static constexpr std::string_view NameKey = "name"sv;
    static constexpr std::string_view NameUtf8Key = "name.utf-8"sv;
    static constexpr std::string_view NodesKey = "nodes"sv;
    static constexpr std::string_view PathKey = "path"sv;
    static constexpr std::string_view PathUtf8Key = "path.utf-8"sv;
    static constexpr std::string_view PieceLayersKey = "piece layers"sv;
    static constexpr std::string_view PieceLengthKey = "piece length"sv;
    static constexpr std::string_view PiecesKey = "pieces"sv;
    static constexpr std::string_view PiecesRootKey = "pieces root"sv;
    static constexpr std::string_view PrivateKey = "private"sv;
    static constexpr std::string_view ProfilesKey = "profiles"sv;
    static constexpr std::string_view PublisherKey = "publisher"sv;
    static constexpr std::string_view PublisherUtf8Key = "publisher.utf-8"sv;
    static constexpr std::string_view PublisherUrlKey = "publisher-url"sv;
    static constexpr std::string_view PublisherUrlUtf8Key = "publisher-url.utf-8"sv;
    static constexpr std::string_view Sha1Key = "sha1"sv;
    static constexpr std::string_view SourceKey = "source"sv;
    static constexpr std::string_view TitleKey = "title"sv;
    static constexpr std::string_view UidKey = "uid"sv;
    static constexpr std::string_view UniqueKey = "unique"sv;
    static constexpr std::string_view UrlListKey = "url-list"sv;
    static constexpr std::string_view VcodecKey = "vcodec"sv;
    static constexpr std::string_view WidthKey = "width"sv;
    static constexpr std::string_view XCrossSeedKey = "x_cross_seed"sv;
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

    tr_error_clear(&my_error);

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

    return tr_loadFile(filename, *contents, error) && parseBenc({ std::data(*contents), std::size(*contents) }, error);
}

tr_pathbuf tr_torrent_metainfo::makeFilename(
    std::string_view dirname,
    std::string_view name,
    std::string_view info_hash_string,
    BasenameFormat format,
    std::string_view suffix)
{
    // `${dirname}/${name}.${info_hash}${suffix}`
    // `${dirname}/${info_hash}${suffix}`
    return format == BasenameFormat::Hash ? tr_pathbuf{ dirname, '/', info_hash_string, suffix } :
                                            tr_pathbuf{ dirname, '/', name, '.', info_hash_string.substr(0, 16), suffix };
}

bool tr_torrent_metainfo::migrateFile(
    std::string_view dirname,
    std::string_view name,
    std::string_view info_hash_string,
    std::string_view suffix)
{
    auto const old_filename = makeFilename(dirname, name, info_hash_string, BasenameFormat::NameAndPartialHash, suffix);
    if (!tr_sys_path_exists(old_filename))
    {
        return false;
    }

    auto const new_filename = makeFilename(dirname, name, info_hash_string, BasenameFormat::Hash, suffix);
    if (tr_sys_path_exists(new_filename))
    {
        tr_sys_path_remove(old_filename);
        return false;
    }

    auto const renamed = tr_sys_path_rename(old_filename, new_filename);
    if (!renamed)
    {
        tr_logAddError(
            fmt::format(
                _("Migrated torrent file from '{old_path}' to '{path}'"),
                fmt::arg("old_path", old_filename),
                fmt::arg("path", new_filename)),
            name);
        return true;
    }

    return renamed;
}

void tr_torrent_metainfo::removeFile(
    std::string_view dirname,
    std::string_view name,
    std::string_view info_hash_string,
    std::string_view suffix)
{
    auto filename = makeFilename(dirname, name, info_hash_string, BasenameFormat::NameAndPartialHash, suffix);
    tr_sys_path_remove(filename, nullptr);

    filename = makeFilename(dirname, name, info_hash_string, BasenameFormat::Hash, suffix);
    tr_sys_path_remove(filename, nullptr);
}
