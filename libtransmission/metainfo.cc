/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <array>
#include <cstring> /* strlen() */
#include <string_view>

#include <event2/buffer.h>

#include "transmission.h"

#include "crypto-utils.h" /* tr_sha1 */
#include "file.h"
#include "log.h"
#include "metainfo.h"
#include "platform.h" /* tr_getTorrentDir() */
#include "session.h"
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"

using namespace std::literals;

/***
****
***/

#ifdef _WIN32
auto constexpr PATH_DELIMITER_CHARS = std::array<char, 2>{ '/', '\\' };
#else
auto constexpr PATH_DELIMITER_CHARS = std::array<char, 1>{ '/' };
#endif

static constexpr bool char_is_path_separator(char c)
{
    for (auto ch : PATH_DELIMITER_CHARS)
    {
        if (c == ch)
        {
            return true;
        }
    }

    return false;
}

static char* metainfoGetBasenameNameAndPartialHash(tr_info const* inf)
{
    char const* name = inf->originalName;
    size_t const name_len = strlen(name);
    char* ret = tr_strdup_printf("%s.%16.16s", name, inf->hashString);

    for (size_t i = 0; i < name_len; ++i)
    {
        if (char_is_path_separator(ret[i]))
        {
            ret[i] = '_';
        }
    }

    return ret;
}

static char* metainfoGetBasenameHashOnly(tr_info const* inf)
{
    return tr_strdup(inf->hashString);
}

char* tr_metainfoGetBasename(tr_info const* inf, enum tr_metainfo_basename_format format)
{
    switch (format)
    {
    case TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH:
        return metainfoGetBasenameNameAndPartialHash(inf);

    case TR_METAINFO_BASENAME_HASH:
        return metainfoGetBasenameHashOnly(inf);

    default:
        TR_ASSERT_MSG(false, "unknown metainfo basename format %d", (int)format);
        return nullptr;
    }
}

static char* getTorrentFilename(tr_session const* session, tr_info const* inf, enum tr_metainfo_basename_format format)
{
    char* base = tr_metainfoGetBasename(inf, format);
    char* filename = tr_strdup_printf("%s" TR_PATH_DELIMITER_STR "%s.torrent", tr_getTorrentDir(session), base);
    tr_free(base);
    return filename;
}

/***
****
***/

char* tr_metainfo_sanitize_path_component(char const* str, size_t len, bool* is_adjusted)
{
    if (len == 0 || (len == 1 && str[0] == '.'))
    {
        return nullptr;
    }

    *is_adjusted = false;

    /* https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file */
    char const* const reserved_chars = "<>:\"/\\|?*";
    auto constexpr ReservedNames = std::array<std::string_view, 22>{
        "CON"sv,  "PRN"sv,  "AUX"sv,  "NUL"sv,  "COM1"sv, "COM2"sv, "COM3"sv, "COM4"sv, "COM5"sv, "COM6"sv, "COM7"sv,
        "COM8"sv, "COM9"sv, "LPT1"sv, "LPT2"sv, "LPT3"sv, "LPT4"sv, "LPT5"sv, "LPT6"sv, "LPT7"sv, "LPT8"sv, "LPT9"sv,
    };

    char* const ret = tr_new(char, len + 2);
    memcpy(ret, str, len);
    ret[len] = '\0';

    for (size_t i = 0; i < len; ++i)
    {
        if (strchr(reserved_chars, ret[i]) != nullptr || (unsigned char)ret[i] < 0x20)
        {
            ret[i] = '_';
            *is_adjusted = true;
        }
    }

    for (auto const& reserved_name : ReservedNames)
    {
        size_t const reserved_name_len = std::size(reserved_name);
        if (evutil_ascii_strncasecmp(ret, std::data(reserved_name), reserved_name_len) != 0 ||
            (ret[reserved_name_len] != '\0' && ret[reserved_name_len] != '.'))
        {
            continue;
        }

        memmove(&ret[reserved_name_len + 1], &ret[reserved_name_len], len - reserved_name_len + 1);
        ret[reserved_name_len] = '_';
        *is_adjusted = true;
        ++len;
        break;
    }

    size_t start_pos = 0;
    size_t end_pos = len;

    while (start_pos < len && ret[start_pos] == ' ')
    {
        ++start_pos;
    }

    while (end_pos > start_pos && (ret[end_pos - 1] == ' ' || ret[end_pos - 1] == '.'))
    {
        --end_pos;
    }

    if (start_pos == end_pos)
    {
        tr_free(ret);
        return nullptr;
    }

    if (start_pos != 0 || end_pos != len)
    {
        len = end_pos - start_pos;
        memmove(ret, &ret[start_pos], len);
        ret[len] = '\0';
        *is_adjusted = true;
    }

    return ret;
}

