/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cerrno> /* EINVAL */
#include <optional>
#include <string>
#include <vector>

#include "transmission.h"
#include "file.h"
#include "magnet-metainfo.h"
#include "session.h"
#include "torrent.h" /* tr_ctorGetSave() */
#include "tr-assert.h"
#include "utils.h" /* tr_new0 */
#include "variant.h"

using namespace std::literals;

struct optional_args
{
    std::optional<bool> paused;
    std::optional<uint16_t> peer_limit;
    std::string download_dir;
};

/** Opaque class used when instantiating torrents.
 * @ingroup tr_ctor */
struct tr_ctor
{
    tr_session const* const session;
    bool saveInOurTorrentsDir = false;
    std::optional<bool> delete_source;

    tr_priority_t priority = TR_PRI_NORMAL;
    bool isSet_metainfo = false;
    tr_variant metainfo = {};
    std::string source_file;

    struct optional_args optional_args[2];

    std::string incomplete_dir;

    std::vector<tr_file_index_t> want;
    std::vector<tr_file_index_t> not_want;
    std::vector<tr_file_index_t> low;
    std::vector<tr_file_index_t> normal;
    std::vector<tr_file_index_t> high;

    std::vector<char> contents;

    explicit tr_ctor(tr_session const* session_in)
        : session{ session_in }
    {
    }
};

/***
****
***/

static void setSourceFile(tr_ctor* ctor, char const* source_file)
{
    ctor->source_file.assign(source_file ? source_file : "");
}

static void clearMetainfo(tr_ctor* ctor)
{
    if (ctor->isSet_metainfo)
    {
        ctor->isSet_metainfo = false;
        tr_variantFree(&ctor->metainfo);
    }

    setSourceFile(ctor, nullptr);
}

static int parseMetainfoContents(tr_ctor* ctor)
{
    auto& contents = ctor->contents;
    auto sv = std::string_view{ std::data(contents), std::size(contents) };
    ctor->isSet_metainfo = tr_variantFromBuf(&ctor->metainfo, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, sv);
    return ctor->isSet_metainfo ? 0 : EILSEQ;
}

int tr_ctorSetMetainfo(tr_ctor* ctor, void const* metainfo, size_t len)
{
    clearMetainfo(ctor);

    ctor->contents.resize(len);
    std::copy_n(static_cast<char const*>(metainfo), len, std::begin(ctor->contents));

    return parseMetainfoContents(ctor);
}

char const* tr_ctorGetSourceFile(tr_ctor const* ctor)
{
    return ctor->source_file.c_str();
}

int tr_ctorSetMetainfoFromMagnetLink(tr_ctor* ctor, char const* magnet_link)
{
    auto mm = tr_magnet_metainfo{};
    if (!mm.parseMagnet(magnet_link ? magnet_link : ""))
    {
        return -1;
    }

    auto tmp = tr_variant{};
    mm.toVariant(&tmp);
    auto len = size_t{};
    char* const str = tr_variantToStr(&tmp, TR_VARIANT_FMT_BENC, &len);
    auto const err = tr_ctorSetMetainfo(ctor, (uint8_t const*)str, len);
    tr_free(str);
    tr_variantFree(&tmp);

    return err;
}

int tr_ctorSetMetainfoFromFile(tr_ctor* ctor, char const* filename)
{
    clearMetainfo(ctor);

    if (!tr_loadFile(ctor->contents, filename, nullptr) || std::empty(ctor->contents))
    {
        return EILSEQ;
    }

    if (int const err = parseMetainfoContents(ctor); err != 0)
    {
        clearMetainfo(ctor);
        return err;
    }

    setSourceFile(ctor, filename);

    /* if no `name' field was set, then set it from the filename */
    if (tr_variant* info = nullptr; tr_variantDictFindDict(&ctor->metainfo, TR_KEY_info, &info))
    {
        auto name = std::string_view{};

        if (!tr_variantDictFindStrView(info, TR_KEY_name_utf_8, &name) && !tr_variantDictFindStrView(info, TR_KEY_name, &name))
        {
            name = ""sv;
        }

        if (std::empty(name))
        {
            char* base = tr_sys_path_basename(filename, nullptr);

            if (base != nullptr)
            {
                tr_variantDictAddStr(info, TR_KEY_name, base);
                tr_free(base);
            }
        }
    }

    return 0;
}

/***
****
***/

void tr_ctorSetFilePriorities(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t fileCount, tr_priority_t priority)
{
    switch (priority)
    {
    case TR_PRI_LOW:
        ctor->low.assign(files, files + fileCount);
        break;

    case TR_PRI_HIGH:
        ctor->high.assign(files, files + fileCount);
        break;

    default: // TR_PRI_NORMAL
        ctor->normal.assign(files, files + fileCount);
        break;
    }
}

void tr_ctorInitTorrentPriorities(tr_ctor const* ctor, tr_torrent* tor)
{
    for (auto file_index : ctor->low)
    {
        tr_torrentInitFilePriority(tor, file_index, TR_PRI_LOW);
    }

    for (auto file_index : ctor->normal)
    {
        tr_torrentInitFilePriority(tor, file_index, TR_PRI_NORMAL);
    }

    for (auto file_index : ctor->high)
    {
        tr_torrentInitFilePriority(tor, file_index, TR_PRI_HIGH);
    }
}

