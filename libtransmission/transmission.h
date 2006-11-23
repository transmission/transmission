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

#ifndef TR_TRANSMISSION_H
#define TR_TRANSMISSION_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "version.h"

#include <inttypes.h>
#ifndef PRIu64
# define PRIu64 "lld"
#endif
#include <time.h>

#define SHA_DIGEST_LENGTH 20
#ifdef __BEOS__
# include <StorageDefs.h>
# define MAX_PATH_LENGTH  B_FILE_NAME_LENGTH
#else
# define MAX_PATH_LENGTH  1024
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

#define TR_DEFAULT_PORT   9090
#define TR_NOERROR        0

/***********************************************************************
 * tr_init
 ***********************************************************************
 * Initializes a libtransmission instance. Returns a obscure handle to
 * be passed to all functions below.
 **********************************************************************/
typedef struct tr_handle_s tr_handle_t;
tr_handle_t * tr_init();

/***********************************************************************
 * tr_setMessageLevel
 ***********************************************************************
 * Set the level of messages to be output or queued
 **********************************************************************/
#define TR_MSG_ERR 1
#define TR_MSG_INF 2
#define TR_MSG_DBG 3
void tr_setMessageLevel( int );
int tr_getMessageLevel( void );

/***********************************************************************
 * tr_setMessageQueuing
 ***********************************************************************
 * Enable or disable message queuing
 **********************************************************************/
typedef struct tr_msg_list_s tr_msg_list_t;
void tr_setMessageQueuing( int );

/***********************************************************************
 * tr_getQueuedMessages
 ***********************************************************************
 * Return a list of queued messages
 **********************************************************************/
tr_msg_list_t * tr_getQueuedMessages( void );
void tr_freeMessageList( tr_msg_list_t * list );

/***********************************************************************
 * tr_getPrefsDirectory
 ***********************************************************************
 * Returns the full path to a directory which can be used to store
 * preferences. The string belongs to libtransmission, do not free it.
 **********************************************************************/
char * tr_getPrefsDirectory();

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 * Sets the port to listen for incoming peer connections.
 * This can be safely called even with active torrents.
 **********************************************************************/
void tr_setBindPort( tr_handle_t *, int );

/***********************************************************************
 * tr_natTraversalEnable
 * tr_natTraversalDisable
 ***********************************************************************
 * Enable or disable NAT traversal using NAT-PMP or UPnP IGD.
 **********************************************************************/
void tr_natTraversalEnable( tr_handle_t * );
void tr_natTraversalDisable( tr_handle_t * );

/***********************************************************************
 * tr_natTraversalStatus
 ***********************************************************************
 * Return the status of NAT traversal
 **********************************************************************/
#define TR_NAT_TRAVERSAL_MAPPING        1
#define TR_NAT_TRAVERSAL_MAPPED         2
#define TR_NAT_TRAVERSAL_NOTFOUND       3
#define TR_NAT_TRAVERSAL_ERROR          4
#define TR_NAT_TRAVERSAL_UNMAPPING      5
#define TR_NAT_TRAVERSAL_DISABLED       6
#define TR_NAT_TRAVERSAL_IS_DISABLED( st ) \
  ( TR_NAT_TRAVERSAL_DISABLED == (st) || TR_NAT_TRAVERSAL_UNMAPPING == (st) )
int tr_natTraversalStatus( tr_handle_t * );

/***********************************************************************
 * tr_setUploadLimit
 ***********************************************************************
 * Sets the total upload rate limit in KB/s
 **********************************************************************/
void tr_setUploadLimit( tr_handle_t *, int );

/***********************************************************************
 * tr_setDownloadLimit
 ***********************************************************************
 * Sets the total download rate limit in KB/s
 **********************************************************************/
void tr_setDownloadLimit( tr_handle_t *, int );

/***********************************************************************
 * tr_torrentCount
 ***********************************************************************
 * Returns the count of open torrents
 **********************************************************************/
int tr_torrentCount( tr_handle_t * h );

/***********************************************************************
 * tr_torrentIterate
 ***********************************************************************
 * Iterates on open torrents
 **********************************************************************/
