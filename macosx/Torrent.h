// This file Copyright © 2006-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>
#import <Quartz/Quartz.h>

#include <libtransmission/transmission.h>

@class FileListNode;

typedef NS_ENUM(unsigned int, TorrentDeterminationType) {
    TorrentDeterminationAutomatic = 0,
    TorrentDeterminationUserSpecified
};

#define kTorrentDidChangeGroupNotification @"TorrentDidChangeGroup"

@interface Torrent : NSObject<NSCopying, QLPreviewItem>

- (instancetype)initWithPath:(NSString*)path
                    location:(NSString*)location
           deleteTorrentFile:(BOOL)torrentDelete
                         lib:(tr_session*)lib;
- (instancetype)initWithTorrentStruct:(tr_torrent*)torrentStruct location:(NSString*)location lib:(tr_session*)lib;
- (instancetype)initWithMagnetAddress:(NSString*)address location:(NSString*)location lib:(tr_session*)lib;
- (void)setResumeStatusForTorrent:(Torrent*)torrent withHistory:(NSDictionary*)history forcePause:(BOOL)pause;

@property(nonatomic, readonly) NSDictionary* history;

- (void)closeRemoveTorrent:(BOOL)trashFiles;

- (void)changeDownloadFolderBeforeUsing:(NSString*)folder determinationType:(TorrentDeterminationType)determinationType;

@property(nonatomic, readonly) NSString* currentDirectory;

- (void)getAvailability:(int8_t*)tab size:(NSInteger)size;
- (void)getAmountFinished:(float*)tab size:(NSInteger)size;
@property(nonatomic) NSIndexSet* previousFinishedPieces;

- (void)update;

- (void)startTransferIgnoringQueue:(BOOL)ignoreQueue;
- (void)startTransferNoQueue;
- (void)startTransfer;
- (void)stopTransfer;
- (void)sleep;
- (void)wakeUp;

@property(nonatomic) NSUInteger queuePosition;

- (void)manualAnnounce;
@property(nonatomic, readonly) BOOL canManualAnnounce;

- (void)resetCache;

@property(nonatomic, getter=isMagnet, readonly) BOOL magnet;
@property(nonatomic, readonly) NSString* magnetLink;

@property(nonatomic, readonly) CGFloat ratio;
@property(nonatomic) tr_ratiolimit ratioSetting;
@property(nonatomic) CGFloat ratioLimit;
@property(nonatomic, readonly) CGFloat progressStopRatio;

@property(nonatomic) tr_idlelimit idleSetting;
@property(nonatomic) NSUInteger idleLimitMinutes;

- (BOOL)usesSpeedLimit:(BOOL)upload;
- (void)setUseSpeedLimit:(BOOL)use upload:(BOOL)upload;
- (NSInteger)speedLimit:(BOOL)upload;
- (void)setSpeedLimit:(NSInteger)limit upload:(BOOL)upload;
@property(nonatomic) BOOL usesGlobalSpeedLimit;

@property(nonatomic) uint16_t maxPeerConnect;

@property(nonatomic) BOOL removeWhenFinishSeeding;

@property(nonatomic, readonly) BOOL waitingToStart;

@property(nonatomic) tr_priority_t priority;

+ (BOOL)trashFile:(NSString*)path error:(NSError**)error;
- (void)moveTorrentDataFileTo:(NSString*)folder;
- (void)copyTorrentFileTo:(NSString*)path;

- (BOOL)alertForRemainingDiskSpace;

@property(nonatomic, readonly) NSImage* icon;

@property(nonatomic, readonly) NSString* name;
@property(nonatomic, getter=isFolder, readonly) BOOL folder;
@property(nonatomic, readonly) uint64_t size;
@property(nonatomic, readonly) uint64_t sizeLeft;

@property(nonatomic, readonly) NSMutableArray* allTrackerStats;
@property(nonatomic, readonly) NSArray* allTrackersFlat; //used by GroupRules
- (BOOL)addTrackerToNewTier:(NSString*)tracker;
- (void)removeTrackers:(NSSet*)trackers;

@property(nonatomic, readonly) NSString* comment;
@property(nonatomic, readonly) NSString* creator;
@property(nonatomic, readonly) NSDate* dateCreated;

@property(nonatomic, readonly) NSInteger pieceSize;
@property(nonatomic, readonly) NSInteger pieceCount;
@property(nonatomic, readonly) NSString* hashString;
@property(nonatomic, readonly) BOOL privateTorrent;

@property(nonatomic, readonly) NSString* torrentLocation;
@property(nonatomic, readonly) NSString* dataLocation;
- (NSString*)fileLocation:(FileListNode*)node;

- (void)renameTorrent:(NSString*)newName completionHandler:(void (^)(BOOL didRename))completionHandler;
- (void)renameFileNode:(FileListNode*)node
              withName:(NSString*)newName
     completionHandler:(void (^)(BOOL didRename))completionHandler;

