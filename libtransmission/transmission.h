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
#ifndef PRId64
# define PRId64 "lld"
#endif
#ifndef PRIu64
# define PRIu64 "llu"
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

#if defined(WIN32)
#define TR_PATH_DELIMITER '\\'
#define TR_PATH_DELIMITER_STR "\\"
#else
#define TR_PATH_DELIMITER '/'
#define TR_PATH_DELIMITER_STR "/"
#endif

#define TR_DEFAULT_PORT   9090

enum
{
    TR_PEER_FROM_INCOMING  = 0,  /* connections made to the listening port */
    TR_PEER_FROM_TRACKER   = 1,  /* peers received from a tracker */
    TR_PEER_FROM_CACHE     = 2,  /* peers read from the peer cache */
    TR_PEER_FROM_PEX       = 3,  /* peers discovered via PEX */
    TR_PEER_FROM__MAX
};

/***********************************************************************
 * Error codes
 **********************************************************************/
/* General errors */
#define TR_OK                       0x00000000
#define TR_ERROR                    0x81000000
#define TR_ERROR_ASSERT             0x82000000
/* I/O errors */
#define TR_ERROR_IO_MASK            0x000000FF
#define TR_ERROR_IO_PERMISSIONS     0x80000002
#define TR_ERROR_IO_SPACE           0x80000004
#define TR_ERROR_IO_FILE_TOO_BIG    0x80000008
#define TR_ERROR_IO_OPEN_FILES      0x80000010
#define TR_ERROR_IO_DUP_DOWNLOAD    0x80000020
#define TR_ERROR_IO_OTHER           0x80000040
/* Misc */
#define TR_ERROR_TC_MASK            0x00000F00
#define TR_ERROR_TC_ERROR           0x80000100
#define TR_ERROR_TC_WARNING         0x80000200

#define TR_ERROR_ISSET( num, code ) ( (code) == ( (code) & (num) ) )

/***********************************************************************
 * tr_init
 ***********************************************************************
 * Initializes a libtransmission instance and returns an opaque handle
 * to be passed to functions below. The tag argument is a short string
 * unique to the program invoking tr_init(), it is currently used as
 * part of saved torrent files' names to prevent one frontend from
 * deleting a torrent used by another. The following tags are used:
 *   beos cli daemon gtk macosx
 **********************************************************************/

typedef struct tr_handle tr_handle;

tr_handle * tr_init( const char * tag );

/* shut down a libtransmission instance created by tr_init(). */
void tr_close( tr_handle * );


/**
***
**/

typedef struct tr_global_stats
{
    uint64_t downloadedGigs;  /* total down / GiB */
    uint64_t downloadedBytes; /* total down % GiB */
    uint64_t uploadedGigs;    /* total up / GiB */
    uint64_t uploadedBytes;   /* total up % GiB */
    double ratio;             /* total up / total down */
    uint64_t filesAdded;      /* number of files added */
    uint64_t sessionCount;    /* program started N times */
}
tr_global_stats;

void tr_getGlobalStats( const tr_handle * handle, tr_global_stats * setme );


/**
***
**/

typedef enum
{
    TR_PLAINTEXT_PREFERRED,
    TR_ENCRYPTION_PREFERRED,
    TR_ENCRYPTION_REQUIRED
}
tr_encryption_mode;

tr_encryption_mode tr_getEncryptionMode( tr_handle * handle );

void tr_setEncryptionMode( tr_handle * handle, tr_encryption_mode mode );

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
typedef struct tr_msg_list tr_msg_list;
void tr_setMessageQueuing( int );

/***********************************************************************
 * tr_getQueuedMessages
 ***********************************************************************
 * Return a list of queued messages
 **********************************************************************/
tr_msg_list * tr_getQueuedMessages( void );
void tr_freeMessageList( tr_msg_list * list );

/***********************************************************************
 * tr_getPrefsDirectory
 ***********************************************************************
 * Returns the full path to a directory which can be used to store
 * preferences. The string belongs to libtransmission, do not free it.
 **********************************************************************/
const char * tr_getPrefsDirectory( void );

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 * Sets the port to listen for incoming peer connections.
 * This can be safely called even with active torrents.
 **********************************************************************/
void tr_setBindPort( tr_handle *, int );

