/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2012 Transmission authors and contributors
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
#import <Quartz/Quartz.h>

@class FileListNode;

typedef enum {
	TorrentDeterminationAutomatic = 0,
	TorrentDeterminationUserSpecified
} TorrentDeterminationType;

#define kTorrentDidChangeGroupNotification @"TorrentDidChangeGroup"

@interface Torrent : NSObject <NSCopying, QLPreviewItem>
{
    tr_torrent * fHandle;
    const tr_info * fInfo;
    const tr_stat * fStat;
	
    NSUserDefaults * fDefaults;

    NSImage * fIcon;
    
    NSString * fHashString;
    
    tr_file_stat * fFileStat;
    NSArray * fFileList, * fFlatFileList;
    
    NSIndexSet * fPreviousFinishedIndexes;
    NSDate * fPreviousFinishedIndexesDate;
    
    BOOL fRemoveWhenFinishSeeding;
    
    NSInteger fGroupValue;
	TorrentDeterminationType fGroupValueDetermination;
	
	TorrentDeterminationType fDownloadFolderDetermination;
    
    BOOL fResumeOnWake;
    
    BOOL fTimeMachineExcludeInitialized;
}

- (id) initWithPath: (NSString *) path location: (NSString *) location deleteTorrentFile: (BOOL) torrentDelete
        lib: (tr_session *) lib;
- (id) initWithTorrentStruct: (tr_torrent *) torrentStruct location: (NSString *) location lib: (tr_session *) lib;
- (id) initWithMagnetAddress: (NSString *) address location: (NSString *) location lib: (tr_session *) lib;
- (id) initWithHistory: (NSDictionary *) history lib: (tr_session *) lib forcePause: (BOOL) pause;

- (NSDictionary *) history;

- (void) closeRemoveTorrent: (BOOL) trashFiles;

- (void) changeDownloadFolderBeforeUsing: (NSString *) folder determinationType: (TorrentDeterminationType) determinationType;

- (NSString *) currentDirectory;

- (void) getAvailability: (int8_t *) tab size: (NSInteger) size;
- (void) getAmountFinished: (float *) tab size: (NSInteger) size;
- (NSIndexSet *) previousFinishedPieces;
- (void) setPreviousFinishedPieces: (NSIndexSet *) indexes;

- (void) update;

- (void) startTransferIgnoringQueue: (BOOL) ignoreQueue;
- (void) startTransferNoQueue;
- (void) startTransfer;
- (void) stopTransfer;
- (void) sleep;
- (void) wakeUp;

- (NSInteger) queuePosition;
- (void) setQueuePosition: (NSUInteger) index;

- (void) manualAnnounce;
- (BOOL) canManualAnnounce;

- (void) resetCache;

- (BOOL) isMagnet;
- (NSString *) magnetLink;

- (CGFloat) ratio;
- (tr_ratiolimit) ratioSetting;
- (void) setRatioSetting: (tr_ratiolimit) setting;
- (CGFloat) ratioLimit;
- (void) setRatioLimit: (CGFloat) limit;
- (CGFloat) progressStopRatio;

- (tr_idlelimit) idleSetting;
- (void) setIdleSetting: (tr_idlelimit) setting;
- (NSUInteger) idleLimitMinutes;
- (void) setIdleLimitMinutes: (NSUInteger) limit;

- (BOOL) usesSpeedLimit: (BOOL) upload;
- (void) setUseSpeedLimit: (BOOL) use upload: (BOOL) upload;
- (NSInteger) speedLimit: (BOOL) upload;
- (void) setSpeedLimit: (NSInteger) limit upload: (BOOL) upload;
- (BOOL) usesGlobalSpeedLimit;
- (void) setUseGlobalSpeedLimit: (BOOL) use;

- (void) setMaxPeerConnect: (uint16_t) count;
- (uint16_t) maxPeerConnect;

@property (nonatomic) BOOL removeWhenFinishSeeding;

- (BOOL) waitingToStart;

- (tr_priority_t) priority;
- (void) setPriority: (tr_priority_t) priority;

+ (BOOL) trashFile: (NSString *) path error: (NSError **) error;
- (void) moveTorrentDataFileTo: (NSString *) folder;
- (void) copyTorrentFileTo: (NSString *) path;

- (BOOL) alertForRemainingDiskSpace;

- (NSImage *) icon;

