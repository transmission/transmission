/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

#import <Cocoa/Cocoa.h>
#import <transmission.h>

#define INVALID -99

typedef enum
{
    TORRENT_FILE_DELETE,
    TORRENT_FILE_SAVE,
    TORRENT_FILE_DEFAULT
} torrentFileState;

@interface Torrent : NSObject
{
    tr_handle * fLib;
    tr_torrent * fHandle;
    const tr_info * fInfo;
    const tr_stat * fStat;
    
    int fID;
    
    BOOL         fResumeOnWake;
    NSDate       * fDateAdded, * fDateCompleted, * fDateActivity;
    
    BOOL        fUseIncompleteFolder;
    NSString    * fDownloadFolder, * fIncompleteFolder;
    
    BOOL        fPublicTorrent;
    NSString    * fPublicTorrentLocation;
	
    NSUserDefaults * fDefaults;

    NSImage * fIcon;
    
    NSString * fNameString, * fHashString;
    
    tr_file_stat * fileStat;
    NSArray * fFileList;
    
    NSMenu * fFileMenu;
    
    float * fPreviousFinishedPieces;
    NSDate * fFinishedPiecesDate;
    
    float fRatioLimit;
    int fRatioSetting;
    BOOL fFinishedSeeding, fWaitToStart, fStalled;
    
    int fOrderValue, fGroupValue;
    
    NSDictionary * fQuickPauseDict;
}

- (id) initWithPath: (NSString *) path location: (NSString *) location deleteTorrentFile: (torrentFileState) torrentDelete
        lib: (tr_handle *) lib;
- (id) initWithData: (NSData *) data location: (NSString *) location lib: (tr_handle *) lib;
- (id) initWithHistory: (NSDictionary *) history lib: (tr_handle *) lib;

- (NSDictionary *) history;

- (void) closeRemoveTorrent;

- (void) changeIncompleteDownloadFolder: (NSString *) folder;
- (void) changeDownloadFolder: (NSString *) folder;
- (NSString *) downloadFolder;

- (void) getAvailability: (int8_t *) tab size: (int) size;
- (void) getAmountFinished: (float *) tab size: (int) size;
- (float *) getPreviousAmountFinished;
-(void) setPreviousAmountFinished: (float *) tab;

- (void) update;

- (void) startTransfer;
- (void) stopTransfer;
- (void) sleep;
- (void) wakeUp;

- (void) manualAnnounce;
- (BOOL) canManualAnnounce;

- (void) resetCache;

- (float) ratio;
- (int) ratioSetting;
- (void) setRatioSetting: (int) setting;
- (float) ratioLimit;
- (void) setRatioLimit: (float) limit;
- (float) actualStopRatio; //returns INVALID if will not stop
- (float) progressStopRatio;

- (tr_speedlimit) speedMode: (BOOL) upload;
- (void) setSpeedMode: (tr_speedlimit) mode upload: (BOOL) upload;
- (int) speedLimit: (BOOL) upload;
- (void) setSpeedLimit: (int) limit upload: (BOOL) upload;

- (void) setMaxPeerConnect: (uint16_t) count;
- (uint16_t) maxPeerConnect;

- (void) setWaitToStart: (BOOL) wait;
- (BOOL) waitingToStart;

- (void) revealData;
- (void) revealPublicTorrent;
- (void) trashData;
- (void) trashTorrent;
- (void) moveTorrentDataFileTo: (NSString *) folder;
- (void) copyTorrentFileTo: (NSString *) path;

- (BOOL) alertForRemainingDiskSpace;
- (BOOL) alertForFolderAvailable;
- (BOOL) alertForMoveFolderAvailable;

- (NSImage *) icon;

- (NSString *) name;
- (BOOL) folder;
- (uint64_t) size;
- (uint64_t) sizeLeft;

- (NSString *) trackerAddress;
- (NSString *) trackerAddressAnnounce;
- (NSDate *) lastAnnounceTime;
- (int) nextAnnounceTime;
- (int) manualAnnounceTime;
- (NSString *) announceResponse;

- (NSString *) trackerAddressScrape;
- (NSDate *) lastScrapeTime;
- (int) nextScrapeTime;
- (NSString *) scrapeResponse;

- (NSArray *) allTrackers;

- (NSString *) comment;
- (NSString *) creator;
- (NSDate *) dateCreated;

- (int) pieceSize;
- (int) pieceCount;
- (NSString *) hashString;
- (BOOL) privateTorrent;

- (NSString *) torrentLocation;
- (NSString *) publicTorrentLocation;
- (NSString *) dataLocation;

- (BOOL) publicTorrent;

- (float) progress;
- (float) progressDone;
- (float) progressLeft;
- (float) checkingProgress;

- (int) eta;
- (int) etaRatio;

- (float) notAvailableDesired;

- (BOOL) isActive;
- (BOOL) isSeeding;
- (BOOL) isChecking;
- (BOOL) allDownloaded;
- (BOOL) isComplete;
- (BOOL) isError;
- (NSString *) errorMessage;

- (NSArray *) peers;

- (NSString *) progressString;
- (NSString *) statusString;
- (NSString *) shortStatusString;
- (NSString *) remainingTimeString;

- (NSString *) stateString;

- (int) seeders;
- (int) leechers;
- (int) completedFromTracker;

- (int) totalPeersConnected;
- (int) totalPeersTracker;
- (int) totalPeersIncoming;
- (int) totalPeersCache;
- (int) totalPeersPex;
- (int) totalPeersKnown;

- (int) peersSendingToUs;
- (int) peersGettingFromUs;

- (float) downloadRate;
- (float) uploadRate;
- (float) totalRate;
- (uint64_t) haveVerified;
- (uint64_t) haveTotal;
- (uint64_t) downloadedTotal;
- (uint64_t) uploadedTotal;
- (uint64_t) failedHash;
- (float) swarmSpeed;

- (int) orderValue;
- (void) setOrderValue: (int) orderValue;

- (int) groupValue;
- (void) setGroupValue: (int) groupValue;
- (int) groupOrderValue;
- (void) checkGroupValueForRemoval: (NSNotification *) notification;

- (NSArray *) fileList;
- (int) fileCount;
- (void) updateFileStat;

//methods require fileStats to have been updated recently to be accurate
- (float) fileProgress: (int) index;
- (BOOL) canChangeDownloadCheckForFile: (int) index;
- (BOOL) canChangeDownloadCheckForFiles: (NSIndexSet *) indexSet;
- (int) checkForFiles: (NSIndexSet *) indexSet;
- (void) setFileCheckState: (int) state forIndexes: (NSIndexSet *) indexSet;
- (void) setFilePriority: (int) priority forIndexes: (NSIndexSet *) indexSet;
- (BOOL) hasFilePriority: (int) priority forIndexes: (NSIndexSet *) indexSet;
- (NSSet *) filePrioritiesForIndexes: (NSIndexSet *) indexSet;

- (NSMenu *) fileMenu;

- (NSDate *) dateAdded;
- (NSDate *) dateCompleted;
- (NSDate *) dateActivity;
- (NSDate *) dateActivityOrAdd;

- (int) stalledMinutes;
- (BOOL) isStalled;

- (NSNumber *) stateSortKey;

- (int) torrentID;
- (const tr_info *) torrentInfo;
- (const tr_stat *) torrentStat;

@end
