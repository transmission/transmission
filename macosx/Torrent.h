/******************************************************************************
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

typedef NS_ENUM(unsigned int, TorrentDeterminationType) {
    TorrentDeterminationAutomatic = 0,
    TorrentDeterminationUserSpecified
};

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

- (instancetype) initWithPath: (NSString *) path location: (NSString *) location deleteTorrentFile: (BOOL) torrentDelete
        lib: (tr_session *) lib;
- (instancetype) initWithTorrentStruct: (tr_torrent *) torrentStruct location: (NSString *) location lib: (tr_session *) lib;
- (instancetype) initWithMagnetAddress: (NSString *) address location: (NSString *) location lib: (tr_session *) lib;
- (instancetype) initWithHistory: (NSDictionary *) history lib: (tr_session *) lib forcePause: (BOOL) pause;

@property (nonatomic, readonly, copy) NSDictionary *history;

- (void) closeRemoveTorrent: (BOOL) trashFiles;

- (void) changeDownloadFolderBeforeUsing: (NSString *) folder determinationType: (TorrentDeterminationType) determinationType;

@property (nonatomic, readonly, copy) NSString *currentDirectory;

- (void) getAvailability: (int8_t *) tab size: (NSInteger) size;
- (void) getAmountFinished: (float *) tab size: (NSInteger) size;
@property (nonatomic, copy) NSIndexSet *previousFinishedPieces;

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
@property (nonatomic, readonly) BOOL canManualAnnounce;

- (void) resetCache;

@property (nonatomic, getter=isMagnet, readonly) BOOL magnet;
@property (nonatomic, readonly, copy) NSString *magnetLink;

@property (nonatomic, readonly) CGFloat ratio;
@property (nonatomic) tr_ratiolimit ratioSetting;
@property (nonatomic) CGFloat ratioLimit;
@property (nonatomic, readonly) CGFloat progressStopRatio;

@property (nonatomic) tr_idlelimit idleSetting;
@property (nonatomic) NSUInteger idleLimitMinutes;

- (BOOL) usesSpeedLimit: (BOOL) upload;
- (void) setUseSpeedLimit: (BOOL) use upload: (BOOL) upload;
- (NSInteger) speedLimit: (BOOL) upload;
- (void) setSpeedLimit: (NSInteger) limit upload: (BOOL) upload;
@property (nonatomic, readonly) BOOL usesGlobalSpeedLimit;
- (void) setUseGlobalSpeedLimit: (BOOL) use;

@property (nonatomic) uint16_t maxPeerConnect;

@property (nonatomic) BOOL removeWhenFinishSeeding;

@property (nonatomic, readonly) BOOL waitingToStart;

@property (nonatomic) tr_priority_t priority;

+ (BOOL) trashFile: (NSString *) path error: (NSError **) error;
- (void) moveTorrentDataFileTo: (NSString *) folder;
- (void) copyTorrentFileTo: (NSString *) path;

@property (nonatomic, readonly) BOOL alertForRemainingDiskSpace;

@property (nonatomic, readonly, copy) NSImage *icon;

@property (nonatomic, readonly, copy) NSString *name;
@property (nonatomic, getter=isFolder, readonly) BOOL folder;
@property (nonatomic, readonly) uint64_t size;
@property (nonatomic, readonly) uint64_t sizeLeft;

@property (nonatomic, readonly, copy) NSMutableArray *allTrackerStats;
@property (nonatomic, readonly, copy) NSArray *allTrackersFlat; //used by GroupRules
- (BOOL) addTrackerToNewTier: (NSString *) tracker;
- (void) removeTrackers: (NSSet *) trackers;

@property (nonatomic, readonly, copy) NSString *comment;
@property (nonatomic, readonly, copy) NSString *creator;
@property (nonatomic, readonly, copy) NSDate *dateCreated;

@property (nonatomic, readonly) NSInteger pieceSize;
@property (nonatomic, readonly) NSInteger pieceCount;
@property (nonatomic, readonly, copy) NSString *hashString;
@property (nonatomic, readonly) BOOL privateTorrent;

@property (nonatomic, readonly, copy) NSString *torrentLocation;
@property (nonatomic, readonly, copy) NSString *dataLocation;
- (NSString *) fileLocation: (FileListNode *) node;

- (void) renameTorrent: (NSString *) newName completionHandler: (void (^)(BOOL didRename)) completionHandler;
- (void) renameFileNode: (FileListNode *) node withName: (NSString *) newName completionHandler: (void (^)(BOOL didRename)) completionHandler;

@property (nonatomic, readonly) CGFloat progress;
@property (nonatomic, readonly) CGFloat progressDone;
@property (nonatomic, readonly) CGFloat progressLeft;
@property (nonatomic, readonly) CGFloat checkingProgress;

@property (nonatomic, readonly) CGFloat availableDesired;

@property (nonatomic, getter=isActive, readonly) BOOL active;
@property (nonatomic, getter=isSeeding, readonly) BOOL seeding;
@property (nonatomic, getter=isChecking, readonly) BOOL checking;
@property (nonatomic, getter=isCheckingWaiting, readonly) BOOL checkingWaiting;
@property (nonatomic, readonly) BOOL allDownloaded;
@property (nonatomic, getter=isComplete, readonly) BOOL complete;
@property (nonatomic, getter=isFinishedSeeding, readonly) BOOL finishedSeeding;
@property (nonatomic, getter=isError, readonly) BOOL error;
@property (nonatomic, getter=isAnyErrorOrWarning, readonly) BOOL anyErrorOrWarning;
@property (nonatomic, readonly, copy) NSString *errorMessage;

@property (nonatomic, readonly, copy) NSArray *peers;

@property (nonatomic, readonly) NSUInteger webSeedCount;
@property (nonatomic, readonly, copy) NSArray *webSeeds;

@property (nonatomic, readonly, copy) NSString *progressString;
@property (nonatomic, readonly, copy) NSString *statusString;
@property (nonatomic, readonly, copy) NSString *shortStatusString;
@property (nonatomic, readonly, copy) NSString *remainingTimeString;

@property (nonatomic, readonly, copy) NSString *stateString;
@property (nonatomic, readonly) NSInteger totalPeersConnected;
@property (nonatomic, readonly) NSInteger totalPeersTracker;
@property (nonatomic, readonly) NSInteger totalPeersIncoming;
@property (nonatomic, readonly) NSInteger totalPeersCache;
@property (nonatomic, readonly) NSInteger totalPeersPex;
@property (nonatomic, readonly) NSInteger totalPeersDHT;
@property (nonatomic, readonly) NSInteger totalPeersLocal;
@property (nonatomic, readonly) NSInteger totalPeersLTEP;

@property (nonatomic, readonly) NSInteger peersSendingToUs;
@property (nonatomic, readonly) NSInteger peersGettingFromUs;

@property (nonatomic, readonly) CGFloat downloadRate;
@property (nonatomic, readonly) CGFloat uploadRate;
@property (nonatomic, readonly) CGFloat totalRate;
@property (nonatomic, readonly) uint64_t haveVerified;
@property (nonatomic, readonly) uint64_t haveTotal;
@property (nonatomic, readonly) uint64_t totalSizeSelected;
@property (nonatomic, readonly) uint64_t downloadedTotal;
@property (nonatomic, readonly) uint64_t uploadedTotal;
@property (nonatomic, readonly) uint64_t failedHash;

@property (nonatomic, readonly) NSInteger groupValue;
- (void) setGroupValue: (NSInteger) groupValue determinationType: (TorrentDeterminationType) determinationType;;
@property (nonatomic, readonly) NSInteger groupOrderValue;
- (void) checkGroupValueForRemoval: (NSNotification *) notification;

@property (nonatomic, readonly, copy) NSArray *fileList;
@property (nonatomic, readonly, copy) NSArray *flatFileList;
@property (nonatomic, readonly) NSInteger fileCount;
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

@property (nonatomic, readonly, copy) NSDate *dateAdded;
@property (nonatomic, readonly, copy) NSDate *dateCompleted;
@property (nonatomic, readonly, copy) NSDate *dateActivity;
@property (nonatomic, readonly, copy) NSDate *dateActivityOrAdd;

@property (nonatomic, readonly) NSInteger secondsDownloading;
@property (nonatomic, readonly) NSInteger secondsSeeding;

@property (nonatomic, readonly) NSInteger stalledMinutes;
@property (nonatomic, getter=isStalled, readonly) BOOL stalled;

- (void) updateTimeMachineExclude;

@property (nonatomic, readonly) NSInteger stateSortKey;
@property (nonatomic, readonly, copy) NSString *trackerSortKey;

@property (nonatomic, readonly) tr_torrent *torrentStruct;

@end
