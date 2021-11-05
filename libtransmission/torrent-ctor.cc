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
#include "magnet.h"
#include "session.h" /* tr_sessionFindTorrentFile() */
#include "torrent.h" /* tr_ctorGetSave() */
#include "tr-assert.h"
#include "utils.h" /* tr_new0 */
#include "variant.h"

using namespace std::literals;

struct optional_args
{
    std::optional<bool> paused;
    std::optional<uint16_t> peer_limit;
    std::optional<std::string> download_dir;
};

/** Opaque class used when instantiating torrents.
 * @ingroup tr_ctor */
struct tr_ctor
{
    tr_session const* const session;
    bool saveInOurTorrentsDir;
    bool doDelete;

    tr_priority_t bandwidthPriority;
    bool isSet_metainfo;
    bool isSet_delete;
    tr_variant metainfo;
    std::optional<std::string> source_file;

    struct optional_args optionalArgs[2];

    char* cookies;
    std::optional<std::string> incomplete_dir;

    std::vector<tr_file_index_t> want;
    std::vector<tr_file_index_t> not_want;
    std::vector<tr_file_index_t> low;
    std::vector<tr_file_index_t> normal;
    std::vector<tr_file_index_t> high;

    tr_ctor(tr_session const* session_in)
        : session{ session_in }
    {
    }
};

/***
****
***/

static void setOptionalStringFromCStr(std::optional<std::string>& setme, char const* str)
{
    if (tr_str_is_empty(str))
    {
        setme.reset();
    }
    else
    {
        setme = str;
    }
}

static void setSourceFile(tr_ctor* ctor, char const* source_file)
{
    setOptionalStringFromCStr(ctor->source_file, source_file);
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

int tr_ctorSetMetainfo(tr_ctor* ctor, void const* metainfo, size_t len)
{
    clearMetainfo(ctor);
    auto const err = tr_variantFromBenc(&ctor->metainfo, metainfo, len);
    ctor->isSet_metainfo = err == 0;
    return err;
}

char const* tr_ctorGetSourceFile(tr_ctor const* ctor)
{
    return ctor->source_file ? ctor->source_file->c_str() : nullptr;
}

int tr_ctorSetMetainfoFromMagnetLink(tr_ctor* ctor, char const* magnet_link)
{
    auto err = int{};

    tr_magnet_info* magnet_info = tr_magnetParse(magnet_link);

    if (magnet_info == nullptr)
    {
        err = -1;
    }
    else
    {
        auto tmp = tr_variant{};
        auto len = size_t{};
        tr_magnetCreateMetainfo(magnet_info, &tmp);
        char* const str = tr_variantToStr(&tmp, TR_VARIANT_FMT_BENC, &len);
        err = tr_ctorSetMetainfo(ctor, (uint8_t const*)str, len);

        tr_free(str);
        tr_variantFree(&tmp);
        tr_magnetFree(magnet_info);
    }

    return err;
}

int tr_ctorSetMetainfoFromFile(tr_ctor* ctor, char const* filename)
{
    auto len = size_t{};
    auto* const metainfo = tr_loadFile(filename, &len, nullptr);

    auto err = int{};
    if (metainfo != nullptr && len != 0)
    {
        err = tr_ctorSetMetainfo(ctor, metainfo, len);
    }
    else
    {
        clearMetainfo(ctor);
        err = 1;
    }

    setSourceFile(ctor, filename);

    /* if no `name' field was set, then set it from the filename */
    if (ctor->isSet_metainfo)
    {
        tr_variant* info = nullptr;

        if (tr_variantDictFindDict(&ctor->metainfo, TR_KEY_info, &info))
        {
            auto name = std::string_view{};

            if (!tr_variantDictFindStrView(info, TR_KEY_name_utf_8, &name) &&
                !tr_variantDictFindStrView(info, TR_KEY_name, &name))
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
    }

    tr_free(metainfo);
    return err;
}

int tr_ctorSetMetainfoFromHash(tr_ctor* ctor, char const* hashString)
{
    char const* const filename = tr_sessionFindTorrentFile(ctor->session, hashString);
    return filename == nullptr ? EINVAL : tr_ctorSetMetainfoFromFile(ctor, filename);
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

void tr_ctorSetDeleteSource(tr_ctor* ctor, bool deleteSource)
{
    ctor->doDelete = deleteSource;
    ctor->isSet_delete = true;
}

bool tr_ctorGetDeleteSource(tr_ctor const* ctor, bool* setme)
{
    bool ret = true;

    if (!ctor->isSet_delete)
    {
        ret = false;
    }
    else if (setme != nullptr)
    {
        *setme = ctor->doDelete;
    }

    return ret;
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

    ctor->optionalArgs[mode].paused = paused;
}

void tr_ctorSetPeerLimit(tr_ctor* ctor, tr_ctorMode mode, uint16_t peer_limit)
{
    TR_ASSERT(ctor != nullptr);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    ctor->optionalArgs[mode].peer_limit = peer_limit;
}

void tr_ctorSetDownloadDir(tr_ctor* ctor, tr_ctorMode mode, char const* directory)
{
    TR_ASSERT(ctor != nullptr);
    TR_ASSERT(mode == TR_FALLBACK || mode == TR_FORCE);

    struct optional_args* args = &ctor->optionalArgs[mode];

    setOptionalStringFromCStr(ctor->download_dir, directory);
}

void tr_ctorSetIncompleteDir(tr_ctor* ctor, char const* directory)
{
    setOptionalStringFromCStr(ctor->incomplete_dir, directory);
}

bool tr_ctorGetPeerLimit(tr_ctor const* ctor, tr_ctorMode mode, uint16_t* setme)
{
    optional_args const& args = ctor->optionalArgs[mode];
    if (!args.peer_limit)
    {
        return false;
    }

    *setme = *args.peer_limit;
    return true;
}

bool tr_ctorGetPaused(tr_ctor const* ctor, tr_ctorMode mode, bool* setme)
{
    optional_args const& args = ctor->optionalArgs[mode];
    if (!args.paused)
    {
        return false;
    }

    *setme = *args.paused;
    return true;
}

bool tr_ctorGetDownloadDir(tr_ctor const* ctor, tr_ctorMode mode, char const** setme)
{
    optional_args const& args = ctor->optionalArgs[mode];
    if (!args.download_dir)
    {
        return false;
    }

    *setme = args.download_dir->c_str();
    return true;
}

bool tr_ctorGetIncompleteDir(tr_ctor const* ctor, char const** setme)
{
    if (!ctor->incomplete_dir)
    {
        return false;
    }

    *setme = ctor->incomplete_dir->c_str();
    return true;
}

bool tr_ctorGetMetainfo(tr_ctor const* ctor, tr_variant const** setme)
{
    bool ret = true;

    if (!ctor->isSet_metainfo)
    {
        ret = false;
    }
    else if (setme != nullptr)
    {
        *setme = &ctor->metainfo;
    }

    return ret;
}

tr_session* tr_ctorGetSession(tr_ctor const* ctor)
{
    return (tr_session*)ctor->session;
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
        ctor->bandwidthPriority = priority;
    }
}

tr_priority_t tr_ctorGetBandwidthPriority(tr_ctor const* ctor)
{
    return ctor->bandwidthPriority;
}

/***
****
***/

tr_ctor* tr_ctorNew(tr_session const* session)
{
    auto* const ctor = new tr_ctor{ session };

    ctor->bandwidthPriority = TR_PRI_NORMAL;

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
