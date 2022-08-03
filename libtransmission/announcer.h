// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // uint32_t
#include <ctime>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "interned-string.h"

struct tr_announcer;
struct tr_torrent_announcer;

/**
 * ***  Tracker Publish / Subscribe
 * **/

enum TrackerEventType
{
    TR_TRACKER_WARNING,
    TR_TRACKER_ERROR,
    TR_TRACKER_ERROR_CLEAR,
    TR_TRACKER_PEERS,
    TR_TRACKER_COUNTS,
};

struct tr_pex;

/** @brief Notification object to tell listeners about announce or scrape occurrences */
struct tr_tracker_event
{
    /* what type of event this is */
    TrackerEventType messageType;

    /* for TR_TRACKER_WARNING and TR_TRACKER_ERROR */
    std::string_view text;
    tr_interned_string announce_url;

    /* for TR_TRACKER_PEERS */
    std::vector<tr_pex> pex;

    /* for TR_TRACKER_PEERS and TR_TRACKER_COUNTS */
    int leechers;
    int seeders;
};

using tr_tracker_callback = void (*)(tr_torrent* tor, tr_tracker_event const* event, void* client_data);

/**
***  Session ctor/dtor
**/

void tr_announcerInit(tr_session*);

void tr_announcerClose(tr_session*);

/**
***  For torrent customers
**/

struct tr_torrent_announcer* tr_announcerAddTorrent(tr_torrent* torrent, tr_tracker_callback callback, void* callback_data);

void tr_announcerResetTorrent(struct tr_announcer*, tr_torrent*);

void tr_announcerRemoveTorrent(struct tr_announcer*, tr_torrent*);

void tr_announcerChangeMyPort(tr_torrent*);

bool tr_announcerCanManualAnnounce(tr_torrent const*);

void tr_announcerManualAnnounce(tr_torrent*);

void tr_announcerTorrentStarted(tr_torrent*);
void tr_announcerTorrentStopped(tr_torrent*);
void tr_announcerTorrentCompleted(tr_torrent*);

enum
{
    TR_ANN_UP,
    TR_ANN_DOWN,
    TR_ANN_CORRUPT
};

void tr_announcerAddBytes(tr_torrent*, int type, uint32_t byteCount);

time_t tr_announcerNextManualAnnounce(tr_torrent const*);

tr_tracker_view tr_announcerTracker(tr_torrent const* torrent, size_t nth);

size_t tr_announcerTrackerCount(tr_torrent const* tor);

/***
****
***/

void tr_tracker_udp_upkeep(tr_session* session);

void tr_tracker_udp_close(tr_session* session);

bool tr_tracker_udp_is_idle(tr_session const* session);
