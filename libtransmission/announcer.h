// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
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
#include "net.h"

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

void tr_announcerAddBytes(tr_torrent*, int type, uint32_t n_bytes);

time_t tr_announcerNextManualAnnounce(tr_torrent const*);

tr_tracker_view tr_announcerTracker(tr_torrent const* torrent, size_t nth);

size_t tr_announcerTrackerCount(tr_torrent const* tor);

/// ANNOUNCE

enum tr_announce_event
{
    /* Note: the ordering of this enum's values is important to
     * announcer.c's tr_tier.announce_event_priority. If changing
     * the enum, ensure announcer.c is compatible with the change. */
    TR_ANNOUNCE_EVENT_NONE,
    TR_ANNOUNCE_EVENT_STARTED,
    TR_ANNOUNCE_EVENT_COMPLETED,
    TR_ANNOUNCE_EVENT_STOPPED,
};

struct tr_announce_request;
struct tr_announce_response;
using tr_announce_response_func = void (*)(tr_announce_response const* response, void* userdata);

struct tr_scrape_request;
struct tr_scrape_response;
using tr_scrape_response_func = void (*)(tr_scrape_response const* response, void* user_data);

/// UDP ANNOUNCER

class tr_announcer_udp
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() noexcept = default;
        virtual void sendto(void const* buf, size_t buflen, sockaddr const* addr, socklen_t addrlen) = 0;
        [[nodiscard]] virtual std::optional<tr_address> announceIP() const = 0;
    };

    virtual ~tr_announcer_udp() noexcept = default;

    [[nodiscard]] static std::unique_ptr<tr_announcer_udp> create(Mediator&);

    [[nodiscard]] virtual bool isIdle() const noexcept = 0;

    virtual void announce(tr_announce_request const& request, tr_announce_response_func response_func, void* user_data) = 0;

    virtual void scrape(tr_scrape_request const& request, tr_scrape_response_func response_func, void* user_data) = 0;

    virtual void upkeep() = 0;

    virtual void startShutdown() = 0;

    // @brief process an incoming udp message if it's a tracker response.
    // @return true if msg was a tracker response; false otherwise
    virtual bool handleMessage(uint8_t const* msg, size_t msglen) = 0;
};
