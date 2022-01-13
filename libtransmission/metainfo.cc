/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <event2/util.h> // evutil_ascii_strncasecmp()

#include "transmission.h"

#include "crypto-utils.h" /* tr_sha1 */
#include "error.h"
#include "error-types.h"
#include "file.h"
#include "log.h"
#include "metainfo.h"
#include "platform.h" /* tr_getTorrentDir() */
#include "session.h"
#include "torrent.h"
#include "utils.h"
#include "variant.h"
#include "magnet-metainfo.h"
#include "web-utils.h"

using namespace std::literals;

/***
****
***/

static std::string getTorrentFilename(tr_session const* session, tr_info const* inf, tr_magnet_metainfo::BasenameFormat format)
{
    return tr_magnet_metainfo::makeFilename(
        tr_getTorrentDir(session),
        inf->name(),
        inf->infoHashString(),
        format,
        ".torrent"sv);
}

/***
****
***/

bool tr_metainfoAppendSanitizedPathComponent(std::string& out, std::string_view in)
{
    auto const original_out_len = std::size(out);

    // remove leading spaces
    auto constexpr leading_test = [](auto ch)
    {
        return isspace(ch);
    };
    auto const it = std::find_if_not(std::begin(in), std::end(in), leading_test);
    in.remove_prefix(std::distance(std::begin(in), it));

    // remove trailing spaces and '.'
    auto constexpr trailing_test = [](auto ch)
    {
        return isspace(ch) || ch == '.';
    };
    auto const rit = std::find_if_not(std::rbegin(in), std::rend(in), trailing_test);
    in.remove_suffix(std::distance(std::rbegin(in), rit));

    // munge banned characters
    // https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
    auto constexpr ensure_legal_char = [](auto ch)
    {
        auto constexpr Banned = std::string_view{ "<>:\"/\\|?*" };
        auto const banned = tr_strvContains(Banned, ch) || (unsigned char)ch < 0x20;
        return banned ? '_' : ch;
    };
    auto const old_out_len = std::size(out);
    std::transform(std::begin(in), std::end(in), std::back_inserter(out), ensure_legal_char);

    // munge banned filenames
    // https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
    auto constexpr ReservedNames = std::array<std::string_view, 22>{
        "CON"sv,  "PRN"sv,  "AUX"sv,  "NUL"sv,  "COM1"sv, "COM2"sv, "COM3"sv, "COM4"sv, "COM5"sv, "COM6"sv, "COM7"sv,
        "COM8"sv, "COM9"sv, "LPT1"sv, "LPT2"sv, "LPT3"sv, "LPT4"sv, "LPT5"sv, "LPT6"sv, "LPT7"sv, "LPT8"sv, "LPT9"sv,
    };
    for (auto const& name : ReservedNames)
    {
        size_t const name_len = std::size(name);
        if (evutil_ascii_strncasecmp(out.c_str() + old_out_len, std::data(name), name_len) != 0 ||
            (out[old_out_len + name_len] != '\0' && out[old_out_len + name_len] != '.'))
        {
            continue;
        }

        out.insert(std::begin(out) + old_out_len + name_len, '_');
        break;
    }

    return std::size(out) > original_out_len;
}

static bool getfile(std::string* setme, std::string_view root, tr_variant* path, std::string& buf)
{
    bool success = false;

    setme->clear();

    if (tr_variantIsList(path))
    {
        success = true;

        buf = root;

        for (int i = 0, n = tr_variantListSize(path); i < n; i++)
        {
            auto raw = std::string_view{};
            if (!tr_variantGetStrView(tr_variantListChild(path, i), &raw))
            {
                success = false;
                break;
            }

            auto const pos = std::size(buf);
            if (!tr_metainfoAppendSanitizedPathComponent(buf, raw))
            {
                continue;
            }

            buf.insert(std::begin(buf) + pos, TR_PATH_DELIMITER);
        }
    }

    if (success && std::size(buf) <= std::size(root))
    {
        success = false;
    }

    if (success)
    {
        *setme = tr_strvUtf8Clean(buf);
    }

    return success;
}

