/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <string_view>
#include <vector>

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
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"
#include "web-utils.h"

using namespace std::literals;

/***
****
***/

std::string tr_buildTorrentFilename(
    std::string_view dirname,
    tr_info const* inf,
    enum tr_metainfo_basename_format format,
    std::string_view suffix)
{
    return format == TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH ?
        tr_strvJoin(dirname, "/"sv, inf->name, "."sv, std::string_view{ inf->hashString, 16 }, suffix) :
        tr_strvJoin(dirname, "/"sv, inf->hashString, suffix);
}

static std::string getTorrentFilename(tr_session const* session, tr_info const* inf, enum tr_metainfo_basename_format format)
{
    return tr_buildTorrentFilename(tr_getTorrentDir(session), inf, format, ".torrent"sv);
}

/***
****
***/

bool tr_metainfoAppendSanitizedPathComponent(std::string& out, std::string_view in, bool* is_adjusted)
{
    auto const original_out_len = std::size(out);
    auto const original_in = in;
    *is_adjusted = false;

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

    *is_adjusted = original_in != std::string_view{ out.c_str() + original_out_len };
    return std::size(out) > original_out_len;
}

static bool getfile(char** setme, bool* is_adjusted, std::string_view root, tr_variant* path, std::string& buf)
{
    bool success = false;

    *setme = nullptr;
    *is_adjusted = false;

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

            auto is_component_adjusted = bool{};
            auto const pos = std::size(buf);
            if (!tr_metainfoAppendSanitizedPathComponent(buf, raw, &is_component_adjusted))
            {
                continue;
            }

            buf.insert(std::begin(buf) + pos, TR_PATH_DELIMITER);

            *is_adjusted |= is_component_adjusted;
        }
    }

    if (success && std::size(buf) <= std::size(root))
    {
        success = false;
    }

    if (success)
    {
        *setme = tr_utf8clean(buf);
        *is_adjusted |= buf != *setme;
    }

    return success;
}

static char const* parseFiles(tr_info* inf, tr_variant* files, tr_variant const* length)
{
    int64_t len = 0;
    inf->totalSize = 0;

    bool is_root_adjusted = false;
    auto root_name = std::string{};
    if (!tr_metainfoAppendSanitizedPathComponent(root_name, inf->name, &is_root_adjusted))
    {
        return "path";
    }

    char const* errstr = nullptr;

    if (tr_variantIsList(files)) /* multi-file mode */
    {
        auto buf = std::string{};
        errstr = nullptr;

        inf->isFolder = true;
        inf->fileCount = tr_variantListSize(files);
        inf->files = tr_new0(tr_file, inf->fileCount);

        for (tr_file_index_t i = 0; i < inf->fileCount; i++)
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

            bool is_file_adjusted = false;
            if (!getfile(&inf->files[i].name, &is_file_adjusted, root_name, path, buf))
            {
                errstr = "path";
                break;
            }

            if (!tr_variantDictFindInt(file, TR_KEY_length, &len))
            {
                errstr = "length";
                break;
            }

            inf->files[i].length = len;
            inf->files[i].priv.is_renamed = is_root_adjusted || is_file_adjusted;
            inf->totalSize += len;
        }
    }
    else if (tr_variantGetInt(length, &len)) /* single-file mode */
    {
        inf->isFolder = false;
        inf->fileCount = 1;
        inf->files = tr_new0(tr_file, 1);
        inf->files[0].name = tr_strndup(root_name.c_str(), std::size(root_name));
        inf->files[0].length = len;
        inf->files[0].priv.is_renamed = is_root_adjusted;
        inf->totalSize += len;
    }
    else
    {
        errstr = "length";
    }

    return errstr;
}

static char* tr_convertAnnounceToScrape(std::string_view url)
{
    char* scrape = nullptr;

    /* To derive the scrape URL use the following steps:
     * Begin with the announce URL. Find the last '/' in it.
     * If the text immediately following that '/' isn't 'announce'
     * it will be taken as a sign that that tracker doesn't support
     * the scrape convention. If it does, substitute 'scrape' for
     * 'announce' to find the scrape page. */

    auto constexpr oldval = "/announce"sv;
    auto pos = url.rfind(oldval.front());
    if (pos != url.npos && url.find(oldval, pos) == pos)
    {
        auto constexpr newval = "/scrape"sv;
        auto const prefix = url.substr(0, pos);
        auto const suffix = url.substr(pos + std::size(oldval));
        auto const n = std::size(prefix) + std::size(newval) + std::size(suffix);
        scrape = tr_new(char, n + 1);
        auto* walk = scrape;
        walk = std::copy(std::begin(prefix), std::end(prefix), walk);
        walk = std::copy(std::begin(newval), std::end(newval), walk);
        walk = std::copy(std::begin(suffix), std::end(suffix), walk);
        *walk = '\0';
        TR_ASSERT(scrape + n == walk);
    }
    // some torrents with UDP announce URLs don't have /announce
    else if (url.find("udp:"sv) == 0)
    {
        scrape = tr_strvDup(url);
    }

    return scrape;
}