@property(nonatomic, readonly) CGFloat progress;
@property(nonatomic, readonly) CGFloat progressDone;
@property(nonatomic, readonly) CGFloat progressLeft;
@property(nonatomic, readonly) CGFloat checkingProgress;

@property(nonatomic, readonly) CGFloat availableDesired;

@property(nonatomic, getter=isActive, readonly) BOOL active;
@property(nonatomic, getter=isSeeding, readonly) BOOL seeding;
@property(nonatomic, getter=isChecking, readonly) BOOL checking;
@property(nonatomic, getter=isCheckingWaiting, readonly) BOOL checkingWaiting;
@property(nonatomic, readonly) BOOL allDownloaded;
@property(nonatomic, getter=isComplete, readonly) BOOL complete;
@property(nonatomic, getter=isFinishedSeeding, readonly) BOOL finishedSeeding;
@property(nonatomic, getter=isError, readonly) BOOL error;
@property(nonatomic, getter=isAnyErrorOrWarning, readonly) BOOL anyErrorOrWarning;
@property(nonatomic, readonly) NSString* errorMessage;

@property(nonatomic, readonly) NSArray* peers;

@property(nonatomic, readonly) NSUInteger webSeedCount;
@property(nonatomic, readonly) NSArray* webSeeds;

@property(nonatomic, readonly) NSString* progressString;
@property(nonatomic, readonly) NSString* statusString;
@property(nonatomic, readonly) NSString* shortStatusString;
@property(nonatomic, readonly) NSString* remainingTimeString;

@property(nonatomic, readonly) NSString* stateString;
@property(nonatomic, readonly) NSInteger totalPeersConnected;
@property(nonatomic, readonly) NSInteger totalPeersTracker;
@property(nonatomic, readonly) NSInteger totalPeersIncoming;
@property(nonatomic, readonly) NSInteger totalPeersCache;
@property(nonatomic, readonly) NSInteger totalPeersPex;
@property(nonatomic, readonly) NSInteger totalPeersDHT;
@property(nonatomic, readonly) NSInteger totalPeersLocal;
@property(nonatomic, readonly) NSInteger totalPeersLTEP;

@property(nonatomic, readonly) NSInteger peersSendingToUs;
@property(nonatomic, readonly) NSInteger peersGettingFromUs;

@property(nonatomic, readonly) CGFloat downloadRate;
@property(nonatomic, readonly) CGFloat uploadRate;
@property(nonatomic, readonly) CGFloat totalRate;
@property(nonatomic, readonly) uint64_t haveVerified;
@property(nonatomic, readonly) uint64_t haveTotal;
@property(nonatomic, readonly) uint64_t totalSizeSelected;
@property(nonatomic, readonly) uint64_t downloadedTotal;
@property(nonatomic, readonly) uint64_t uploadedTotal;
@property(nonatomic, readonly) uint64_t failedHash;

@property(nonatomic, readonly) NSInteger groupValue;
- (void)setGroupValue:(NSInteger)groupValue determinationType:(TorrentDeterminationType)determinationType;
;
@property(nonatomic, readonly) NSInteger groupOrderValue;
- (void)checkGroupValueForRemoval:(NSNotification*)notification;

@property(nonatomic, readonly) NSArray* fileList;
@property(nonatomic, readonly) NSArray* flatFileList;
@property(nonatomic, readonly) NSInteger fileCount;

//methods require fileStats to have been updated recently to be accurate
- (CGFloat)fileProgress:(FileListNode*)node;
- (BOOL)canChangeDownloadCheckForFile:(NSUInteger)index;
- (BOOL)canChangeDownloadCheckForFiles:(NSIndexSet*)indexSet;
- (NSInteger)checkForFiles:(NSIndexSet*)indexSet;
- (void)setFileCheckState:(NSInteger)state forIndexes:(NSIndexSet*)indexSet;
- (void)setFilePriority:(tr_priority_t)priority forIndexes:(NSIndexSet*)indexSet;
- (BOOL)hasFilePriority:(tr_priority_t)priority forIndexes:(NSIndexSet*)indexSet;
- (NSSet*)filePrioritiesForIndexes:(NSIndexSet*)indexSet;

@property(nonatomic, readonly) NSDate* dateAdded;
@property(nonatomic, readonly) NSDate* dateCompleted;
@property(nonatomic, readonly) NSDate* dateActivity;
@property(nonatomic, readonly) NSDate* dateActivityOrAdd;

@property(nonatomic, readonly) NSInteger secondsDownloading;
@property(nonatomic, readonly) NSInteger secondsSeeding;

@property(nonatomic, readonly) NSInteger stalledMinutes;
@property(nonatomic, getter=isStalled, readonly) BOOL stalled;

- (void)updateTimeMachineExclude;

@property(nonatomic, readonly) NSInteger stateSortKey;
@property(nonatomic, readonly) NSString* trackerSortKey;

@property(nonatomic, readonly) tr_torrent* torrentStruct;

@end