static char const* parseFiles(tr_info* inf, tr_variant* files, tr_variant const* length)
{
    int64_t len = 0;
    inf->total_size_ = 0;

    auto root_name = std::string{};
    if (!tr_metainfoAppendSanitizedPathComponent(root_name, inf->name()))
    {
        return "path";
    }

    char const* errstr = nullptr;

    if (tr_variantIsList(files)) /* multi-file mode */
    {
        auto buf = std::string{};
        errstr = nullptr;

        tr_file_index_t const n = tr_variantListSize(files);
        inf->files.resize(n);
        for (tr_file_index_t i = 0; i < n; ++i)
        {
            auto* const file = tr_variantListChild(files, i);

            if (!tr_variantIsDict(file))
            {
                errstr = "files";
                break;
            }

            tr_variant* path = nullptr;
            if (!tr_variantDictFindList(file, TR_KEY_path_utf_8, &path) && !tr_variantDictFindList(file, TR_KEY_path, &path))
            {
                errstr = "path";
                break;
            }

            if (!getfile(&inf->files[i].subpath_, root_name, path, buf))
            {
                errstr = "path";
                break;
            }

            if (!tr_variantDictFindInt(file, TR_KEY_length, &len))
            {
                errstr = "length";
                break;
            }

            inf->files[i].size_ = len;
            inf->total_size_ += len;
        }
    }
    else if (tr_variantGetInt(length, &len)) /* single-file mode */
    {
        inf->files.resize(1);
        inf->files[0].subpath_ = root_name;
        inf->files[0].size_ = len;
        inf->total_size_ += len;
    }
    else
    {
        errstr = "length";
    }

    return errstr;
}

static char const* getannounce(tr_info* inf, tr_variant* meta)
{
    inf->announce_list = std::make_shared<tr_announce_list>();

    // tr_tracker_info* trackers = nullptr;
    // int trackerCount = 0;
    auto url = std::string_view{};

    /* Announce-list */
    tr_variant* tiers = nullptr;
    if (tr_variantDictFindList(meta, TR_KEY_announce_list, &tiers))
    {
        for (size_t i = 0, in = tr_variantListSize(tiers); i < in; ++i)
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

                inf->announce_list->add(i, url);
            }
        }
    }

    /* Regular announce value */
    if (std::empty(*inf->announce_list) && tr_variantDictFindStrView(meta, TR_KEY_announce, &url))
    {
        inf->announce_list->add(0, url);
    }

    return nullptr;
}

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
static char* fix_webseed_url(tr_info const* inf, std::string_view url)
{
    url = tr_strvStrip(url);

    if (!tr_urlIsValid(url))
    {
        return nullptr;
    }

    if (inf->fileCount() > 1 && !std::empty(url) && url.back() != '/')
    {
        return tr_strvDup(tr_strvJoin(url, "/"sv));
    }

    return tr_strvDup(url);
}

static void geturllist(tr_info* inf, tr_variant* meta)
{
    inf->webseeds_.clear();

    auto url = std::string_view{};
    tr_variant* urls = nullptr;
    if (tr_variantDictFindList(meta, TR_KEY_url_list, &urls))
    {
        int const n = tr_variantListSize(urls);

        for (int i = 0; i < n; ++i)
        {
            if (tr_variantGetStrView(tr_variantListChild(urls, i), &url))
            {
                char* const fixed_url = fix_webseed_url(inf, url);
                if (fixed_url != nullptr)
                {
                    inf->webseeds_.emplace_back(fixed_url);
                }
            }
        }
    }
    else if (tr_variantDictFindStrView(meta, TR_KEY_url_list, &url)) /* handle single items in webseeds */
    {
        char* const fixed_url = fix_webseed_url(inf, url);
        if (fixed_url != nullptr)
        {
            inf->webseeds_.emplace_back(fixed_url);
        }
    }
}