int tr_getPublicPort( const tr_handle * );

/***********************************************************************
 * tr_natTraversalEnable
 * tr_natTraversalDisable
 ***********************************************************************
 * Enable or disable NAT traversal using NAT-PMP or UPnP IGD.
 **********************************************************************/
void tr_natTraversalEnable( tr_handle *, int enable );

/***********************************************************************
 * tr_handleStatus
 ***********************************************************************
 * Returns some status info for the given handle.
 **********************************************************************/
typedef struct tr_handle_status tr_handle_status;
tr_handle_status * tr_handleStatus( tr_handle * );


/***********************************************************************
 * tr_torrentCount
 ***********************************************************************
 * Returns the count of open torrents
 **********************************************************************/
int tr_torrentCount( tr_handle * h );


/***********************************************************************
 * tr_torrentIterate
 ***********************************************************************
 * Iterates on open torrents
 **********************************************************************/
typedef struct tr_torrent tr_torrent;
typedef void (*tr_callback_t) ( tr_torrent *, void * );
void tr_torrentIterate( tr_handle *, tr_callback_t, void * );


/***********************************************************************
*** Speed Limits
**/

enum { TR_UP, TR_DOWN };

typedef enum
{
    TR_SPEEDLIMIT_GLOBAL,    /* only follow the overall speed limit */
    TR_SPEEDLIMIT_SINGLE,    /* only follow the per-torrent limit */
    TR_SPEEDLIMIT_UNLIMITED  /* no limits at all */
}
tr_speedlimit;

void tr_torrentSetSpeedMode( tr_torrent   * tor,
                             int            up_or_down,
                             tr_speedlimit  mode );

tr_speedlimit tr_torrentGetSpeedMode( const tr_torrent  * tor,
                                      int                 up_or_down);

void tr_torrentSetSpeedLimit( tr_torrent   * tor,
                              int            up_or_down,
                              int            single_KiB_sec );

int tr_torrentGetSpeedLimit( const tr_torrent  * tor,
                             int                 up_or_down );

void tr_setUseGlobalSpeedLimit( tr_handle * handle,
                                int           up_or_down,
                                int           use_flag );

void tr_setGlobalSpeedLimit( tr_handle * handle,
                             int           up_or_down,
                             int           global_KiB_sec );

void tr_getGlobalSpeedLimit( tr_handle * handle,
                             int           up_or_down,
                             int         * setme_is_enabled,
                             int         * setme_KiBsec );

/***********************************************************************
 * Torrent Priorities
 **********************************************************************/

enum
{
    TR_PRI_LOW    = -1,
    TR_PRI_NORMAL =  0, /* since NORMAL is 0, memset initializes nicely */
    TR_PRI_HIGH   =  1
};

typedef int8_t tr_priority_t;

/* set a batch of files to a particular priority.
 * priority must be one of TR_PRI_NORMAL, _HIGH, or _LOW */
void tr_torrentSetFilePriorities( tr_torrent        * tor,
                                  int               * files,
                                  int                 fileCount,
                                  tr_priority_t       priority );

/* returns a malloc()ed array of tor->info.fileCount items,
 * each holding a value of TR_PRI_NORMAL, _HIGH, or _LOW.
   free the array when done. */
tr_priority_t* tr_torrentGetFilePriorities( const tr_torrent * );

/* single-file form of tr_torrentGetFilePriorities.
 * returns one of TR_PRI_NORMAL, _HIGH, or _LOW. */
tr_priority_t tr_torrentGetFilePriority( const tr_torrent *, int file );

/* returns true if the file's `download' flag is set */
int tr_torrentGetFileDL( const tr_torrent *, int file );

/* set a batch of files to be downloaded or not. */
void tr_torrentSetFileDLs ( tr_torrent   * tor,
                            int          * files,
                            int            fileCount,
                            int            do_download );

/***********************************************************************
 * tr_torrentRates
 ***********************************************************************
 * Gets the total download and upload rates
 **********************************************************************/
void tr_torrentRates( tr_handle *, float *, float * );



/**
 *  Load all the torrents in tr_getTorrentsDirectory().
 *  This can be used at startup to kickstart all the torrents
 *  from the previous session.
 */
