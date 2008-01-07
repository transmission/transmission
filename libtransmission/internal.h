/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#ifndef TR_INTERNAL_H
#define TR_INTERNAL_H 1

#define TR_NAME "Transmission"

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

typedef enum { TR_NET_OK, TR_NET_ERROR, TR_NET_WAIT } tr_tristate_t;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

int tr_trackerInfoInit( struct tr_tracker_info  * info,
                        const char              * address,
                        int                       address_len );

void tr_trackerInfoClear( struct tr_tracker_info * info );

uint8_t* tr_peerIdNew( void );

const uint8_t* tr_getPeerId( void );

struct tr_handle
{
    tr_encryption_mode         encryptionMode;

    struct tr_event_handle   * events;

    int                        torrentCount;
    tr_torrent               * torrentList;

    char                     * tag;
    unsigned int               isPortSet : 1;
    unsigned int               isPexEnabled : 1;

    char                       useUploadLimit;
    char                       useDownloadLimit;
    struct tr_ratecontrol    * upload;
    struct tr_ratecontrol    * download;

    struct tr_peerMgr        * peerMgr;
    struct tr_shared         * shared;

    struct tr_lock           * lock;

    tr_handle_status           stats[2];
    int                        statCur;

    uint8_t                    isClosed;

    struct tr_stats_handle   * sessionStats;
    struct tr_tracker_handle * tracker;
};

void tr_globalLock       ( struct tr_handle * );
void tr_globalUnlock     ( struct tr_handle * );
int  tr_globalIsLocked   ( const struct tr_handle * );

#endif
