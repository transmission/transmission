// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cctype>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>

#include "transmission.h"

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
#include "variant.h"
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

    if (tm.fileCount() > 1U && !std::empty(url) && url.back() != '/')
    {
        return std::string{ url } + '/';
    }

    return std::string{ url };
}

void tr_torrent_metainfo::parseWebseeds(tr_torrent_metainfo& setme, tr_variant* meta)
{
    setme.webseed_urls_.clear();

    auto url = std::string_view{};
    tr_variant* urls = nullptr;
    if (tr_variantDictFindList(meta, TR_KEY_url_list, &urls))
    {
        size_t const n = tr_variantListSize(urls);
        setme.webseed_urls_.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            if (tr_variantGetStrView(tr_variantListChild(urls, i), &url) && tr_urlIsValid(url))
            {
                setme.webseed_urls_.push_back(fixWebseedUrl(setme, url));
            }
        }
    }
    else if (tr_variantDictFindStrView(meta, TR_KEY_url_list, &url) && tr_urlIsValid(url)) // handle single items in webseeds
    {
        setme.webseed_urls_.push_back(fixWebseedUrl(setme, url));
    }
}

bool tr_torrent_metainfo::parsePath(std::string_view root, tr_variant* path, std::string& setme)
{
    if (!tr_variantIsList(path))
    {
        return false;
    }

    setme = root;

    for (size_t i = 0, n = tr_variantListSize(path); i < n; ++i)
    {
        auto raw = std::string_view{};

        if (!tr_variantGetStrView(tr_variantListChild(path, i), &raw))
        {
            return false;
        }

        if (!std::empty(raw))
        {
            setme += TR_PATH_DELIMITER;
            setme += raw;
        }
    }

    auto const sanitized = tr_file_info::sanitizePath(setme);

    if (std::size(sanitized) <= std::size(root))
    {
        return false;
    }

    tr_strvUtf8Clean(sanitized, setme);
    return true;
}

std::string_view tr_torrent_metainfo::parseFiles(tr_torrent_metainfo& setme, tr_variant* info_dict, uint64_t* setme_total_size)
{
    auto total_size = uint64_t{ 0 };

    setme.files_.clear();

    auto const root_name = tr_file_info::sanitizePath(setme.name_);

    if (std::empty(root_name))
    {
        return "invalid name"sv;
    }

    // bittorrent 1.0 spec
    // http://bittorrent.org/beps/bep_0003.html
    //
    // "There is also a key length or a key files, but not both or neither.
    //
    // "If length is present then the download represents a single file,
    // otherwise it represents a set of files which go in a directory structure.
    // In the single file case, length maps to the length of the file in bytes.
    auto len = int64_t{};
    tr_variant* files_entry = nullptr;
    if (tr_variantDictFindInt(info_dict, TR_KEY_length, &len))
    {
        total_size = len;
        setme.files_.add(root_name, len);
    }

    // "For the purposes of the other keys, the multi-file case is treated as
    // only having a single file by concatenating the files in the order they
    // appear in the files list. The files list is the value files maps to,
    // and is a list of dictionaries containing the following keys:
    // length - The length of the file, in bytes.
    // path - A list of UTF-8 encoded strings corresponding to subdirectory
    // names, the last of which is the actual file name (a zero length list
    // is an error case).
    // In the multifile case, the name key is the name of a directory."
    else if (tr_variantDictFindList(info_dict, TR_KEY_files, &files_entry))
    {
        auto buf = std::string{};
        buf.reserve(1024); // arbitrary
        auto const n_files = size_t{ tr_variantListSize(files_entry) };
        setme.files_.reserve(n_files);
        for (size_t i = 0; i < n_files; ++i)
        {
            auto* const file_entry = tr_variantListChild(files_entry, i);
            if (!tr_variantIsDict(file_entry))
            {
                return "'files' is not a dictionary";
            }

            if (!tr_variantDictFindInt(file_entry, TR_KEY_length, &len))
            {
                return "length";
            }

            tr_variant* path_variant = nullptr;
            if (!tr_variantDictFindList(file_entry, TR_KEY_path_utf_8, &path_variant) &&
                !tr_variantDictFindList(file_entry, TR_KEY_path, &path_variant))
            {
                return "path";
            }

            if (!parsePath(root_name, path_variant, buf))
            {
                return "path";
            }

            setme.files_.add(buf, len);
            total_size += len;
        }
    }
    else
    {
        // TODO: add support for 'file tree' BitTorrent 2 torrents / hybrid torrents.
        // Patches welcomed!
        // https://www.bittorrent.org/beps/bep_0052.html#info-dictionary
        return "'info' dict has neither 'files' nor 'length' key";
    }

    *setme_total_size = total_size;
    return {};
}