static char const* getannounce(tr_info* inf, tr_variant* meta)
{
    tr_tracker_info* trackers = nullptr;
    int trackerCount = 0;
    auto url = std::string_view{};

    /* Announce-list */
    tr_variant* tiers = nullptr;
    if (tr_variantDictFindList(meta, TR_KEY_announce_list, &tiers))
    {
        int const numTiers = tr_variantListSize(tiers);
        int n = 0;

        for (int i = 0; i < numTiers; i++)
        {
            n += tr_variantListSize(tr_variantListChild(tiers, i));
        }

        trackers = tr_new0(tr_tracker_info, n);

        int validTiers = 0;
        for (int i = 0; i < numTiers; ++i)
        {
            tr_variant* tier = tr_variantListChild(tiers, i);
            int const tierSize = tr_variantListSize(tier);
            bool anyAdded = false;

            for (int j = 0; j < tierSize; j++)
            {
                if (tr_variantGetStrView(tr_variantListChild(tier, j), &url))
                {
                    url = tr_strvStrip(url);

                    if (tr_urlIsValidTracker(url))
                    {
                        tr_tracker_info* t = trackers + trackerCount;
                        t->tier = validTiers;
                        t->announce = tr_strvDup(url);
                        t->scrape = tr_convertAnnounceToScrape(url);
                        t->id = trackerCount;

                        anyAdded = true;
                        ++trackerCount;
                    }
                }
            }

            if (anyAdded)
            {
                ++validTiers;
            }
        }

        /* did we use any of the tiers? */
        if (trackerCount == 0)
        {
            tr_free(trackers);
            trackers = nullptr;
        }
    }

    /* Regular announce value */
    if (trackerCount == 0 && tr_variantDictFindStrView(meta, TR_KEY_announce, &url))
    {
        url = tr_strvStrip(url);

        if (tr_urlIsValidTracker(url))
        {
            trackers = tr_new0(tr_tracker_info, 1);
            trackers[trackerCount].tier = 0;
            trackers[trackerCount].announce = tr_strvDup(url);
            trackers[trackerCount].scrape = tr_convertAnnounceToScrape(url);
            trackers[trackerCount].id = 0;
            trackerCount++;
        }
    }

    inf->trackers = trackers;
    inf->trackerCount = trackerCount;

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

    if (inf->fileCount > 1 && !std::empty(url) && url.back() != '/')
    {
        return tr_strdup_printf("%" TR_PRIsv "/", TR_PRIsv_ARG(url));
    }

    return tr_strvDup(url);
}

static void geturllist(tr_info* inf, tr_variant* meta)
{
    tr_variant* urls = nullptr;
    auto url = std::string_view{};

    if (tr_variantDictFindList(meta, TR_KEY_url_list, &urls))
    {
        int const n = tr_variantListSize(urls);

        inf->webseedCount = 0;
        inf->webseeds = tr_new0(char*, n);

        for (int i = 0; i < n; i++)
        {
            if (tr_variantGetStrView(tr_variantListChild(urls, i), &url))
            {
                char* const fixed_url = fix_webseed_url(inf, url);
                if (fixed_url != nullptr)
                {
                    inf->webseeds[inf->webseedCount++] = fixed_url;
                }
            }
        }
    }
    else if (tr_variantDictFindStrView(meta, TR_KEY_url_list, &url)) /* handle single items in webseeds */
    {
        char* const fixed_url = fix_webseed_url(inf, url);
        if (fixed_url != nullptr)
        {
            inf->webseedCount = 1;
            inf->webseeds = tr_new0(char*, 1);
            inf->webseeds[0] = fixed_url;
        }
    }
}