typedef struct tr_torrent_s tr_torrent_t;
typedef void (*tr_callback_t) ( tr_torrent_t *, void * );
void tr_torrentIterate( tr_handle_t *, tr_callback_t, void * );

/***********************************************************************
 * tr_torrentRates
 ***********************************************************************
 * Gets the total download and upload rates
 **********************************************************************/
void tr_torrentRates( tr_handle_t *, float *, float * );

/***********************************************************************
 * tr_close
 ***********************************************************************
 * Frees memory allocated by tr_init.
 **********************************************************************/
void tr_close( tr_handle_t * );

/***********************************************************************
 * tr_torrentInit
 ***********************************************************************
 * Opens and parses torrent file at 'path'. If the file exists and is a
 * valid torrent file, returns an handle and adds it to the list of
 * torrents (but doesn't start it). Returns NULL and sets *error
 * otherwise.  If the TR_FSAVEPRIVATE flag is passed then a private copy
 * of the torrent file will be saved.
 **********************************************************************/
#define TR_EINVALID     1
#define TR_EUNSUPPORTED 2
#define TR_EDUPLICATE   3
#define TR_EOTHER       666
tr_torrent_t * tr_torrentInit( tr_handle_t *, const char * path,
                               int flags, int * error );

/***********************************************************************
 * tr_torrentInitSaved
 ***********************************************************************
 * Opens and parses a torrent file as with tr_torrentInit, only taking
 * the hash string of a saved torrent file instead of a filename.  There
 * are currently no valid flags for this function.
 **********************************************************************/
tr_torrent_t * tr_torrentInitSaved( tr_handle_t *, const char * hashStr,
                                    int flags, int * error );

typedef struct tr_info_s tr_info_t;
tr_info_t * tr_torrentInfo( tr_torrent_t * );

/***********************************************************************
 * tr_torrentScrape
 ***********************************************************************
 * Asks the tracker for the count of seeders and leechers. Returns 0
 * and fills 's' and 'l' if successful. Otherwise returns 1 if the
 * tracker doesn't support the scrape protocol, is unreachable or
 * replied with some error. tr_torrentScrape may block up to 20 seconds
 * before returning.
 **********************************************************************/
int tr_torrentScrape( tr_torrent_t *, int * s, int * l );

/***********************************************************************
 * tr_torrentStart
 ***********************************************************************
 * Starts downloading. The download is launched in a seperate thread,
 * therefore tr_torrentStart returns immediately.
 **********************************************************************/
void   tr_torrentSetFolder( tr_torrent_t *, const char * );
char * tr_torrentGetFolder( tr_torrent_t * );
void   tr_torrentStart( tr_torrent_t * );

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
void tr_torrentStop( tr_torrent_t * );

/***********************************************************************
 * tr_getFinished
 ***********************************************************************
 * The first call after a torrent is completed returns 1. Returns 0
 * in other cases.
 **********************************************************************/
int tr_getFinished( tr_torrent_t * );

/***********************************************************************
 * tr_torrentStat
 ***********************************************************************
 * Returns a pointer to an tr_stat_t structure with updated information
 * on the torrent. The structure belongs to libtransmission (do not
 * free it) and is guaranteed to be unchanged until the next call to
 * tr_torrentStat.
 * The interface should call this function every second or so in order
 * to update itself.
 **********************************************************************/
typedef struct tr_stat_s tr_stat_t;
tr_stat_t * tr_torrentStat( tr_torrent_t * );

/***********************************************************************
 * tr_torrentPeers
 ***********************************************************************/
typedef struct tr_peer_stat_s tr_peer_stat_t;
tr_peer_stat_t * tr_torrentPeers( tr_torrent_t *, int * peerCount );
void tr_torrentPeersFree( tr_peer_stat_t *, int peerCount );

/***********************************************************************
 * tr_torrentAvailability
 ***********************************************************************
 * Use this to draw an advanced progress bar which is 'size' pixels
 * wide. Fills 'tab' which you must have allocated: each byte is set
 * to either -1 if we have the piece, otherwise it is set to the number
 * of connected peers who have the piece.
 **********************************************************************/