// https://www.bittorrent.org/beps/bep_0012.html
std::string_view tr_torrent_metainfo::parseAnnounce(tr_torrent_metainfo& setme, tr_variant* meta)
{
    setme.announce_list_.clear();

    auto url = std::string_view{};

    // announce-list
    // example: d['announce-list'] = [ [tracker1], [backup1], [backup2] ]
    if (tr_variant* tiers = nullptr; tr_variantDictFindList(meta, TR_KEY_announce_list, &tiers))
    {
        for (size_t i = 0, n_tiers = tr_variantListSize(tiers); i < n_tiers; ++i)
        {
            tr_variant* tier_list = tr_variantListChild(tiers, i);
            if (tier_list == nullptr)
            {
                continue;
            }

            for (size_t j = 0, jn = tr_variantListSize(tier_list); j < jn; ++j)
            {
                if (!tr_variantGetStrView(tr_variantListChild(tier_list, j), &url))
                {
                    continue;
                }

                setme.announce_list_.add(url, i);
            }
        }
    }

    // single 'announce' url
    if (std::empty(setme.announce_list_) && tr_variantDictFindStrView(meta, TR_KEY_announce, &url))
    {
        setme.announce_list_.add(url, 0);
    }

    return {};
}

std::string_view tr_torrent_metainfo::parseImpl(tr_torrent_metainfo& setme, tr_variant* meta, std::string_view benc)
{
    int64_t i = 0;
    auto sv = std::string_view{};

    // info_hash: urlencoded 20-byte SHA1 hash of the value of the info key
    // from the Metainfo file. Note that the value will be a bencoded
    // dictionary, given the definition of the info key above.
    tr_variant* info_dict = nullptr;
    if (tr_variantDictFindDict(meta, TR_KEY_info, &info_dict))
    {
        // Calculate the hash of the `info` dict.
        // This is the torrent's unique ID and is central to everything.
        auto const info_dict_benc = tr_variantToStr(info_dict, TR_VARIANT_FMT_BENC);
        auto const hash = tr_sha1(info_dict_benc);
        if (!hash)
        {
            return "bad info_dict checksum";
        }
        setme.info_hash_ = *hash;
        setme.info_hash_str_ = tr_sha1_to_string(setme.info_hash_);

        // Remember the offset and length of the bencoded info dict.
        // This is important when providing metainfo to magnet peers
        // (see http://bittorrent.org/beps/bep_0009.html for details).
        //
        // Calculating this later from scratch is kind of expensive,
        // so do it here since we've already got the bencoded info dict.
        auto const it = std::search(std::begin(benc), std::end(benc), std::begin(info_dict_benc), std::end(info_dict_benc));
        setme.info_dict_offset_ = std::distance(std::begin(benc), it);
        setme.info_dict_size_ = std::size(info_dict_benc);

        // In addition, remember the offset of the pieces dictionary entry.
        // This will be useful when we load piece checksums on demand.
        auto constexpr Key = "6:pieces"sv;
        auto const pit = std::search(std::begin(benc), std::end(benc), std::begin(Key), std::end(Key));
        setme.pieces_offset_ = std::distance(std::begin(benc), pit) + std::size(Key);
    }
    else
    {
        return "missing 'info' dictionary";
    }

    // name
    if (tr_variantDictFindStrView(info_dict, TR_KEY_name_utf_8, &sv) || tr_variantDictFindStrView(info_dict, TR_KEY_name, &sv))
    {
        tr_strvUtf8Clean(sv, setme.name_);
    }
    else
    {
        return "'info' dictionary has neither 'name.utf-8' nor 'name'";
    }

    // comment (optional)
    setme.comment_.clear();
    if (tr_variantDictFindStrView(meta, TR_KEY_comment_utf_8, &sv) || tr_variantDictFindStrView(meta, TR_KEY_comment, &sv))
    {
        tr_strvUtf8Clean(sv, setme.comment_);
    }

    // created by (optional)
    setme.creator_.clear();
    if (tr_variantDictFindStrView(meta, TR_KEY_created_by_utf_8, &sv) ||
        tr_variantDictFindStrView(meta, TR_KEY_created_by, &sv))
    {
        tr_strvUtf8Clean(sv, setme.creator_);
    }

    // creation date (optional)
    setme.date_created_ = tr_variantDictFindInt(meta, TR_KEY_creation_date, &i) ? i : 0;

    // private (optional)
    setme.is_private_ = (tr_variantDictFindInt(info_dict, TR_KEY_private, &i) ||
                         tr_variantDictFindInt(meta, TR_KEY_private, &i)) &&
        (i != 0);

    // source (optional)
    setme.source_.clear();
    if (tr_variantDictFindStrView(info_dict, TR_KEY_source, &sv) || tr_variantDictFindStrView(meta, TR_KEY_source, &sv))
    {
        tr_strvUtf8Clean(sv, setme.source_);
    }

    // piece length
    if (!tr_variantDictFindInt(info_dict, TR_KEY_piece_length, &i) || (i <= 0) || (i > UINT32_MAX))
    {
        return "'info' dict 'piece length' is missing or has an invalid value";
    }
    auto const piece_size = (uint32_t)i;

    // pieces
    if (!tr_variantDictFindStrView(info_dict, TR_KEY_pieces, &sv) || (std::size(sv) % sizeof(tr_sha1_digest_t) != 0))
    {
        return "'info' dict 'pieces' is missing or has an invalid value";
    }
    auto const n = std::size(sv) / sizeof(tr_sha1_digest_t);
    setme.pieces_.resize(n);
    std::copy_n(std::data(sv), std::size(sv), reinterpret_cast<char*>(std::data(setme.pieces_)));

    // files
    auto total_size = uint64_t{ 0 };
    if (auto const errstr = parseFiles(setme, info_dict, &total_size); !std::empty(errstr))
    {
        return errstr;
    }

    if (std::empty(setme.files_))
    {
        return "no files found"sv;
    }

    // do the size and piece size match up?
    setme.block_info_.initSizes(total_size, piece_size);
    if (setme.block_info_.pieceCount() != std::size(setme.pieces_))
    {
        return "piece count and file sizes do not match";
    }

    parseAnnounce(setme, meta);
    parseWebseeds(setme, meta);

    return {};
}