static bool getfile(char** setme, bool* is_adjusted, char const* root, tr_variant* path, struct evbuffer* buf)
{
    bool success = false;
    size_t root_len = 0;

    *setme = nullptr;
    *is_adjusted = false;

    if (tr_variantIsList(path))
    {
        success = true;
        evbuffer_drain(buf, evbuffer_get_length(buf));
        root_len = strlen(root);
        evbuffer_add(buf, root, root_len);

        for (int i = 0, n = tr_variantListSize(path); i < n; i++)
        {
            size_t len = 0;
            char const* str = nullptr;
            if (!tr_variantGetStr(tr_variantListChild(path, i), &str, &len))
            {
                success = false;
                break;
            }

            bool is_component_adjusted = false;
            char* final_str = tr_metainfo_sanitize_path_component(str, len, &is_component_adjusted);
            if (final_str == nullptr)
            {
                continue;
            }

            *is_adjusted = *is_adjusted || is_component_adjusted;

            evbuffer_add(buf, TR_PATH_DELIMITER_STR, 1);
            evbuffer_add(buf, final_str, strlen(final_str));

            tr_free(final_str);
        }
    }

    if (success && evbuffer_get_length(buf) <= root_len)
    {
        success = false;
    }

    if (success)
    {
        char const* const buf_data = (char*)evbuffer_pullup(buf, -1);
        size_t const buf_len = evbuffer_get_length(buf);

        *setme = tr_utf8clean(std::string_view{ buf_data, buf_len });

        if (!*is_adjusted)
        {
            *is_adjusted = buf_len != strlen(*setme) || strncmp(buf_data, *setme, buf_len) != 0;
        }
    }

    return success;
}

static char const* parseFiles(tr_info* inf, tr_variant* files, tr_variant const* length)
{
    int64_t len = 0;
    inf->totalSize = 0;

    bool is_root_adjusted = false;
    char* const root_name = tr_metainfo_sanitize_path_component(inf->name, strlen(inf->name), &is_root_adjusted);
    if (root_name == nullptr)
    {
        return "path";
    }

    char const* result = nullptr;

    if (tr_variantIsList(files)) /* multi-file mode */
    {
        evbuffer* const buf = evbuffer_new();
        result = nullptr;

        inf->isFolder = true;
        inf->fileCount = tr_variantListSize(files);
        inf->files = tr_new0(tr_file, inf->fileCount);

        for (tr_file_index_t i = 0; i < inf->fileCount; i++)
        {
            auto* const file = tr_variantListChild(files, i);

            if (!tr_variantIsDict(file))
            {
                result = "files";
                break;
            }

            tr_variant* path = nullptr;
            if (!tr_variantDictFindList(file, TR_KEY_path_utf_8, &path) && !tr_variantDictFindList(file, TR_KEY_path, &path))
            {
                result = "path";
                break;
            }

            bool is_file_adjusted = false;
            if (!getfile(&inf->files[i].name, &is_file_adjusted, root_name, path, buf))
            {
                result = "path";
                break;
            }

            if (!tr_variantDictFindInt(file, TR_KEY_length, &len))
            {
                result = "length";
                break;
            }

            inf->files[i].length = len;
            inf->files[i].is_renamed = is_root_adjusted || is_file_adjusted;
            inf->totalSize += len;
        }

        evbuffer_free(buf);
    }
    else if (tr_variantGetInt(length, &len)) /* single-file mode */
    {
        inf->isFolder = false;
        inf->fileCount = 1;
        inf->files = tr_new0(tr_file, 1);
        inf->files[0].name = tr_strdup(root_name);
        inf->files[0].length = len;
        inf->files[0].is_renamed = is_root_adjusted;
        inf->totalSize += len;
    }
    else
    {
        result = "length";
    }

    tr_free(root_name);
    return result;
}

static char* tr_convertAnnounceToScrape(char const* announce)
{
    char* scrape = nullptr;

    /* To derive the scrape URL use the following steps:
     * Begin with the announce URL. Find the last '/' in it.
     * If the text immediately following that '/' isn't 'announce'
     * it will be taken as a sign that that tracker doesn't support
     * the scrape convention. If it does, substitute 'scrape' for
     * 'announce' to find the scrape page. */

    char const* s = strrchr(announce, '/');

    if (s != nullptr && strncmp(s + 1, "announce", 8) == 0)
    {
        char const* prefix = announce;
        size_t const prefix_len = s + 1 - announce;
        char const* suffix = s + 1 + 8;
        size_t const suffix_len = strlen(suffix);
        size_t const alloc_len = prefix_len + 6 + suffix_len + 1;

        scrape = tr_new(char, alloc_len);

        char* walk = scrape;
        memcpy(walk, prefix, prefix_len);
        walk += prefix_len;
        memcpy(walk, "scrape", 6);
        walk += 6;
        memcpy(walk, suffix, suffix_len);
        walk += suffix_len;
        *walk++ = '\0';

        TR_ASSERT((size_t)(walk - scrape) == alloc_len);
    }
    /* Some torrents with UDP announce URLs don't have /announce. */
    else if (strncmp(announce, "udp:", 4) == 0)
    {
        scrape = tr_strdup(announce);
    }

    return scrape;
}

