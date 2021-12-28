/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cerrno> // EINVAL
#include <optional>
#include <string>
#include <vector>

#include "transmission.h"

#include "error.h"
#include "error-types.h"
#include "magnet-metainfo.h"
#include "session.h"
#include "torrent-metainfo.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"
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

    tr_torrent_metainfo metainfo = {};

    tr_priority_t priority = TR_PRI_NORMAL;

    struct optional_args optional_args[2];

    std::string incomplete_dir;
    std::string torrent_filename;

    std::vector<tr_file_index_t> wanted;
    std::vector<tr_file_index_t> unwanted;
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

bool tr_ctorSetMetainfoFromFile(tr_ctor* ctor, char const* filename, tr_error** error)
{
    if (filename == nullptr)
    {
        tr_error_set(error, EINVAL, "no filename specified"sv);
        return false;
    }

    if (!tr_loadFile(ctor->contents, filename, error))
    {
        return false;
    }

    ctor->metainfo.clear();
    auto const contents_sv = std::string_view{ std::data(ctor->contents), std::size(ctor->contents) };
    return ctor->metainfo.parseBenc(contents_sv, error);
}

bool tr_ctorSetMetainfo(tr_ctor* ctor, char const* metainfo, size_t len, tr_error** error)
{
    ctor->metainfo.clear();
    ctor->contents.assign(metainfo, metainfo + len);
    auto const contents_sv = std::string_view{ std::data(ctor->contents), std::size(ctor->contents) };
    return ctor->metainfo.parseBenc(contents_sv, error);
}

bool tr_ctorSetMetainfoFromMagnetLink(tr_ctor* ctor, char const* magnet_link, tr_error** error)
{
    ctor->metainfo.clear();
    return ctor->metainfo.parseMagnet(magnet_link ? magnet_link : "", error);
}

std::string_view tr_ctorGetContents(tr_ctor const* ctor)
{
    return std::string_view{ std::data(ctor->contents), std::size(ctor->contents) };
}

char const* tr_ctorGetSourceFile(tr_ctor const* ctor)
{
    return ctor->metainfo.parsedTorrentFile().c_str();
}

bool tr_ctorSaveContents(tr_ctor const* ctor, std::string_view filename, tr_error** error)
{
    TR_ASSERT(ctor != nullptr);
    TR_ASSERT(!std::empty(filename));

    if (std::empty(ctor->contents))
    {
        tr_error_set(error, EINVAL, "torrent ctor has no contents to save"sv);
        return false;
    }

    return tr_saveFile(filename, { std::data(ctor->contents), std::size(ctor->contents) }, error);
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
    tor->setFilePriorities(std::data(ctor->low), std::size(ctor->low), TR_PRI_LOW);
    tor->setFilePriorities(std::data(ctor->normal), std::size(ctor->normal), TR_PRI_NORMAL);
    tor->setFilePriorities(std::data(ctor->high), std::size(ctor->high), TR_PRI_HIGH);
}

void tr_ctorSetFilesWanted(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t fileCount, bool wanted)
{
    auto& indices = wanted ? ctor->wanted : ctor->unwanted;
    indices.assign(files, files + fileCount);
}

void tr_ctorInitTorrentWanted(tr_ctor const* ctor, tr_torrent* tor)
{
    tor->initFilesWanted(std::data(ctor->unwanted), std::size(ctor->unwanted), false);
    tor->initFilesWanted(std::data(ctor->wanted), std::size(ctor->wanted), true);
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

tr_torrent_metainfo const* tr_ctorGetMetainfo(tr_ctor const* ctor)
{
    return std::empty(ctor->metainfo.files()) ? nullptr : &ctor->metainfo;
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

    tr_ctorSetDeleteSource(ctor, tr_sessionGetDeleteSource(session));
    tr_ctorSetPaused(ctor, TR_FALLBACK, tr_sessionGetPaused(session));
    tr_ctorSetPeerLimit(ctor, TR_FALLBACK, session->peerLimitPerTorrent);
    tr_ctorSetDownloadDir(ctor, TR_FALLBACK, tr_sessionGetDownloadDir(session));

    tr_ctorSetSave(ctor, true);
    return ctor;
}

void tr_ctorFree(tr_ctor* ctor)
{
    delete ctor;
}