void tr_ctorSetFilesWanted(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t fileCount, bool wanted)
{
    auto& indices = wanted ? ctor->want : ctor->not_want;
    indices.assign(files, files + fileCount);
}

void tr_ctorInitTorrentWanted(tr_ctor const* ctor, tr_torrent* tor)
{
    tr_torrentInitFileDLs(tor, std::data(ctor->not_want), std::size(ctor->not_want), false);
    tr_torrentInitFileDLs(tor, std::data(ctor->want), std::size(ctor->want), true);
}

/***
****
***/

void tr_ctorSetDeleteSource(tr_ctor* ctor, bool delete_source)
{
    ctor->delete_source = delete_source;
}

bool tr_ctorGetDeleteSource(tr_ctor const* ctor, bool* setme)
{
    auto const& delete_source = ctor->delete_source;
    if (!delete_source)
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = *delete_source;
    }

    return true;
}

/***
****
***/

void tr_ctorSetSave(tr_ctor* ctor, bool saveInOurTorrentsDir)
{
    ctor->saveInOurTorrentsDir = saveInOurTorrentsDir;
}

bool tr_ctorGetSave(tr_ctor const* ctor)
{
    return ctor != nullptr && ctor->saveInOurTorrentsDir;
}

void tr_ctorSetPaused(tr_ctor* ctor, tr_ctorMode mode, bool paused)
{
    TR_ASSERT(ctor != nullptr);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    ctor->optional_args[mode].paused = paused;
}

void tr_ctorSetPeerLimit(tr_ctor* ctor, tr_ctorMode mode, uint16_t peer_limit)
{
    TR_ASSERT(ctor != nullptr);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    ctor->optional_args[mode].peer_limit = peer_limit;
}

void tr_ctorSetDownloadDir(tr_ctor* ctor, tr_ctorMode mode, char const* directory)
{
    TR_ASSERT(ctor != nullptr);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    ctor->optional_args[mode].download_dir.assign(directory ? directory : "");
}

void tr_ctorSetIncompleteDir(tr_ctor* ctor, char const* directory)
{
    ctor->incomplete_dir.assign(directory ? directory : "");
}

bool tr_ctorGetPeerLimit(tr_ctor const* ctor, tr_ctorMode mode, uint16_t* setme)
{
    auto const& peer_limit = ctor->optional_args[mode].peer_limit;
    if (!peer_limit)
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = *peer_limit;
    }

    return true;
}

bool tr_ctorGetPaused(tr_ctor const* ctor, tr_ctorMode mode, bool* setme)
{
    auto const& paused = ctor->optional_args[mode].paused;
    if (!paused)
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = *paused;
    }

    return true;
}

bool tr_ctorGetDownloadDir(tr_ctor const* ctor, tr_ctorMode mode, char const** setme)
{
    auto const& str = ctor->optional_args[mode].download_dir;
    if (std::empty(str))
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = str.c_str();
    }

    return true;
}

bool tr_ctorGetIncompleteDir(tr_ctor const* ctor, char const** setme)
{
    auto const& str = ctor->incomplete_dir;
    if (std::empty(str))
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = str.c_str();
    }

    return true;
}

bool tr_ctorGetMetainfo(tr_ctor const* ctor, tr_variant const** setme)
{
    if (!ctor->isSet_metainfo)
    {
        return false;
    }

    if (setme != nullptr)
    {
        *setme = &ctor->metainfo;
    }

    return true;
}

tr_session* tr_ctorGetSession(tr_ctor const* ctor)
{
    return const_cast<tr_session*>(ctor->session);
}

/***
****
***/

static bool isPriority(int i)
{
    return i == TR_PRI_LOW || i == TR_PRI_NORMAL || i == TR_PRI_HIGH;
}

void tr_ctorSetBandwidthPriority(tr_ctor* ctor, tr_priority_t priority)
{
    if (isPriority(priority))
    {
        ctor->priority = priority;
    }
}

tr_priority_t tr_ctorGetBandwidthPriority(tr_ctor const* ctor)
{
    return ctor->priority;
}

/***
****
***/

tr_ctor* tr_ctorNew(tr_session const* session)
{
    auto* const ctor = new tr_ctor{ session };

    if (session != nullptr)
    {
        tr_ctorSetDeleteSource(ctor, tr_sessionGetDeleteSource(session));
        tr_ctorSetPaused(ctor, TR_FALLBACK, tr_sessionGetPaused(session));
        tr_ctorSetPeerLimit(ctor, TR_FALLBACK, session->peerLimitPerTorrent);
        tr_ctorSetDownloadDir(ctor, TR_FALLBACK, tr_sessionGetDownloadDir(session));
    }

    tr_ctorSetSave(ctor, true);
    return ctor;
}

void tr_ctorFree(tr_ctor* ctor)
{
    clearMetainfo(ctor);
    delete ctor;
}
