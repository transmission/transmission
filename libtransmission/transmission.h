/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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

#ifndef TR_TRANSMISSION_H
#define TR_TRANSMISSION_H 1

#include <inttypes.h>

#define SHA_DIGEST_LENGTH    20
#define MAX_PATH_LENGTH      1024
#define TR_MAX_TORRENT_COUNT 50

/***********************************************************************
 * tr_init
 ***********************************************************************
 * Initializes a libtransmission instance. Returns a obscure handle to
 * be passed to all functions below.
 **********************************************************************/
typedef struct tr_handle_s tr_handle_t;

tr_handle_t * tr_init();

/***********************************************************************
 * tr_getPrefsDirectory
 ***********************************************************************
 * Returns the full path to the directory used by libtransmission to
 * store the resume files. The string belongs to libtransmission, do
 * not free it.
 **********************************************************************/
char * tr_getPrefsDirectory( tr_handle_t * );

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 * Sets a "start" port: everytime we start a torrent, we try to bind
 * this port, then the next one and so on until we are successful.
 **********************************************************************/
void tr_setBindPort( tr_handle_t *, int );

/***********************************************************************
 * tr_setUploadLimit
 ***********************************************************************
 * Sets the total upload rate limit in KB/s
 **********************************************************************/
void tr_setUploadLimit( tr_handle_t *, int );

/***********************************************************************
 * tr_torrentRates
 ***********************************************************************
 * Gets the total download and upload rates
 **********************************************************************/
void tr_torrentRates( tr_handle_t *, float *, float * );

/***********************************************************************
 * tr_getFinished
 ***********************************************************************
 * Tests to see if torrent is finished
 **********************************************************************/
int tr_getFinished( tr_handle_t *, int );

/***********************************************************************
 * tr_setFinished
 ***********************************************************************
 * Sets the boolean value finished in the torrent back to false
 **********************************************************************/
void tr_setFinished( tr_handle_t *, int, int );

/***********************************************************************
 * tr_torrentInit
 ***********************************************************************
 * Opens and parses torrent file at 'path'. If the file exists and is a
 * valid torrent file, returns 0 and adds it to the list of torrents
 * (but doesn't start it). Returns a non-zero value otherwise.
 **********************************************************************/
int tr_torrentInit( tr_handle_t *, const char * path );

/***********************************************************************
 * tr_torrentScrape
 ***********************************************************************
 * Asks the tracker for the count of seeders and leechers. Returns 0
 * and fills 's' and 'l' if successful. Otherwise returns 1 if the
 * tracker doesn't support the scrape protocol, is unreachable or
 * replied with some error. tr_torrentScrape may block up to 20 seconds
 * before returning.
 **********************************************************************/
int tr_torrentScrape( tr_handle_t *, int, int * s, int * l );

/***********************************************************************
 * tr_torrentStart
 ***********************************************************************
 * Starts downloading. The download is launched in a seperate thread,
 * therefore tr_torrentStart returns immediately.
 **********************************************************************/
void   tr_torrentSetFolder( tr_handle_t *, int, const char * );
char * tr_torrentGetFolder( tr_handle_t *, int );
void   tr_torrentStart( tr_handle_t *, int );

/***********************************************************************
 * tr_torrentStop
 ***********************************************************************
 * Stops downloading and notices the tracker that we are leaving. The
 * thread keeps running while doing so.
 * The thread will eventually be joined, either:
 * - by tr_torrentStat when the tracker has been successfully noticed,
 * - by tr_torrentStat if the tracker could not be noticed within 60s,
 * - by tr_torrentClose if you choose to remove the torrent without
 *   waiting any further.
 **********************************************************************/
void tr_torrentStop( tr_handle_t *, int );

/***********************************************************************
 * tr_torrentStat
 ***********************************************************************
 * Allocates an array of tr_stat_t structures, containing information
 * about the current status of all open torrents (see the contents
 * of tr_stat_s below). Returns the count of open torrents and sets
 * (*s) to the address of the array, or NULL if no torrent is open.
 * In the former case, the array belongs to the caller who is
 * responsible of freeing it.
 * The interface should call this function every 0.5 second or so in
 * order to update itself.
 **********************************************************************/
typedef struct tr_stat_s tr_stat_t;

int tr_torrentCount( tr_handle_t * h );
int tr_torrentStat( tr_handle_t *, tr_stat_t ** s );

/***********************************************************************
 * tr_torrentClose
 ***********************************************************************
 * Frees memory allocated by tr_torrentInit. If the torrent was running,
 * you must call tr_torrentStop() before closing it.
 **********************************************************************/
void tr_torrentClose( tr_handle_t *, int );

/***********************************************************************
 * tr_close
 ***********************************************************************
 * Frees memory allocated by tr_init.
 **********************************************************************/
void tr_close( tr_handle_t * );


/***********************************************************************
 * tr_stat_s
 **********************************************************************/
typedef struct tr_file_s
{
    uint64_t length;                /* Length of the file, in bytes */
    char     name[MAX_PATH_LENGTH]; /* Path to the file */
}
tr_file_t;

typedef struct tr_info_s
{
    /* Path to torrent */
    char        torrent[MAX_PATH_LENGTH];

    /* General info */
    uint8_t     hash[SHA_DIGEST_LENGTH];
    char        name[MAX_PATH_LENGTH];

    /* Tracker info */
    char        trackerAddress[256];
    int         trackerPort;
    char        trackerAnnounce[MAX_PATH_LENGTH];

    /* Pieces info */
    int         pieceSize;
    int         pieceCount;
    uint64_t    totalSize;
    uint8_t   * pieces;

    /* Files info */
    int         fileCount;
    tr_file_t * files;
}
tr_info_t;

struct tr_stat_s
{
    tr_info_t   info;

#define TR_STATUS_CHECK    0x001 /* Checking files */
#define TR_STATUS_DOWNLOAD 0x002 /* Downloading */
#define TR_STATUS_SEED     0x004 /* Seeding */
#define TR_STATUS_STOPPING 0x008 /* Sending 'stopped' to the tracker */
#define TR_STATUS_STOPPED  0x010 /* Sent 'stopped' but thread still
                                    running (for internal use only) */
#define TR_STATUS_PAUSE    0x020 /* Paused */
#define TR_TRACKER_ERROR   0x100
    int         status;
    char        error[128];

    float       progress;
    float       rateDownload;
    float       rateUpload;
    int         eta;
    int         peersTotal;
    int         peersUploading;
    int         peersDownloading;
    char        pieces[120];
    int			seeders;
	int			leechers;

    uint64_t    downloaded;
    uint64_t    uploaded;

    char      * folder;
};

#ifdef __TRANSMISSION__
#  include "internal.h"
#endif

#endif