tr_torrent ** tr_loadTorrents ( tr_handle  * h,
                                const char   * fallback_destination,
                                int            isPaused,
                                int          * setmeCount );


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
tr_torrent * tr_torrentInit( tr_handle    * handle,
                             const char   * metainfo_filename,
                             const char   * destination,
                             int            isPaused,
                             int          * setme_error );

/* This is a stupid hack to fix #415.  Probably I should fold all
 * these torrent constructors into a single function that takes
 * a function object to hold all these esoteric arguments. */
tr_torrent * tr_torrentLoad( tr_handle    * handle,
                             const char   * metainfo_filename,
                             const char   * fallback_destination,
                             int            isPaused,
                             int          * setme_error );

typedef struct tr_info tr_info;

/**
 * Parses the specified metainfo file.
 *
 * Returns TR_OK if it parsed and can be added to Transmission.
 * Returns TR_INVALID if it couldn't be parsed.
 * Returns TR_EDUPLICATE if it parsed but can't be added.
 *
 * "destination" can be NULL if you don't need to know whether
 * or not the torrent can be added.
 *
 " "setme_info" can be NULL if you don't need the information.
 * If the metainfo can be parsed and setme_info is non-NULL,
 * it will be filled with the metadata's info.  You'll need to
 * call tr_metainfoFree( setme_info ) when done with it.
 */
int tr_torrentParse( const tr_handle  * handle,
                     const char       * metainfo_filename,
                     const char       * destination,
                     tr_info          * setme_info );

/**
 * Parses the cached metainfo file that matches the given hash string.
 * See tr_torrentParse() for a description of the arguments
 */
int
tr_torrentParseHash( const tr_handle  * h,
                     const char       * hashStr,
                     const char       * destination,
                     tr_info          * setme_info );


/***********************************************************************
 * tr_torrentInitData
 ***********************************************************************
 * Like tr_torrentInit, except the actual torrent data is passed in
 * instead of the filename.
 **********************************************************************/
tr_torrent * tr_torrentInitData( tr_handle *,
                                 const uint8_t * data, size_t size,
                                 const char * destination,
                                 int isPaused,
                                 int * error );

/***********************************************************************
 * tr_torrentInitSaved
 ***********************************************************************
 * Opens and parses a torrent file as with tr_torrentInit, only taking
 * the hash string of a saved torrent file instead of a filename. There
 * are currently no valid flags for this function.
 **********************************************************************/
tr_torrent * tr_torrentInitSaved( tr_handle *,
                                  const char * hashStr,
                                  const char * destination,
                                  int isPaused,
                                  int * error );

/***********************************************************************
 * tr_torrentDisablePex
 ***********************************************************************
 * Disable or enable peer exchange for this torrent. Peer exchange is
 * enabled by default, except for private torrents where pex is
 * disabled and cannot be enabled.
 **********************************************************************/
void tr_torrentDisablePex( tr_torrent *, int disable );
int tr_torrentIsPexEnabled( const tr_torrent * );

const tr_info * tr_torrentInfo( const tr_torrent * );

void   tr_torrentSetFolder( tr_torrent *, const char * );
const char * tr_torrentGetFolder( const tr_torrent * );

/***********************************************************************
 * tr_torrentStart
 ***********************************************************************
 * Starts downloading. The download is launched in a seperate thread,
 * therefore tr_torrentStart returns immediately.
 **********************************************************************/
void   tr_torrentStart( tr_torrent * );

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
void tr_torrentStop( tr_torrent * );


/**
***  Register to be notified whenever a torrent's state changes.
**/

typedef enum
{
    TR_CP_INCOMPLETE,   /* doesn't have all the desired pieces */
    TR_CP_DONE,         /* has all the pieces but the DND ones */
    TR_CP_COMPLETE      /* has every piece */
}
cp_status_t;

typedef void (tr_torrent_status_func)(tr_torrent   * torrent,
                                      cp_status_t    status,
                                      void         * user_data );

void tr_torrentSetStatusCallback( tr_torrent             * torrent,
                                  tr_torrent_status_func   func,
                                  void                   * user_data );

void tr_torrentClearStatusCallback( tr_torrent * torrent );