static char const* tr_metainfoParseImpl(
    tr_session const* session,
    tr_info* inf,
    std::vector<tr_sha1_digest_t>* pieces,
    uint64_t* infoDictLength,
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

            if (std::size(sv) != SHA_DIGEST_LENGTH)
            {
                return "info_hash";
            }

            std::copy(std::begin(sv), std::end(sv), inf->hash);
            tr_sha1_to_hex(inf->hashString, inf->hash);

            // maybe get the display name
            if (tr_variantDictFindStrView(d, TR_KEY_display_name, &sv))
            {
                tr_free(inf->name);
                tr_free(inf->originalName);
                inf->name = tr_strvDup(sv);
                inf->originalName = tr_strvDup(sv);
            }

            if (inf->name == nullptr)
            {
                inf->name = tr_strdup(inf->hashString);
            }

            if (inf->originalName == nullptr)
            {
                inf->originalName = tr_strdup(inf->hashString);
            }
        }
        else // not a magnet link and has no info dict...
        {
            return "info";
        }
    }
    else
    {
        size_t blen = 0;
        char* bstr = tr_variantToStr(infoDict, TR_VARIANT_FMT_BENC, &blen);
        tr_sha1(inf->hash, bstr, (int)blen, nullptr);
        tr_sha1_to_hex(inf->hashString, inf->hash);

        if (infoDictLength != nullptr)
        {
            *infoDictLength = blen;
        }

        tr_free(bstr);
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

        tr_free(inf->name);
        tr_free(inf->originalName);
        inf->name = tr_utf8clean(sv);
        inf->originalName = tr_strdup(inf->name);
    }

    /* comment */
    if (!tr_variantDictFindStrView(meta, TR_KEY_comment_utf_8, &sv) && !tr_variantDictFindStrView(meta, TR_KEY_comment, &sv))
    {
        sv = ""sv;
    }

    tr_free(inf->comment);
    inf->comment = tr_utf8clean(sv);

    /* created by */
    if (!tr_variantDictFindStrView(meta, TR_KEY_created_by_utf_8, &sv) &&
        !tr_variantDictFindStrView(meta, TR_KEY_created_by, &sv))
    {
        sv = ""sv;
    }

    tr_free(inf->creator);
    inf->creator = tr_utf8clean(sv);

    /* creation date */
    i = 0;
    (void)!tr_variantDictFindInt(meta, TR_KEY_creation_date, &i);
    inf->dateCreated = i;

    /* private */
    if (!tr_variantDictFindInt(infoDict, TR_KEY_private, &i) && !tr_variantDictFindInt(meta, TR_KEY_private, &i))
    {
        i = 0;
    }

    inf->isPrivate = i != 0;

    /* source */
    if (!tr_variantDictFindStrView(infoDict, TR_KEY_source, &sv) && !tr_variantDictFindStrView(meta, TR_KEY_source, &sv))
    {
        sv = ""sv;
    }

    tr_free(inf->source);
    inf->source = tr_utf8clean(sv);

    /* piece length */
    if (!isMagnet)
    {
        if (!tr_variantDictFindInt(infoDict, TR_KEY_piece_length, &i) || (i < 1))
        {
            return "piece length";
        }

        inf->pieceSize = i;
    }

    /* pieces and files */
    if (!isMagnet)
    {
        if (!tr_variantDictFindStrView(infoDict, TR_KEY_pieces, &sv))
        {
            return "pieces";
        }

        if (std::size(sv) % SHA_DIGEST_LENGTH != 0)
        {
            return "pieces";
        }

        auto const n_pieces = std::size(sv) / SHA_DIGEST_LENGTH;
        inf->pieceCount = n_pieces;
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

        if (inf->fileCount == 0 || inf->totalSize == 0)
        {
            return "files";
        }

        if ((uint64_t)inf->pieceCount != (inf->totalSize + inf->pieceSize - 1) / inf->pieceSize)
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
    tr_free(inf->torrent);
    inf->torrent = session != nullptr ? tr_strvDup(getTorrentFilename(session, inf, TR_METAINFO_BASENAME_HASH)) : nullptr;

    return nullptr;
}

std::optional<tr_metainfo_parsed> tr_metainfoParse(tr_session const* session, tr_variant const* meta_in, tr_error** error)
{
    auto out = tr_metainfo_parsed{};

    char const* bad_tag = tr_metainfoParseImpl(session, &out.info, &out.pieces, &out.info_dict_length, meta_in);
    if (bad_tag != nullptr)
    {
        tr_error_set(error, TR_ERROR_EINVAL, _("Error parsing metainfo: %s"), bad_tag);
        tr_metainfoFree(&out.info);
        return {};
    }

    return std::optional<tr_metainfo_parsed>{ std::move(out) };
}

void tr_metainfoFree(tr_info* inf)
{
    for (unsigned int i = 0; i < inf->webseedCount; i++)
    {
        tr_free(inf->webseeds[i]);
    }

    for (tr_file_index_t ff = 0; ff < inf->fileCount; ff++)
    {
        tr_free(inf->files[ff].name);
    }

    tr_free(inf->webseeds);
    tr_free(inf->files);
    tr_free(inf->comment);
    tr_free(inf->creator);
    tr_free(inf->source);
    tr_free(inf->torrent);
    tr_free(inf->originalName);
    tr_free(inf->name);

    for (unsigned int i = 0; i < inf->trackerCount; i++)
    {
        tr_free(inf->trackers[i].announce);
        tr_free(inf->trackers[i].scrape);
    }

    tr_free(inf->trackers);

    memset(inf, '\0', sizeof(tr_info));
}

void tr_metainfoRemoveSaved(tr_session const* session, tr_info const* inf)
{
    auto filename = getTorrentFilename(session, inf, TR_METAINFO_BASENAME_HASH);
    tr_sys_path_remove(filename.c_str(), nullptr);

    filename = getTorrentFilename(session, inf, TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH);
    tr_sys_path_remove(filename.c_str(), nullptr);
}

void tr_metainfoMigrateFile(
    tr_session const* session,
    tr_info const* info,
    enum tr_metainfo_basename_format old_format,
    enum tr_metainfo_basename_format new_format)
{
    auto const old_filename = getTorrentFilename(session, info, old_format);
    auto const new_filename = getTorrentFilename(session, info, new_format);

    if (tr_sys_path_rename(old_filename.c_str(), new_filename.c_str(), nullptr))
    {
        tr_logAddNamedError(
            info->name,
            "Migrated torrent file from \"%s\" to \"%s\"",
            old_filename.c_str(),
            new_filename.c_str());
    }
}