bool tr_torrent_metainfo::parseBenc(std::string_view benc, tr_error** error)
{
    auto top = tr_variant{};
    if (!tr_variantFromBuf(&top, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, benc, nullptr, error))
    {
        return false;
    }

    auto const errmsg = parseImpl(*this, &top, benc);
    tr_variantFree(&top);
    if (!std::empty(errmsg))
    {
        tr_error_set(error, TR_ERROR_EINVAL, fmt::format(FMT_STRING("Error parsing metainfo: {:s}"), errmsg));
        return false;
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

tr_sha1_digest_t const& tr_torrent_metainfo::pieceHash(tr_piece_index_t piece) const
{
    return this->pieces_[piece];
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
    return format == BasenameFormat::Hash ? tr_pathbuf{ dirname, "/"sv, info_hash_string, suffix } :
                                            tr_pathbuf{ dirname, "/"sv, name, "."sv, info_hash_string.substr(0, 16), suffix };
}

bool tr_torrent_metainfo::migrateFile(
    std::string_view dirname,
    std::string_view name,
    std::string_view info_hash_string,
    std::string_view suffix)
{
    auto const old_filename = makeFilename(dirname, name, info_hash_string, BasenameFormat::NameAndPartialHash, suffix);
    auto const old_filename_exists = tr_sys_path_exists(old_filename);
    auto const new_filename = makeFilename(dirname, name, info_hash_string, BasenameFormat::Hash, suffix);
    auto const new_filename_exists = tr_sys_path_exists(new_filename);

    if (old_filename_exists && new_filename_exists)
    {
        tr_sys_path_remove(old_filename);
        return false;
    }

    if (new_filename_exists)
    {
        return false;
    }

    if (old_filename_exists && tr_sys_path_rename(old_filename, new_filename))
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
    tr_sys_path_remove(makeFilename(dirname, name, info_hash_string, BasenameFormat::NameAndPartialHash, suffix));
    tr_sys_path_remove(makeFilename(dirname, name, info_hash_string, BasenameFormat::Hash, suffix));
}