/**
 * MANUAL ANNOUNCE
 *
 * Trackers usually set an announce interval of 15 or 30 minutes.
 * Users can send one-time announce requests that override this
 * interval by calling tr_manualUpdate().
 *
 * The wait interval for tr_manualUpdate() is much smaller.
 * You can test whether or not a manual update is possible
 * (for example, to desensitize the button) by calling
 * tr_torrentCanManualUpdate().
 */

void tr_manualUpdate( tr_torrent * );

int tr_torrentCanManualUpdate( const tr_torrent * );

/***********************************************************************
 * tr_torrentStat
 ***********************************************************************
 * Returns a pointer to an tr_stat structure with updated information
 * on the torrent. The structure belongs to libtransmission (do not
 * free it) and is guaranteed to be unchanged until the next call to
 * tr_torrentStat.
 * The interface should call this function every second or so in order
 * to update itself.
 **********************************************************************/
typedef struct tr_stat tr_stat;
const tr_stat * tr_torrentStat( tr_torrent * );

/***********************************************************************
 * tr_torrentPeers
 ***********************************************************************/
typedef struct tr_peer_stat tr_peer_stat;
tr_peer_stat * tr_torrentPeers( const tr_torrent *, int * peerCount );
void tr_torrentPeersFree( tr_peer_stat *, int peerCount );

typedef struct tr_file_stat tr_file_stat;
tr_file_stat * tr_torrentFiles( const tr_torrent *, int * fileCount );
void tr_torrentFilesFree( tr_file_stat *, int fileCount );


/***********************************************************************
 * tr_torrentAvailability
 ***********************************************************************
 * Use this to draw an advanced progress bar which is 'size' pixels
 * wide. Fills 'tab' which you must have allocated: each byte is set
 * to either -1 if we have the piece, otherwise it is set to the number
 * of connected peers who have the piece.
 **********************************************************************/
void tr_torrentAvailability( const tr_torrent *, int8_t * tab, int size );

void tr_torrentAmountFinished( const tr_torrent * tor, float * tab, int size );

/***********************************************************************
 * tr_torrentRemoveSaved
 ***********************************************************************
 * Removes the saved copy of a torrent file for torrents which the
 * TR_FLAG_SAVE flag is set.
 **********************************************************************/
void tr_torrentRemoveSaved( tr_torrent * );

void tr_torrentRecheck( tr_torrent * );

/***********************************************************************
 * tr_torrentClose
 ***********************************************************************
 * Frees memory allocated by tr_torrentInit. If the torrent was running,
 * it is stopped first.
 **********************************************************************/
void tr_torrentClose( tr_torrent * );

/***********************************************************************
 * tr_info
 **********************************************************************/

typedef struct tr_file
{
    uint64_t length;                /* Length of the file, in bytes */
    char     name[MAX_PATH_LENGTH]; /* Path to the file */
    int8_t   priority;              /* TR_PRI_HIGH, _NORMAL, or _LOW */
    int8_t   dnd;                   /* nonzero if the file shouldn't be downloaded */
    int      firstPiece;            /* We need pieces [firstPiece... */
    int      lastPiece;             /* ...lastPiece] to dl this file */
    uint64_t offset;                /* file begins at the torrent's nth byte */
}
tr_file;

typedef struct tr_piece
{
    uint8_t  hash[SHA_DIGEST_LENGTH];  /* pieces hash */
    int8_t   priority;                 /* TR_PRI_HIGH, _NORMAL, or _LOW */
    int8_t   dnd;                      /* nonzero if the piece shouldn't be downloaded */
}
tr_piece;
    
typedef struct tr_tracker_info
{
    char * address;
    int    port;
    char * announce;
    char * scrape;
}
tr_tracker_info;

struct tr_info
{
    /* Path to torrent */
    char                 torrent[MAX_PATH_LENGTH];

    /* General info */
    uint8_t              hash[SHA_DIGEST_LENGTH];
    char                 hashString[2*SHA_DIGEST_LENGTH+1];
    char                 name[MAX_PATH_LENGTH];

    /* Flags */
    unsigned int isPrivate : 1;
    unsigned int isMultifile : 1;

    /* Tracker info */
    struct
    {
        tr_tracker_info  * list;
        int                 count;
    }                  * trackerList;
    int                  trackerTiers;
    char               * primaryAddress;

    /* Torrent info */
    char                 comment[MAX_PATH_LENGTH];
    char                 creator[MAX_PATH_LENGTH];
    int                  dateCreated;

