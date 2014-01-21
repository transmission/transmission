/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef _TR_ANNOUNCER_H_
#define _TR_ANNOUNCER_H_

#include "transmission.h"

struct tr_announcer;
struct tr_torrent_tiers;

/**
 * ***  Tracker Publish / Subscribe
 * **/

typedef enum
{
    TR_TRACKER_WARNING,
    TR_TRACKER_ERROR,
    TR_TRACKER_ERROR_CLEAR,
    TR_TRACKER_PEERS
}
TrackerEventType;

struct tr_pex;

/** @brief Notification object to tell listeners about announce or scrape occurences */
typedef struct
{
    /* what type of event this is */
    TrackerEventType    messageType;

    /* for TR_TRACKER_WARNING and TR_TRACKER_ERROR */
    const char * text;
    const char * tracker;

    /* for TR_TRACKER_PEERS */
    const struct tr_pex * pex;
    size_t pexCount;

    /* [0...100] for probability a peer is a seed. calculated by the leecher/seeder ratio */
    int8_t           seedProbability;
}
tr_tracker_event;

typedef void (*tr_tracker_callback) (tr_torrent              * tor,
                                     const tr_tracker_event  * event,
                                     void                    * client_data);

/**
***  Session ctor/dtor
**/

void tr_announcerInit (tr_session *);

void tr_announcerClose (tr_session *);

/**
***  For torrent customers
**/

struct tr_torrent_tiers * tr_announcerAddTorrent (tr_torrent          * torrent,
                                                  tr_tracker_callback   cb,
                                                  void                * cbdata);

bool tr_announcerHasBacklog (const struct tr_announcer *);

void tr_announcerResetTorrent (struct tr_announcer*, tr_torrent*);

void tr_announcerRemoveTorrent (struct tr_announcer * ,
                                tr_torrent          *);

void tr_announcerChangeMyPort (tr_torrent *);

bool tr_announcerCanManualAnnounce (const tr_torrent *);

void tr_announcerManualAnnounce (tr_torrent *);

void tr_announcerTorrentStarted (tr_torrent *);
void tr_announcerTorrentStopped (tr_torrent *);
void tr_announcerTorrentCompleted (tr_torrent *);

enum { TR_ANN_UP, TR_ANN_DOWN, TR_ANN_CORRUPT };
void tr_announcerAddBytes (tr_torrent *, int up_down_or_corrupt, uint32_t byteCount);

time_t tr_announcerNextManualAnnounce (const tr_torrent *);

tr_tracker_stat * tr_announcerStats (const tr_torrent * torrent,
                                     int              * setmeTrackerCount);

void tr_announcerStatsFree (tr_tracker_stat * trackers,
                            int               trackerCount);

/***
****
***/

void tr_tracker_udp_upkeep (tr_session * session);

void tr_tracker_udp_close (tr_session * session);

bool tr_tracker_udp_is_idle (const tr_session * session);



#endif /* _TR_ANNOUNCER_H_ */
