/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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

@interface Torrent : NSObject
{
    tr_handle_t * fLib;
    tr_torrent_t * fHandle;
    const tr_info_t * fInfo;
    const tr_stat_t * fStat;
    
    int fID;
    
    BOOL         fResumeOnWake;
    NSDate       * fDateAdded, * fDateCompleted, * fDateActivity;
    
    BOOL        fUseIncompleteFolder;
    NSString    * fDownloadFolder, * fIncompleteFolder;
    
    BOOL        fPublicTorrent;
    NSString    * fPublicTorrentLocation;
	
	BOOL fPex;

    NSUserDefaults * fDefaults;

    NSImage * fIcon, * fIconFlipped, * fIconSmall;
    NSMutableString * fNameString, * fProgressString, * fStatusString, * fShortStatusString, * fRemainingTimeString;
    
    tr_file_stat_t * fileStat;
    NSArray * fFileList, * fFlatFileList;
    
    NSMenu * fTorrentMenu;
    
    float   fRatioLimit;
    int     fRatioSetting;
    BOOL    fFinishedSeeding, fWaitToStart, fError, fChecking, fStalled;
    
    int fOrderValue;
    
    NSBitmapImageRep * fBitmap;
    int8_t * fPieces;
    
    NSDictionary * fQuickPauseDict;
}

- (id) initWithPath: (NSString *) path location: (NSString *) location forceDeleteTorrent: (BOOL) delete lib: (tr_handle_t *) lib;
- (id) initWithHistory: (NSDictionary *) history lib: (tr_handle_t *) lib;

- (NSDictionary *) history;

- (void) closeTorrent;
- (void) closeRemoveTorrent;

- (void) changeIncompleteDownloadFolder: (NSString *) folder;
- (void) changeDownloadFolder: (NSString *) folder;
- (NSString *) downloadFolder;

- (void) getAvailability: (int8_t *) tab size: (int) size;
- (void) getAmountFinished: (float *) tab size: (int) size;

- (void) update;
- (NSDictionary *) infoForCurrentView;

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

- (int) speedMode: (BOOL) upload;
- (void) setSpeedMode: (int) mode upload: (BOOL) upload;
- (int) speedLimit: (BOOL) upload;
- (void) setSpeedLimit: (int) limit upload: (BOOL) upload;

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
- (NSImage *) iconFlipped;
- (NSImage *) iconSmall;

- (NSString *) name;
- (uint64_t) size;
- (NSString *) trackerAddress;
- (NSString *) trackerAddressAnnounce;

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

- (NSString *) stateString;

- (float) progress;
- (float) progressDone;
- (int) eta;

- (BOOL) isActive;
- (BOOL) isSeeding;
- (BOOL) isPaused;
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

- (int) seeders;
- (int) leechers;
- (int) completedFromTracker;

- (int) totalPeersConnected;
- (int) totalPeersTracker;
- (int) totalPeersIncoming;
- (int) totalPeersCache;
- (int) totalPeersPex;

- (int) peersSendingToUs;
- (int) peersGettingFromUs;

- (float) downloadRate;
- (float) uploadRate;
- (uint64_t) downloadedValid;
- (uint64_t) downloadedTotal;
- (uint64_t) uploadedTotal;
- (float) swarmSpeed;

- (BOOL) pex;
- (void) setPex: (BOOL) setting;

- (NSNumber *) orderValue;
- (void) setOrderValue: (int) orderValue;

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

- (NSMenu *) torrentMenu;

- (NSDate *) dateAdded;
- (NSDate *) dateCompleted;
- (NSDate *) dateActivity;

- (int) stalledMinutes;
- (BOOL) isStalled;

- (NSNumber *) stateSortKey;
- (NSNumber *) progressSortKey;
- (NSNumber *) ratioSortKey;

- (int) torrentID;
- (const tr_info_t *) torrentInfo;
- (const tr_stat_t *) torrentStat;

@end
