/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef TR_TRACKER_H
#define TR_TRACKER_H 1

typedef struct tr_tracker_s tr_tracker_t;

tr_tracker_t * tr_trackerInit      ( tr_torrent_t * );

void           tr_trackerPulse     ( tr_tracker_t *,
                                     int          * peerCount,
                                     uint8_t     ** peerCompact );

void           tr_trackerCompleted( tr_tracker_t * );
void           tr_trackerStopped  ( tr_tracker_t * );
void           tr_trackerClose    ( tr_tracker_t * );

/***********************************************************************
 * tr_trackerSeeders
 ***********************************************************************
 * Looks for the seeders as returned by the tracker.
 **********************************************************************/
int tr_trackerSeeders  ( const tr_tracker_t * );

/***********************************************************************
 * tr_trackerLeechers
 ***********************************************************************
 * Looks for the leechers as returned by the tracker.
 **********************************************************************/
int tr_trackerLeechers ( const tr_tracker_t * );

/***********************************************************************
 * tr_trackerDownloaded
 ***********************************************************************
 * Looks for number of completed downloads as returned by the tracker
 * (from scrape).
 **********************************************************************/
int tr_trackerDownloaded( const tr_tracker_t * tc );

/***********************************************************************
 * tr_trackerGet
 ***********************************************************************
 * Return the tracker currently in use.
 **********************************************************************/
const tr_tracker_info_t * tr_trackerGet( const tr_tracker_t * tc );

int tr_trackerCannotConnect( const tr_tracker_t * tc );

/* the time of the tracker's last message to us */
uint64_t tr_trackerLastResponseDate ( const tr_tracker_t * );


#if 0
/***********************************************************************
 * tr_trackerScrape
 ***********************************************************************
 * Attempt a blocking scrape and return the seeders, leechers, and
 * completed downloads if successful.
 **********************************************************************/
int tr_trackerScrape( tr_torrent_t * tor, int * s, int * l, int * d );
#endif

#endif
