/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef _TR_ANNOUNCER_H_
#define _TR_ANNOUNCER_H_

#include "transmission.h"
#include "publish.h"

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

/** @brief Notification object to tell listeners about announce or scrape occurences */
typedef struct
{
    /* what type of event this is */
    TrackerEventType    messageType;

    /* for TR_TRACKER_WARNING and TR_TRACKER_ERROR */
    const char *  text;

    /* for TR_TRACKER_PEERS */
    const uint8_t *  compact;
    int              compactLen;
    int              allAreSeeds;
}
tr_tracker_event;

/**
***  Session ctor/dtor
**/

void tr_announcerInit( tr_session * );

void tr_announcerClose( tr_session * );

/**
***  For torrent customers
**/

struct tr_torrent_tiers * tr_announcerAddTorrent( struct tr_announcer  *,
                                                  tr_torrent           * );

tr_bool tr_announcerHasBacklog( const struct tr_announcer * );

void tr_announcerResetTorrent( struct tr_announcer*, tr_torrent* );

tr_publisher_tag tr_announcerSubscribe( struct tr_torrent_tiers  * tiers,
                                        tr_delivery_func           func,
                                        void                     * userData );

void tr_announcerUnsubscribe( struct tr_torrent_tiers  * tiers,
                              tr_publisher_tag           tag );

void tr_announcerRemoveTorrent( struct tr_announcer * ,
                                tr_torrent          * );

void tr_announcerChangeMyPort( tr_torrent * );

tr_bool tr_announcerCanManualAnnounce( const tr_torrent * );

void tr_announcerManualAnnounce( tr_torrent * );

void tr_announcerTorrentStarted( tr_torrent * );
void tr_announcerTorrentStopped( tr_torrent * );
void tr_announcerTorrentCompleted( tr_torrent * );

time_t tr_announcerNextManualAnnounce( const tr_torrent * );

tr_tracker_stat * tr_announcerStats( const tr_torrent * torrent,
                                     int              * setmeTrackerCount );

void tr_announcerStatsFree( tr_tracker_stat * trackers,
                            int               trackerCount );


#endif /* _TR_ANNOUNCER_H_ */
