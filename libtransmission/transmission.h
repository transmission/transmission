/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

#define TR_PEER_FROM__MAX       4
#define TR_PEER_FROM_INCOMING   0 /* connections made to the listening port */
#define TR_PEER_FROM_TRACKER    1 /* peers received from a tracker */
#define TR_PEER_FROM_CACHE      2 /* peers read from the peer cache */
#define TR_PEER_FROM_PEX        3 /* peers discovered via PEX */

/***********************************************************************
 * Error codes
 **********************************************************************/
/* General errors */
#define TR_OK                   0x00000000
#define TR_ERROR                0x81000000
#define TR_ERROR_ASSERT         0x82000000
/* I/O errors */
#define TR_ERROR_IO_MASK        0x0000000F
#define TR_ERROR_IO_PARENT      0x80000001
#define TR_ERROR_IO_PERMISSIONS 0x80000002
#define TR_ERROR_IO_OTHER       0x80000008
/* Misc */
#define TR_ERROR_TC_MASK        0x000000F0
#define TR_ERROR_TC_ERROR       0x80000010
#define TR_ERROR_TC_WARNING     0x80000020

/***********************************************************************
 * tr_init
 ***********************************************************************
 * Initializes a libtransmission instance. Returns a obscure handle to
 * be passed to all functions below. The tag argument is a short string
 * unique to the program invoking tr_init(), it is currently used as
 * part of saved torrent files' names to prevent one frontend from
 * deleting a torrent used by another. The following tags are used:
 *   beos cli daemon gtk macosx
 **********************************************************************/
typedef struct tr_handle_s tr_handle_t;
tr_handle_t * tr_init( const char * tag );

typedef struct tr_tracker_info_s tr_tracker_info_t;

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
void tr_natTraversalEnable( tr_handle_t *, int enable );

/***********************************************************************
 * tr_handleStatus
 ***********************************************************************
 * Returns some status info for the given handle.
 **********************************************************************/
typedef struct tr_handle_status_s tr_handle_status_t;
tr_handle_status_t * tr_handleStatus( tr_handle_t * );

/***********************************************************************
 * tr_setGlobalUploadLimit
 ***********************************************************************
 * Sets the total upload rate limit in KB/s
 **********************************************************************/
void tr_setGlobalUploadLimit( tr_handle_t *, int );

/***********************************************************************
 * tr_setGlobalDownloadLimit
 ***********************************************************************
 * Sets the total download rate limit in KB/s
 **********************************************************************/
void tr_setGlobalDownloadLimit( tr_handle_t *, int );

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