- (NSString *) name;
- (BOOL) isFolder;
- (uint64_t) size;
- (uint64_t) sizeLeft;

- (NSMutableArray *) allTrackerStats;
- (NSArray *) allTrackersFlat; //used by GroupRules
- (BOOL) addTrackerToNewTier: (NSString *) tracker;
- (void) removeTrackers: (NSSet *) trackers;

- (NSString *) comment;
- (NSString *) creator;
- (NSDate *) dateCreated;

- (NSInteger) pieceSize;
- (NSInteger) pieceCount;
- (NSString *) hashString;
- (BOOL) privateTorrent;

- (NSString *) torrentLocation;
- (NSString *) dataLocation;
- (NSString *) fileLocation: (FileListNode *) node;

- (void) renameTorrent: (NSString *) newName completionHandler: (void (^)(BOOL didRename)) completionHandler;
- (void) renameFileNode: (FileListNode *) node withName: (NSString *) newName completionHandler: (void (^)(BOOL didRename)) completionHandler;

- (CGFloat) progress;
- (CGFloat) progressDone;
- (CGFloat) progressLeft;
- (CGFloat) checkingProgress;

- (CGFloat) availableDesired;

- (BOOL) isActive;
- (BOOL) isSeeding;
- (BOOL) isChecking;
- (BOOL) isCheckingWaiting;
- (BOOL) allDownloaded;
- (BOOL) isComplete;
- (BOOL) isFinishedSeeding;
- (BOOL) isError;
- (BOOL) isAnyErrorOrWarning;
- (NSString *) errorMessage;

- (NSArray *) peers;

- (NSUInteger) webSeedCount;
- (NSArray *) webSeeds;

- (NSString *) progressString;
- (NSString *) statusString;
- (NSString *) shortStatusString;
- (NSString *) remainingTimeString;

- (NSString *) stateString;
- (NSInteger) totalPeersConnected;
- (NSInteger) totalPeersTracker;
- (NSInteger) totalPeersIncoming;
- (NSInteger) totalPeersCache;
- (NSInteger) totalPeersPex;
- (NSInteger) totalPeersDHT;
- (NSInteger) totalPeersLocal;
- (NSInteger) totalPeersLTEP;

- (NSInteger) peersSendingToUs;
- (NSInteger) peersGettingFromUs;

- (CGFloat) downloadRate;
- (CGFloat) uploadRate;
- (CGFloat) totalRate;
- (uint64_t) haveVerified;
- (uint64_t) haveTotal;
- (uint64_t) totalSizeSelected;
- (uint64_t) downloadedTotal;
- (uint64_t) uploadedTotal;
- (uint64_t) failedHash;

- (NSInteger) groupValue;
- (void) setGroupValue: (NSInteger) groupValue determinationType: (TorrentDeterminationType) determinationType;;
- (NSInteger) groupOrderValue;
- (void) checkGroupValueForRemoval: (NSNotification *) notification;

- (NSArray *) fileList;
- (NSArray *) flatFileList;
- (NSInteger) fileCount;
- (void) updateFileStat;

//methods require fileStats to have been updated recently to be accurate
- (CGFloat) fileProgress: (FileListNode *) node;
- (BOOL) canChangeDownloadCheckForFile: (NSUInteger) index;
- (BOOL) canChangeDownloadCheckForFiles: (NSIndexSet *) indexSet;
- (NSInteger) checkForFiles: (NSIndexSet *) indexSet;
- (void) setFileCheckState: (NSInteger) state forIndexes: (NSIndexSet *) indexSet;
- (void) setFilePriority: (tr_priority_t) priority forIndexes: (NSIndexSet *) indexSet;
- (BOOL) hasFilePriority: (tr_priority_t) priority forIndexes: (NSIndexSet *) indexSet;
- (NSSet *) filePrioritiesForIndexes: (NSIndexSet *) indexSet;

- (NSDate *) dateAdded;
- (NSDate *) dateCompleted;
- (NSDate *) dateActivity;
- (NSDate *) dateActivityOrAdd;

- (NSInteger) secondsDownloading;
- (NSInteger) secondsSeeding;

- (NSInteger) stalledMinutes;
- (BOOL) isStalled;

- (void) updateTimeMachineExclude;

- (NSInteger) stateSortKey;
- (NSString *) trackerSortKey;

- (tr_torrent *) torrentStruct;

@end
