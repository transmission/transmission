// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno> // EINVAL
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

using namespace std::literals;

namespace
{
struct optional_args
{
    std::optional<bool> paused;
    std::optional<uint16_t> peer_limit;
    std::string download_dir;
};
} // namespace

/** Opaque class used when instantiating torrents.
 * @ingroup tr_ctor */
struct tr_ctor
{
    tr_session const* const session;
    std::optional<bool> delete_source;

    tr_torrent_metainfo metainfo = {};

    tr_priority_t priority = TR_PRI_NORMAL;

    tr_torrent::labels_t labels{};

    std::array<struct optional_args, 2> optional_args{};

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

// ---

bool tr_ctorSetMetainfoFromFile(tr_ctor* ctor, std::string_view filename, tr_error** error)
{
    if (std::empty(filename))
    {
        tr_error_set(error, EINVAL, "no filename specified"sv);
        return false;
    }

    if (!tr_loadFile(filename, ctor->contents, error))
    {
        return false;
    }

    ctor->torrent_filename = filename;
    auto const contents_sv = std::string_view{ std::data(ctor->contents), std::size(ctor->contents) };
    return ctor->metainfo.parseBenc(contents_sv, error);
}

bool tr_ctorSetMetainfoFromFile(tr_ctor* ctor, char const* filename, tr_error** error)
{
    return tr_ctorSetMetainfoFromFile(ctor, std::string_view{ filename != nullptr ? filename : "" }, error);
}

bool tr_ctorSetMetainfo(tr_ctor* ctor, char const* metainfo, size_t len, tr_error** error)
{
    ctor->torrent_filename.clear();
    ctor->contents.assign(metainfo, metainfo + len);
    auto const contents_sv = std::string_view{ std::data(ctor->contents), std::size(ctor->contents) };
    return ctor->metainfo.parseBenc(contents_sv, error);
}

bool tr_ctorSetMetainfoFromMagnetLink(tr_ctor* ctor, std::string_view magnet_link, tr_error** error)
{
    ctor->torrent_filename.clear();
    ctor->metainfo = {};
    return ctor->metainfo.parseMagnet(magnet_link, error);
}

bool tr_ctorSetMetainfoFromMagnetLink(tr_ctor* ctor, char const* magnet_link, tr_error** error)
{
    return tr_ctorSetMetainfoFromMagnetLink(ctor, std::string_view{ magnet_link != nullptr ? magnet_link : "" }, error);
}

char const* tr_ctorGetSourceFile(tr_ctor const* ctor)
{
    return ctor->torrent_filename.c_str();
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

    return tr_saveFile(filename, ctor->contents, error);
}

// ---

void tr_ctorSetFilePriorities(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t file_count, tr_priority_t priority)
{
    switch (priority)
    {
    case TR_PRI_LOW:
        ctor->low.assign(files, files + file_count);
        break;

    case TR_PRI_HIGH:
        ctor->high.assign(files, files + file_count);
        break;

    default: // TR_PRI_NORMAL
        ctor->normal.assign(files, files + file_count);
        break;
    }
}

void tr_ctorInitTorrentPriorities(tr_ctor const* ctor, tr_torrent* tor)
{
    tor->setFilePriorities(std::data(ctor->low), std::size(ctor->low), TR_PRI_LOW);
    tor->setFilePriorities(std::data(ctor->normal), std::size(ctor->normal), TR_PRI_NORMAL);
    tor->setFilePriorities(std::data(ctor->high), std::size(ctor->high), TR_PRI_HIGH);
}

void tr_ctorSetFilesWanted(tr_ctor* ctor, tr_file_index_t const* files, tr_file_index_t file_count, bool wanted)
{
    auto& indices = wanted ? ctor->wanted : ctor->unwanted;
    indices.assign(files, files + file_count);
}

void tr_ctorInitTorrentWanted(tr_ctor const* ctor, tr_torrent* tor)
{
    tor->initFilesWanted(std::data(ctor->unwanted), std::size(ctor->unwanted), false);
    tor->initFilesWanted(std::data(ctor->wanted), std::size(ctor->wanted), true);
}

// ---

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

// ---

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

    ctor->optional_args[mode].download_dir.assign(directory != nullptr ? directory : "");
}

void tr_ctorSetIncompleteDir(tr_ctor* ctor, char const* directory)
{
    ctor->incomplete_dir.assign(directory != nullptr ? directory : "");
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

tr_torrent_metainfo tr_ctorStealMetainfo(tr_ctor* ctor)
{
    auto metainfo = tr_torrent_metainfo{};
    std::swap(ctor->metainfo, metainfo);
    return metainfo;
}

tr_torrent_metainfo const* tr_ctorGetMetainfo(tr_ctor const* ctor)
{
    return !std::empty(ctor->metainfo.infoHashString()) ? &ctor->metainfo : nullptr;
}

tr_session* tr_ctorGetSession(tr_ctor const* ctor)
{
    return const_cast<tr_session*>(ctor->session);
}

// ---

void tr_ctorSetBandwidthPriority(tr_ctor* ctor, tr_priority_t priority)
{
    if (priority != TR_PRI_LOW && priority != TR_PRI_NORMAL && priority != TR_PRI_HIGH)
    {
        return;
    }

    ctor->priority = priority;
}

tr_priority_t tr_ctorGetBandwidthPriority(tr_ctor const* ctor)
{
    return ctor->priority;
}

// ---

void tr_ctorSetLabels(tr_ctor* ctor, tr_quark const* labels, size_t n_labels)
{
    ctor->labels = { labels, labels + n_labels };
}

tr_torrent::labels_t const& tr_ctorGetLabels(tr_ctor const* ctor)
{
    return ctor->labels;
}

// ---

tr_ctor* tr_ctorNew(tr_session const* session)
{
    auto* const ctor = new tr_ctor{ session };

    tr_ctorSetDeleteSource(ctor, session->shouldDeleteSource());
    tr_ctorSetPaused(ctor, TR_FALLBACK, session->shouldPauseAddedTorrents());
    tr_ctorSetPeerLimit(ctor, TR_FALLBACK, session->peerLimitPerTorrent());
    tr_ctorSetDownloadDir(ctor, TR_FALLBACK, tr_sessionGetDownloadDir(session));

    return ctor;
}

void tr_ctorFree(tr_ctor* ctor)
{
    delete ctor;
}