void tr_torrentAvailability( tr_torrent_t *, int8_t * tab, int size );

void tr_torrentAmountFinished( tr_torrent_t * tor, float * tab, int size );

/***********************************************************************
 * tr_torrentRemoveSaved
 ***********************************************************************
 * Removes the private saved copy of a torrent file for torrents which
 * the TR_FSAVEPRIVATE flag is set.
 **********************************************************************/
void tr_torrentRemoveSaved( tr_torrent_t * );

/***********************************************************************
 * tr_torrentClose
 ***********************************************************************
 * Frees memory allocated by tr_torrentInit. If the torrent was running,
 * you must call tr_torrentStop() before closing it.
 **********************************************************************/
void tr_torrentClose( tr_handle_t *, tr_torrent_t * );

/***********************************************************************
 * tr_info_s
 **********************************************************************/
typedef struct tr_file_s
{
    uint64_t length;                /* Length of the file, in bytes */
    char     name[MAX_PATH_LENGTH]; /* Path to the file */
}
tr_file_t;
struct tr_info_s
{
    /* Path to torrent */
    char        torrent[MAX_PATH_LENGTH];

    /* General info */
    uint8_t     hash[SHA_DIGEST_LENGTH];
    char        hashString[2*SHA_DIGEST_LENGTH+1];
    char        name[MAX_PATH_LENGTH];

    /* Flags */
#define TR_FSAVEPRIVATE 0x01    /* save a private copy of the torrent */
    int         flags;

    /* Tracker info */
    char        trackerAddress[256];
    int         trackerPort;
    char        trackerAnnounce[MAX_PATH_LENGTH];
    
    /* Torrent info */
    char        comment[MAX_PATH_LENGTH];
    char        creator[MAX_PATH_LENGTH];
    int         dateCreated;

    /* Pieces info */
    int         pieceSize;
    int         pieceCount;
    uint64_t    totalSize;
    uint8_t   * pieces;

    /* Files info */
    int         multifile;
    int         fileCount;
    tr_file_t * files;
};

/***********************************************************************
 * tr_stat_s
 **********************************************************************/
struct tr_stat_s
{
#define TR_STATUS_CHECK    0x001 /* Checking files */
#define TR_STATUS_DOWNLOAD 0x002 /* Downloading */
#define TR_STATUS_SEED     0x004 /* Seeding */
#define TR_STATUS_STOPPING 0x008 /* Sending 'stopped' to the tracker */
#define TR_STATUS_STOPPED  0x010 /* Sent 'stopped' but thread still
                                    running (for internal use only) */
#define TR_STATUS_PAUSE    0x020 /* Paused */

#define TR_STATUS_ACTIVE   (TR_STATUS_CHECK|TR_STATUS_DOWNLOAD|TR_STATUS_SEED)
#define TR_STATUS_INACTIVE (TR_STATUS_STOPPING|TR_STATUS_STOPPED|TR_STATUS_PAUSE)
    int                 status;

#define TR_ETRACKER 1
#define TR_EINOUT   2
    int                 error;
    char                trackerError[128];

    float               progress;
    float               rateDownload;
    float               rateUpload;
    int                 eta;
    int                 peersTotal;
    int                 peersIncoming;
    int                 peersUploading;
    int                 peersDownloading;
    int                 seeders;
    int                 leechers;

    uint64_t            downloaded;
    uint64_t            uploaded;
    float               swarmspeed;
};

struct tr_peer_stat_s
{
    char    addr[INET_ADDRSTRLEN];
    char *  client;
    
    int     isConnected;
    int     isIncoming;
    int     isDownloading;
    int     isUploading;
    float   progress;
};

struct tr_msg_list_s
{
    int                    level;
    time_t                 when;
    char                 * message;
    struct tr_msg_list_s * next;
};

#ifdef __TRANSMISSION__
#  include "internal.h"
#endif

#ifdef __cplusplus
}
#endif

#endif
