/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef _TR_TRACKER_H_
#define _TR_TRACKER_H_

#include <inttypes.h> /* for uint8_t */
#include "transmission.h"
#include "publish.h"

/**
***  Locating a tracker
**/

typedef struct tr_tracker tr_tracker;

tr_tracker * tr_trackerNew( const tr_torrent * );

void  tr_trackerFree ( tr_tracker * );

/**
***  Tracker Publish / Subscribe
**/

typedef enum
{
    TR_TRACKER_WARNING,
    TR_TRACKER_ERROR,
    TR_TRACKER_ERROR_CLEAR,
    TR_TRACKER_PEERS
}
TrackerEventType;

typedef struct
{
    /* what type of event this is */
    TrackerEventType messageType;

    /* the torrent's 20-character sha1 hash */
    const uint8_t * hash;

    /* for TR_TRACKER_WARNING and TR_TRACKER_ERROR */
    const char * text;

    /* for TR_TRACKER_PEERS */
    const uint8_t * peerCompact;
    int peerCount;
}
tr_tracker_event;

tr_publisher_tag  tr_trackerSubscribe       ( struct tr_tracker * tag,
                                              tr_delivery_func      func,
                                              void                * user );

void              tr_trackerUnsubscribe     ( struct tr_tracker * tracker,
                                              tr_publisher_tag      tag );

/***
****
***/

void tr_trackerStart                        ( struct tr_tracker * );

void tr_trackerCompleted                    ( struct tr_tracker * );

void tr_trackerStop                         ( struct tr_tracker * );

void tr_trackerReannounce                   ( struct tr_tracker * );

void tr_trackerChangeMyPort                 ( struct tr_tracker * );

const tr_tracker_info * tr_trackerGetAddress( const struct tr_tracker * );

int  tr_trackerCanManualAnnounce            ( const struct tr_tracker * );

void tr_trackerGetCounts                    ( const struct tr_tracker *,
                                              int * setme_completedCount,
                                              int * setme_leecherCount,
                                              int * setme_seederCount );

#endif