    /* Pieces info */
    int                  pieceSize;
    int                  pieceCount;
    uint64_t             totalSize;
    tr_piece           * pieces;

    /* Files info */
    int                  fileCount;
    tr_file            * files;
};

typedef enum
{
    TR_STATUS_CHECK_WAIT   = (1<<0), /* Waiting in queue to check files */
    TR_STATUS_CHECK        = (1<<1), /* Checking files */
    TR_STATUS_DOWNLOAD     = (1<<2), /* Downloading */
    TR_STATUS_DONE         = (1<<3), /* not at 100% so can't tell the tracker
                                        we're a seeder, but due to DND files
                                        there's nothing we want right now */
    TR_STATUS_SEED         = (1<<4), /* Seeding */
    TR_STATUS_STOPPED      = (1<<5)  /* Torrent is stopped */
}
tr_torrent_status;

#define TR_STATUS_IS_ACTIVE(s) ((s) != TR_STATUS_STOPPED)

/***********************************************************************
 * tr_stat
 **********************************************************************/
struct tr_stat
{
    tr_torrent_status   status;

    int                 error;
    char                errorString[128];

    const tr_tracker_info * tracker;

    float               recheckProgress;
    float               percentComplete;
    float               percentDone;
    float               rateDownload;
    float               rateUpload;
    int                 eta;
    int                 peersKnown;
    int                 peersConnected;
    int                 peersFrom[TR_PEER_FROM__MAX];
    int                 peersSendingToUs;
    int                 peersGettingFromUs;
    int                 seeders;
    int                 leechers;
    int                 completedFromTracker;

    /* Byte count of how much data is left to be downloaded until
     * we're done -- that is, until we've got all the pieces we wanted. */
    uint64_t            leftUntilDone;

    /* Byte count of all the corrupt data you've ever downloaded for
     * this torrent.  If you're on a poisoned torrent, this number can
     * grow very large. */
    uint64_t            corruptEver;

    /* Byte count of all data you've ever uploaded for this torrent. */
    uint64_t            uploadedEver;

    /* Byte count of all the non-corrupt data you've ever downloaded
     * for this torrent.  If you deleted the files and downloaded a second time,
     * this will be 2*totalSize.. */
    uint64_t            downloadedEver;

    /* Byte count of all the checksum-verified data we have for this torrent. */
    uint64_t            haveValid;

    /* Byte count of all the partial piece data we have for this torrent.
     * As pieces become complete, this value may decrease as portions of it are
     * moved to `corrupt' or `haveValid'. */
    uint64_t            haveUnchecked;

    /* Byte count of all the non-DND piece data that either we already have,
     * or that a peer we're connected to has. [0...desiredSize] */
    uint64_t            desiredAvailable;

    /* Byte count of all the piece data we want, whether we currently
     * have it nor not. [0...tr_info.totalSize] */
    uint64_t            desiredSize;

    float               swarmspeed;

#define TR_RATIO_NA  -1
    float               ratio;
    
    uint64_t            startDate;
    uint64_t            activityDate;
};

struct tr_file_stat
{
    uint64_t bytesCompleted;
    float progress;
};

typedef enum
{
    TR_PEER_STATUS_HANDSHAKE,
    TR_PEER_STATUS_PEER_IS_CHOKED,
    TR_PEER_STATUS_CLIENT_IS_CHOKED,
    TR_PEER_STATUS_CLIENT_IS_INTERESTED,
    TR_PEER_STATUS_READY,
    TR_PEER_STATUS_ACTIVE_AND_CHOKED,
    TR_PEER_STATUS_REQUEST_SENT,
    TR_PEER_STATUS_ACTIVE
}
tr_peer_status;

struct tr_peer_stat
{
    char addr[INET_ADDRSTRLEN];
    char client[80];
    
    unsigned int isEncrypted   : 1;
    unsigned int isDownloading : 1;
    unsigned int isUploading   : 1;

    tr_peer_status status;

    uint8_t  from;
    uint16_t port;
    
    float progress;
    float downloadFromRate;
    float uploadToRate;
};

struct tr_msg_list
{
    uint8_t              level;
    time_t               when;
    char               * message;
    struct tr_msg_list * next;
};

struct tr_handle_status
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