static char const* getannounce(tr_info* inf, tr_variant* meta)
{
    size_t len = 0;
    char const* str = nullptr;
    tr_tracker_info* trackers = nullptr;
    int trackerCount = 0;
    tr_variant* tiers = nullptr;

    /* Announce-list */
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
                if (tr_variantGetStr(tr_variantListChild(tier, j), &str, &len))
                {
                    char* url = tr_strstrip(tr_strndup(str, len));

                    if (!tr_urlIsValidTracker(url))
                    {
                        tr_free(url);
                    }
                    else
                    {
                        tr_tracker_info* t = trackers + trackerCount;
                        t->tier = validTiers;
                        t->announce = url;
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
    if (trackerCount == 0 && tr_variantDictFindStr(meta, TR_KEY_announce, &str, &len))
    {
        char* url = tr_strstrip(tr_strndup(str, len));

        if (!tr_urlIsValidTracker(url))
        {
            tr_free(url);
        }
        else
        {
            trackers = tr_new0(tr_tracker_info, 1);
            trackers[trackerCount].tier = 0;
            trackers[trackerCount].announce = url;
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
static char* fix_webseed_url(tr_info const* inf, char const* url_in)
{
    char* ret = nullptr;

    char* const url = tr_strdup(url_in);
    tr_strstrip(url);
    size_t const len = strlen(url);

    if (tr_urlIsValid(url, len))
    {
        if (inf->fileCount > 1 && len > 0 && url[len - 1] != '/')
        {
            ret = tr_strdup_printf("%*.*s/", TR_ARG_TUPLE((int)len, (int)len, url));
        }
        else
        {
            ret = tr_strndup(url, len);
        }
    }

    tr_free(url);
    return ret;
}

static void geturllist(tr_info* inf, tr_variant* meta)
{
    tr_variant* urls = nullptr;
    char const* url = nullptr;

    if (tr_variantDictFindList(meta, TR_KEY_url_list, &urls))
    {
        int const n = tr_variantListSize(urls);

        inf->webseedCount = 0;
        inf->webseeds = tr_new0(char*, n);

        for (int i = 0; i < n; i++)
        {
            if (tr_variantGetStr(tr_variantListChild(urls, i), &url, nullptr))
            {
                char* fixed_url = fix_webseed_url(inf, url);

                if (fixed_url != nullptr)
                {
                    inf->webseeds[inf->webseedCount++] = fixed_url;
                }
            }
        }
    }
    else if (tr_variantDictFindStr(meta, TR_KEY_url_list, &url, nullptr)) /* handle single items in webseeds */
    {
        char* fixed_url = fix_webseed_url(inf, url);

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
    bool* hasInfoDict,
    size_t* infoDictLength,
    tr_variant const* meta_in)
{
    int64_t i = 0;
    size_t len = 0;
    char const* str = nullptr;
    uint8_t const* raw = nullptr;
    tr_variant* const meta = const_cast<tr_variant*>(meta_in);
    bool isMagnet = false;

    /* info_hash: urlencoded 20-byte SHA1 hash of the value of the info key
     * from the Metainfo file. Note that the value will be a bencoded
     * dictionary, given the definition of the info key above. */
    tr_variant* infoDict = nullptr;
    bool b = tr_variantDictFindDict(meta, TR_KEY_info, &infoDict);

    if (hasInfoDict != nullptr)
    {
        *hasInfoDict = b;
    }

    if (!b)
    {
        /* no info dictionary... is this a magnet link? */
        tr_variant* d = nullptr;
        if (tr_variantDictFindDict(meta, TR_KEY_magnet_info, &d))
        {
            isMagnet = true;

            /* get the info-hash */
            if (!tr_variantDictFindRaw(d, TR_KEY_info_hash, &raw, &len))
            {
                return "info_hash";
            }

            if (len != SHA_DIGEST_LENGTH)
            {
                return "info_hash";
            }

            memcpy(inf->hash, raw, len);
            tr_sha1_to_hex(inf->hashString, inf->hash);

            /* maybe get the display name */
            if (tr_variantDictFindStr(d, TR_KEY_display_name, &str, &len))
            {
                tr_free(inf->name);
                tr_free(inf->originalName);
                inf->name = tr_strndup(str, len);
                inf->originalName = tr_strndup(str, len);
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
        else /* not a magnet link and has no info dict... */
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
        len = 0;

        if (!tr_variantDictFindStr(infoDict, TR_KEY_name_utf_8, &str, &len) &&
            !tr_variantDictFindStr(infoDict, TR_KEY_name, &str, &len))
        {
            str = "";
        }

        if (tr_str_is_empty(str))
        {
            return "name";
        }

        tr_free(inf->name);
        tr_free(inf->originalName);
        inf->name = tr_utf8clean(std::string_view{ str, len });
        inf->originalName = tr_strdup(inf->name);
    }

    /* comment */
    len = 0;

    if (!tr_variantDictFindStr(meta, TR_KEY_comment_utf_8, &str, &len) &&
        !tr_variantDictFindStr(meta, TR_KEY_comment, &str, &len))
    {
        str = "";
    }

    tr_free(inf->comment);
    inf->comment = tr_utf8clean(std::string_view{ str, len });

    /* created by */
    len = 0;

    if (!tr_variantDictFindStr(meta, TR_KEY_created_by_utf_8, &str, &len) &&
        !tr_variantDictFindStr(meta, TR_KEY_created_by, &str, &len))
    {
        str = "";
    }

    tr_free(inf->creator);
    inf->creator = tr_utf8clean(std::string_view{ str, len });

    /* creation date */
    if (!tr_variantDictFindInt(meta, TR_KEY_creation_date, &i))
    {
        i = 0;
    }

    inf->dateCreated = i;

    /* private */
    if (!tr_variantDictFindInt(infoDict, TR_KEY_private, &i) && !tr_variantDictFindInt(meta, TR_KEY_private, &i))
    {
        i = 0;
    }

    inf->isPrivate = i != 0;

    /* source */
    len = 0;
    if (!tr_variantDictFindStr(infoDict, TR_KEY_source, &str, &len))
    {
        if (!tr_variantDictFindStr(meta, TR_KEY_source, &str, &len))
        {
            str = "";
        }
    }

    tr_free(inf->source);
    inf->source = tr_utf8clean(std::string_view{ str, len });

    /* piece length */
    if (!isMagnet)
    {
        if (!tr_variantDictFindInt(infoDict, TR_KEY_piece_length, &i) || (i < 1))
        {
            return "piece length";
        }

        inf->pieceSize = i;
    }

    /* pieces */
    if (!isMagnet)
    {
        if (!tr_variantDictFindRaw(infoDict, TR_KEY_pieces, &raw, &len))
        {
            return "pieces";
        }

        if (len % SHA_DIGEST_LENGTH != 0)
        {
            return "pieces";
        }

        inf->pieceCount = len / SHA_DIGEST_LENGTH;
        inf->pieces = tr_new0(tr_sha1_digest_t, inf->pieceCount);
        std::copy_n(raw, len, (uint8_t*)(inf->pieces));
    }

    /* files */
    if (!isMagnet)
    {
        if ((str = parseFiles(inf, tr_variantDictFind(infoDict, TR_KEY_files), tr_variantDictFind(infoDict, TR_KEY_length))) !=
            nullptr)
        {
            return str;
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
    if ((str = getannounce(inf, meta)) != nullptr)
    {
        return str;
    }

    /* get the url-list */
    geturllist(inf, meta);

    /* filename of Transmission's copy */
    tr_free(inf->torrent);
    inf->torrent = session != nullptr ? getTorrentFilename(session, inf, TR_METAINFO_BASENAME_HASH) : nullptr;

    return nullptr;
}

bool tr_metainfoParse(
    tr_session const* session,
    tr_variant const* meta_in,
    tr_info* inf,
    bool* hasInfoDict,
    size_t* infoDictLength)
{
    char const* badTag = tr_metainfoParseImpl(session, inf, hasInfoDict, infoDictLength, meta_in);
    bool const success = badTag == nullptr;

    if (badTag != nullptr)
    {
        tr_logAddNamedError(inf->name, _("Invalid metadata entry \"%s\""), badTag);
        tr_metainfoFree(inf);
    }

    return success;
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
    tr_free(inf->pieces);
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
    char* filename = getTorrentFilename(session, inf, TR_METAINFO_BASENAME_HASH);
    tr_sys_path_remove(filename, nullptr);
    tr_free(filename);

    filename = getTorrentFilename(session, inf, TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH);
    tr_sys_path_remove(filename, nullptr);
    tr_free(filename);
}

void tr_metainfoMigrateFile(
    tr_session const* session,
    tr_info const* info,
    enum tr_metainfo_basename_format old_format,
    enum tr_metainfo_basename_format new_format)
{
    char* old_filename = getTorrentFilename(session, info, old_format);
    char* new_filename = getTorrentFilename(session, info, new_format);

    if (tr_sys_path_rename(old_filename, new_filename, nullptr))
    {
        tr_logAddNamedError(info->name, "Migrated torrent file from \"%s\" to \"%s\"", old_filename, new_filename);
    }

    tr_free(new_filename);
    tr_free(old_filename);
}