static char const* tr_metainfoParseImpl(
    tr_session const* session,
    tr_info* inf,
    std::vector<tr_sha1_digest_t>* pieces,
    uint64_t* info_dict_size,
    tr_variant const* meta_in)
{
    int64_t i = 0;
    auto sv = std::string_view{};
    tr_variant* const meta = const_cast<tr_variant*>(meta_in);
    bool isMagnet = false;

    /* info_hash: urlencoded 20-byte SHA1 hash of the value of the info key
     * from the Metainfo file. Note that the value will be a bencoded
     * dictionary, given the definition of the info key above. */
    tr_variant* infoDict = nullptr;
    if (bool b = tr_variantDictFindDict(meta, TR_KEY_info, &infoDict); !b)
    {
        /* no info dictionary... is this a magnet link? */
        if (tr_variant* d = nullptr; tr_variantDictFindDict(meta, TR_KEY_magnet_info, &d))
        {
            isMagnet = true;

            // get the info-hash
            if (!tr_variantDictFindStrView(d, TR_KEY_info_hash, &sv))
            {
                return "info_hash";
            }

            if (std::size(sv) != std::size(inf->hash_))
            {
                return "info_hash";
            }

            std::copy(std::begin(sv), std::end(sv), reinterpret_cast<char*>(std::data(inf->hash_)));
            inf->info_hash_string_ = tr_sha1_to_string(inf->hash_);

            // maybe get the display name
            tr_variantDictFindStrView(d, TR_KEY_display_name, &sv);
            inf->setName(!std::empty(sv) ? sv : inf->info_hash_string_);
        }
        else // not a magnet link and has no info dict...
        {
            return "info";
        }
    }
    else
    {
        auto const benc = tr_variantToStr(infoDict, TR_VARIANT_FMT_BENC);
        auto const hash = tr_sha1(benc);
        if (!hash)
        {
            return "hash";
        }

        inf->hash_ = *hash;
        inf->info_hash_string_ = tr_sha1_to_string(inf->hash_);

        if (info_dict_size != nullptr)
        {
            *info_dict_size = std::size(benc);
        }
    }

    /* name */
    if (!isMagnet)
    {
        if (!tr_variantDictFindStrView(infoDict, TR_KEY_name_utf_8, &sv) &&
            !tr_variantDictFindStrView(infoDict, TR_KEY_name, &sv))
        {
            sv = ""sv;
        }

        if (std::empty(sv))
        {
            return "name";
        }

        inf->name_ = tr_strvUtf8Clean(sv);
    }

    /* comment */
    if (!tr_variantDictFindStrView(meta, TR_KEY_comment_utf_8, &sv) && !tr_variantDictFindStrView(meta, TR_KEY_comment, &sv))
    {
        sv = ""sv;
    }

    inf->comment_ = tr_strvUtf8Clean(sv);

    /* created by */
    if (!tr_variantDictFindStrView(meta, TR_KEY_created_by_utf_8, &sv) &&
        !tr_variantDictFindStrView(meta, TR_KEY_created_by, &sv))
    {
        sv = ""sv;
    }

    inf->creator_ = tr_strvUtf8Clean(sv);

    /* creation date */
    i = 0;
    (void)!tr_variantDictFindInt(meta, TR_KEY_creation_date, &i);
    inf->date_created_ = i;

    /* private */
    if (!tr_variantDictFindInt(infoDict, TR_KEY_private, &i) && !tr_variantDictFindInt(meta, TR_KEY_private, &i))
    {
        i = 0;
    }

    inf->is_private_ = i != 0;

    /* source */
    if (!tr_variantDictFindStrView(infoDict, TR_KEY_source, &sv) && !tr_variantDictFindStrView(meta, TR_KEY_source, &sv))
    {
        sv = ""sv;
    }

    inf->source_ = tr_strvUtf8Clean(sv);

    /* piece length */
    if (!isMagnet)
    {
        if (!tr_variantDictFindInt(infoDict, TR_KEY_piece_length, &i) || (i < 1))
        {
            return "piece length";
        }

        inf->piece_size_ = i;
    }

    /* pieces and files */
    if (!isMagnet)
    {
        if (!tr_variantDictFindStrView(infoDict, TR_KEY_pieces, &sv))
        {
            return "pieces";
        }

        if (std::size(sv) % std::size(tr_sha1_digest_t{}) != 0)
        {
            return "pieces";
        }

        auto const n_pieces = std::size(sv) / std::size(tr_sha1_digest_t{});
        inf->piece_count_ = n_pieces;
        pieces->resize(n_pieces);
        std::copy_n(std::data(sv), std::size(sv), reinterpret_cast<uint8_t*>(std::data(*pieces)));

        auto const* const errstr = parseFiles(
            inf,
            tr_variantDictFind(infoDict, TR_KEY_files),
            tr_variantDictFind(infoDict, TR_KEY_length));
        if (errstr != nullptr)
        {
            return errstr;
        }

        if (inf->fileCount() == 0 || inf->total_size_ == 0)
        {
            return "files";
        }

        if ((uint64_t)inf->piece_count_ != (inf->total_size_ + inf->piece_size_ - 1) / inf->piece_size_)
        {
            return "files";
        }
    }

    /* get announce or announce-list */
    auto const* const errstr = getannounce(inf, meta);
    if (errstr != nullptr)
    {
        return errstr;
    }

    /* get the url-list */
    geturllist(inf, meta);

    /* filename of Transmission's copy */
    inf->torrent_file_ = session != nullptr ? getTorrentFilename(session, inf, tr_magnet_metainfo::BasenameFormat::Hash) : ""sv;

    return nullptr;
}

std::optional<tr_metainfo_parsed> tr_metainfoParse(tr_session const* session, tr_variant const* meta_in, tr_error** error)
{
    auto out = tr_metainfo_parsed{};

    char const* bad_tag = tr_metainfoParseImpl(session, &out.info, &out.pieces, &out.info_dict_size, meta_in);
    if (bad_tag != nullptr)
    {
        tr_error_set(error, TR_ERROR_EINVAL, tr_strvJoin("Error parsing metainfo: "sv, bad_tag));
        tr_metainfoFree(&out.info);
        return {};
    }

    return std::optional<tr_metainfo_parsed>{ std::move(out) };
}

void tr_metainfoFree(tr_info* inf)
{
    inf->webseeds_.clear();
    inf->announce_list.reset();
}
