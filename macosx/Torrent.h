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

#define PRIORITY_LOW -1
#define PRIORITY_NORMAL 0
#define PRIORITY_HIGH 1

#define INVALID -99

@interface Torrent : NSObject
{
    tr_handle_t  * fLib;
    tr_torrent_t * fHandle;
    tr_info_t    * fInfo;
    tr_stat_t    * fStat;
    
    int fID;
    
    BOOL         fResumeOnWake;
    NSDate       * fDateAdded, * fDateCompleted, * fAnnounceDate,
                * fDateActivity;
    
    BOOL        fUseIncompleteFolder;
    NSString    * fDownloadFolder, * fIncompleteFolder;
    
    BOOL        fPublicTorrent;
    NSString    * fPublicTorrentLocation;
	
	BOOL fPex;

    NSUserDefaults * fDefaults;

    NSImage * fIcon, * fIconFlipped, * fIconSmall;
    NSMutableString * fNameString, * fProgressString, * fStatusString, * fShortStatusString, * fRemainingTimeString;
    
    NSArray * fFileList, * fFlatFileList;
    
    int     fUploadLimit, fDownloadLimit;
    float   fRatioLimit;
    int     fCheckUpload, fCheckDownload, fRatioSetting;
    BOOL    fFinishedSeeding, fWaitToStart, fError, fChecking, fStalled;
    
    int fOrderValue;
    
    NSBitmapImageRep * fBitmap;
    int8_t * fPieces;
}

- (id) initWithPath: (NSString *) path forceDeleteTorrent: (BOOL) delete lib: (tr_handle_t *) lib;
- (id) initWithHistory: (NSDictionary *) history lib: (tr_handle_t *) lib;

- (NSDictionary *) history;

- (void) setIncompleteFolder: (NSString *) folder;
- (void) setDownloadFolder: (NSString *) folder;
- (void) updateDownloadFolder;
- (NSString *) downloadFolder;

- (void) getAvailability: (int8_t *) tab size: (int) size;
- (void) getAmountFinished: (float *) tab size: (int) size;

- (void) update;
- (NSDictionary *) infoForCurrentView;

- (void)        startTransfer;
- (void)        stopTransfer;
- (void)        stopTransferForQuit;
- (void)        sleep;
- (void)        wakeUp;

- (void)        announce;
- (NSDate *)    announceDate;

- (void)        resetCache;

- (float)       ratio;
- (int)         ratioSetting;
- (void)        setRatioSetting: (int) setting;
- (float)       ratioLimit;
- (void)        setRatioLimit: (float) limit;
- (float)       actualStopRatio; //returns INVALID if will not stop
- (float)       progressStopRatio;

- (int)     checkUpload;
- (void)    setCheckUpload: (int) setting;
- (int)     uploadLimit;
- (void)    setUploadLimit: (int) limit;
- (int)     checkDownload;
- (void)    setCheckDownload: (int) setting;
- (int)     downloadLimit;
- (void)    setDownloadLimit: (int) limit;

- (void) updateSpeedSetting;

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

- (NSImage *)   icon;
- (NSImage *)   iconFlipped;
- (NSImage *)   iconSmall;

- (NSString *) name;
- (uint64_t)   size;
- (NSString *) trackerAddress;
- (NSString *) trackerAddressAnnounce;

- (NSString *) comment;
- (NSString *) creator;
- (NSDate *)   dateCreated;

- (int)        pieceSize;
- (int)        pieceCount;
- (NSString *) hashString;
- (BOOL)       privateTorrent;

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
- (BOOL) isWaitingToChecking;
- (BOOL) isChecking;
- (BOOL) allDownloaded;
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

- (int) totalPeers;
- (int) totalPeersTracker;
- (int) totalPeersIncoming;
- (int) totalPeersCache;
- (int) totalPeersPex;

- (int) peersUploading;
- (int) peersDownloading;

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
- (float) fileProgress: (int) index;
- (int) checkForFiles: (NSIndexSet *) indexSet;
- (BOOL) canChangeDownloadCheckForFiles: (NSIndexSet *) indexSet;
- (void) setFileCheckState: (int) state forIndexes: (NSIndexSet *) indexSet;
- (void) setFilePriority: (int) priority forIndexes: (NSIndexSet *) indexSet;
- (BOOL) hasFilePriority: (int) priority forIndexes: (NSIndexSet *) indexSet;

- (NSDate *) dateAdded;
- (NSDate *) dateCompleted;
- (NSDate *) dateActivity;

- (int) stalledMinutes;
- (BOOL) isStalled;

- (NSNumber *) stateSortKey;
- (NSNumber *) progressSortKey;
- (NSNumber *) ratioSortKey;

- (int) torrentID;
- (tr_info_t *) torrentInfo;
- (tr_stat_t *) torrentStat;

@end