void tr_setUseCustomLimit( tr_torrent_t * tor, int limit );
void tr_setUploadLimit( tr_torrent_t * tor, int limit );
void tr_setDownloadLimit( tr_torrent_t * tor, int limit );

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
 * Opens and parses torrent file at 'path'. If the file exists and is
 * a valid torrent file, returns an handle and adds it to the list of
 * torrents (but doesn't start it). Returns NULL and sets *error
 * otherwise. If hash is not NULL and the torrent is already loaded
 * then it's 20-byte hash will be copied in. If the TR_FLAG_SAVE flag
 * is passed then a copy of the torrent file will be saved.
 **********************************************************************/
#define TR_EINVALID     1
#define TR_EUNSUPPORTED 2
#define TR_EDUPLICATE   3
#define TR_EOTHER       666
tr_torrent_t * tr_torrentInit( tr_handle_t *, const char * path,
                               uint8_t * hash, int flags, int * error );

/***********************************************************************
 * tr_torrentInitData
 ***********************************************************************
 * Like tr_torrentInit, except the actual torrent data is passed in
 * instead of the filename.
 **********************************************************************/
tr_torrent_t * tr_torrentInitData( tr_handle_t *, uint8_t * data,
                                   size_t size, uint8_t * hash,
                                   int flags, int * error );

/***********************************************************************
 * tr_torrentInitSaved
 ***********************************************************************
 * Opens and parses a torrent file as with tr_torrentInit, only taking
 * the hash string of a saved torrent file instead of a filename. There
 * are currently no valid flags for this function.
 **********************************************************************/
tr_torrent_t * tr_torrentInitSaved( tr_handle_t *, const char * hashStr,
                                    int flags, int * error );

/***********************************************************************
 * tr_torrentDisablePex
 ***********************************************************************
 * Disable or enable peer exchange for this torrent. Peer exchange is
 * enabled by default, except for private torrents where pex is
 * disabled and cannot be enabled.
 **********************************************************************/
void tr_torrentDisablePex( tr_torrent_t *, int disable );

/***********************************************************************
 * tr_torrentScrape
 ***********************************************************************
 * Return torrent metainfo.
 **********************************************************************/
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
int tr_torrentScrape( tr_torrent_t *, int * s, int * l, int * d );

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
 * tr_manualUpdate
 ***********************************************************************
 * Reannounce to tracker regardless of wait interval
 **********************************************************************/
void tr_manualUpdate( tr_torrent_t * );

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
 * tr_torrentCompletion
 ***********************************************************************
 * Returns the completion progress for each file in the torrent as an
 * array of floats the same size and order as in tr_info_t. Free the
 * array when done.
 **********************************************************************/
float * tr_torrentCompletion( tr_torrent_t * tor );

/***********************************************************************
 * tr_torrentRemoveSaved
 ***********************************************************************
 * Removes the saved copy of a torrent file for torrents which the
 * TR_FLAG_SAVE flag is set.
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
    char                 torrent[MAX_PATH_LENGTH];

    /* General info */
    uint8_t              hash[SHA_DIGEST_LENGTH];
    char                 hashString[2*SHA_DIGEST_LENGTH+1];
    char                 name[MAX_PATH_LENGTH];

    /* Flags */
#define TR_FLAG_SAVE    0x01 /* save a copy of the torrent file */
#define TR_FLAG_PRIVATE 0x02 /* do not share information for this torrent */
    int                  flags;

    /* Tracker info */
    struct
    {
        tr_tracker_info_t * list;
        int                 count;
    }                  * trackerList;
    int                  trackerTiers;

    /* Torrent info */
    char                 comment[MAX_PATH_LENGTH];
    char                 creator[MAX_PATH_LENGTH];
    int                  dateCreated;

    /* Pieces info */
    int                  pieceSize;
    int                  pieceCount;
    uint64_t             totalSize;
    uint8_t            * pieces;

    /* Files info */
    int                  multifile;
    int                  fileCount;
    tr_file_t          * files;
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

    int                 error;
    char                errorString[128];
    int                 cannotConnect;

    tr_tracker_info_t * tracker;

    float               progress;
    float               rateDownload;
    float               rateUpload;
    int                 eta;
    int                 peersTotal;
    int                 peersFrom[TR_PEER_FROM__MAX];
    int                 peersUploading;
    int                 peersDownloading;
    int                 seeders;
    int                 leechers;
    int                 completedFromTracker;

    uint64_t            downloaded;
    uint64_t            uploaded;
    float               swarmspeed;

#define TR_RATIO_NA  -1
#define TR_RATIO_INF -2
    float               ratio;
};

struct tr_peer_stat_s
{
    char    addr[INET_ADDRSTRLEN];
    const char * client;
    
    int     isConnected;
    int     from;
    float   progress;
    int     port;
    
    int     isDownloading;
    int     isUploading;
    float   downloadFromRate;
    float   uploadToRate;
};

struct tr_msg_list_s
{
    int                    level;
    time_t                 when;
    char                 * message;
    struct tr_msg_list_s * next;
};

struct tr_tracker_info_s
{
    char * address;
    int    port;
    char * announce;
    char * scrape;
};

struct tr_handle_status_s
{
#define TR_NAT_TRAVERSAL_MAPPING        1
#define TR_NAT_TRAVERSAL_MAPPED         2
#define TR_NAT_TRAVERSAL_NOTFOUND       3
#define TR_NAT_TRAVERSAL_ERROR          4
#define TR_NAT_TRAVERSAL_UNMAPPING      5
#define TR_NAT_TRAVERSAL_DISABLED       6
#define TR_NAT_TRAVERSAL_IS_DISABLED( st ) \
  ( TR_NAT_TRAVERSAL_DISABLED == (st) || TR_NAT_TRAVERSAL_UNMAPPING == (st) )
    int natTraversalStatus;
    int publicPort;
};

#ifdef __TRANSMISSION__
#  include "internal.h"
#endif

#ifdef __cplusplus
}
#endif

#endif
