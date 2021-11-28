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

#include <libtransmission/transmission.h>
#include <libtransmission/error.h>
#include <libtransmission/log.h>
#include <libtransmission/utils.h> // tr_new()

#import "Torrent.h"
#import "GroupsController.h"
#import "FileListNode.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "TrackerNode.h"

#define ETA_IDLE_DISPLAY_SEC (2 * 60)

@interface Torrent (Private)

- (instancetype)initWithPath:(NSString*)path
                        hash:(NSString*)hashString
               torrentStruct:(tr_torrent*)torrentStruct
               magnetAddress:(NSString*)magnetAddress
                         lib:(tr_session*)lib
                  groupValue:(NSNumber*)groupValue
     removeWhenFinishSeeding:(NSNumber*)removeWhenFinishSeeding
              downloadFolder:(NSString*)downloadFolder
      legacyIncompleteFolder:(NSString*)incompleteFolder;

- (void)createFileList;
- (void)insertPathForComponents:(NSArray*)components
             withComponentIndex:(NSUInteger)componentIndex
                      forParent:(FileListNode*)parent
                       fileSize:(uint64_t)size
                          index:(NSInteger)index
                       flatList:(NSMutableArray*)flatFileList;
- (void)sortFileList:(NSMutableArray*)fileNodes;

- (void)startQueue;
- (void)completenessChange:(tr_completeness)status wasRunning:(BOOL)wasRunning;
- (void)ratioLimitHit;
- (void)idleLimitHit;
- (void)metadataRetrieved;
- (void)renameFinished:(BOOL)success
                 nodes:(NSArray*)nodes
     completionHandler:(void (^)(BOOL))completionHandler
               oldPath:(NSString*)oldPath
               newName:(NSString*)newName;

@property(nonatomic, readonly) BOOL shouldShowEta;
@property(nonatomic, readonly) NSString* etaString;

- (void)setTimeMachineExclude:(BOOL)exclude;

@end

void startQueueCallback(tr_torrent* torrent, void* torrentData)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [(__bridge Torrent*)torrentData startQueue];
    });
}

void completenessChangeCallback(tr_torrent* torrent, tr_completeness status, bool wasRunning, void* torrentData)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [(__bridge Torrent*)torrentData completenessChange:status wasRunning:wasRunning];
    });
}

void ratioLimitHitCallback(tr_torrent* torrent, void* torrentData)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [(__bridge Torrent*)torrentData ratioLimitHit];
    });
}

void idleLimitHitCallback(tr_torrent* torrent, void* torrentData)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [(__bridge Torrent*)torrentData idleLimitHit];
    });
}

void metadataCallback(tr_torrent* torrent, void* torrentData)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [(__bridge Torrent*)torrentData metadataRetrieved];
    });
}

void renameCallback(tr_torrent* torrent, char const* oldPathCharString, char const* newNameCharString, int error, void* contextInfo)
{
    @autoreleasepool
    {
        NSString* oldPath = @(oldPathCharString);
        NSString* newName = @(newNameCharString);

        dispatch_async(dispatch_get_main_queue(), ^{
            NSDictionary* contextDict = (__bridge_transfer NSDictionary*)contextInfo;
            Torrent* torrentObject = contextDict[@"Torrent"];
            [torrentObject renameFinished:error == 0 nodes:contextDict[@"Nodes"]
                        completionHandler:contextDict[@"CompletionHandler"]
                                  oldPath:oldPath
                                  newName:newName];
        });
    }
}

bool trashDataFile(char const* filename, tr_error** error)
{
    if (filename == NULL)
    {
        return false;
    }

    @autoreleasepool
    {
        NSError* localError;
        if (![Torrent trashFile:@(filename) error:&localError])
        {
            tr_error_set_literal(error, localError.code, localError.description.UTF8String);
            return false;
        }
    }

    return true;
}

@implementation Torrent
{
    tr_torrent* fHandle;
    tr_info const* fInfo;
    tr_stat const* fStat;

    NSUserDefaults* fDefaults;

    NSImage* fIcon;

    NSString* fHashString;

    NSArray* fFileList;
    NSArray* fFlatFileList;

    NSIndexSet* fPreviousFinishedIndexes;
    NSDate* fPreviousFinishedIndexesDate;

    NSInteger fGroupValue;
    TorrentDeterminationType fGroupValueDetermination;

    TorrentDeterminationType fDownloadFolderDetermination;

    BOOL fResumeOnWake;

    BOOL fTimeMachineExcludeInitialized;
}

- (instancetype)initWithPath:(NSString*)path
                    location:(NSString*)location
           deleteTorrentFile:(BOOL)torrentDelete
                         lib:(tr_session*)lib
{
    self = [self initWithPath:path hash:nil torrentStruct:NULL magnetAddress:nil lib:lib groupValue:nil
        removeWhenFinishSeeding:nil
                 downloadFolder:location
         legacyIncompleteFolder:nil];

    if (self)
    {
        if (torrentDelete && ![self.torrentLocation isEqualToString:path])
        {
            [Torrent trashFile:path error:nil];
        }
    }
    return self;
}

- (instancetype)initWithTorrentStruct:(tr_torrent*)torrentStruct location:(NSString*)location lib:(tr_session*)lib
{
    self = [self initWithPath:nil hash:nil torrentStruct:torrentStruct magnetAddress:nil lib:lib groupValue:nil
        removeWhenFinishSeeding:nil
                 downloadFolder:location
         legacyIncompleteFolder:nil];

    return self;
}

- (instancetype)initWithMagnetAddress:(NSString*)address location:(NSString*)location lib:(tr_session*)lib
{
    self = [self initWithPath:nil hash:nil torrentStruct:nil magnetAddress:address lib:lib groupValue:nil
        removeWhenFinishSeeding:nil
                 downloadFolder:location
         legacyIncompleteFolder:nil];

    return self;
}

- (instancetype)initWithHistory:(NSDictionary*)history lib:(tr_session*)lib forcePause:(BOOL)pause
{
    self = [self initWithPath:history[@"InternalTorrentPath"] hash:history[@"TorrentHash"] torrentStruct:NULL magnetAddress:nil
                            lib:lib
                     groupValue:history[@"GroupValue"]
        removeWhenFinishSeeding:history[@"RemoveWhenFinishSeeding"]
                 downloadFolder:history[@"DownloadFolder"] //upgrading from versions < 1.80
         legacyIncompleteFolder:[history[@"UseIncompleteFolder"] boolValue] //upgrading from versions < 1.80
             ?
             history[@"IncompleteFolder"] :
             nil];

    if (self)
    {
        //start transfer
        NSNumber* active;
        if (!pause && (active = history[@"Active"]) && active.boolValue)
        {
            fStat = tr_torrentStat(fHandle);
            [self startTransferNoQueue];
        }

        //upgrading from versions < 1.30: get old added, activity, and done dates
        NSDate* date;
        if ((date = history[@"Date"]))
        {
            tr_torrentSetAddedDate(fHandle, date.timeIntervalSince1970);
        }
        if ((date = history[@"DateActivity"]))
        {
            tr_torrentSetActivityDate(fHandle, date.timeIntervalSince1970);
        }
        if ((date = history[@"DateCompleted"]))
        {
            tr_torrentSetDoneDate(fHandle, date.timeIntervalSince1970);
        }

        //upgrading from versions < 1.60: get old stop ratio settings
        NSNumber* ratioSetting;
        if ((ratioSetting = history[@"RatioSetting"]))
        {
            switch (ratioSetting.intValue)
            {
            case NSControlStateValueOn:
                self.ratioSetting = TR_RATIOLIMIT_SINGLE;
                break;
            case NSControlStateValueOff:
                self.ratioSetting = TR_RATIOLIMIT_UNLIMITED;
                break;
            case NSControlStateValueMixed:
                self.ratioSetting = TR_RATIOLIMIT_GLOBAL;
                break;
            }
        }
        NSNumber* ratioLimit;
        if ((ratioLimit = history[@"RatioLimit"]))
        {
            self.ratioLimit = ratioLimit.floatValue;
        }
    }
    return self;
}

- (NSDictionary*)history
{
    return @{
        @"InternalTorrentPath" : self.torrentLocation,
        @"TorrentHash" : self.hashString,
        @"Active" : @(self.active),
        @"WaitToStart" : @(self.waitingToStart),
        @"GroupValue" : @(fGroupValue),
        @"RemoveWhenFinishSeeding" : @(_removeWhenFinishSeeding)
    };
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (NSString*)description
{
    return [@"Torrent: " stringByAppendingString:self.name];
}

- (id)copyWithZone:(NSZone*)zone
{
    return self;
}

- (void)closeRemoveTorrent:(BOOL)trashFiles
{
    //allow the file to be indexed by Time Machine
    [self setTimeMachineExclude:NO];

    tr_torrentRemove(fHandle, trashFiles, trashDataFile);
}

- (void)changeDownloadFolderBeforeUsing:(NSString*)folder determinationType:(TorrentDeterminationType)determinationType
{
    //if data existed in original download location, unexclude it before changing the location
    [self setTimeMachineExclude:NO];

    tr_torrentSetDownloadDir(fHandle, folder.UTF8String);

    fDownloadFolderDetermination = determinationType;
}

- (NSString*)currentDirectory
{
    return @(tr_torrentGetCurrentDir(fHandle));
}

- (void)getAvailability:(int8_t*)tab size:(NSInteger)size
{
    tr_torrentAvailability(fHandle, tab, size);
}

- (void)getAmountFinished:(float*)tab size:(NSInteger)size
{
    tr_torrentAmountFinished(fHandle, tab, size);
}

- (NSIndexSet*)previousFinishedPieces
{
    //if the torrent hasn't been seen in a bit, and therefore hasn't been refreshed, return nil
    if (fPreviousFinishedIndexesDate && fPreviousFinishedIndexesDate.timeIntervalSinceNow > -2.0)
    {
        return fPreviousFinishedIndexes;
    }
    else
    {
        return nil;
    }
}

- (void)setPreviousFinishedPieces:(NSIndexSet*)indexes
{
    fPreviousFinishedIndexes = indexes;

    fPreviousFinishedIndexesDate = indexes != nil ? [[NSDate alloc] init] : nil;
}

- (void)update
{
    //get previous stalled value before update
    BOOL const wasStalled = fStat != NULL && self.stalled;

    fStat = tr_torrentStat(fHandle);

    //make sure the "active" filter is updated when stalled-ness changes
    if (wasStalled != self.stalled)
    {
        //posting asynchronously with coalescing to prevent stack overflow on lots of torrents changing state at the same time
        [NSNotificationQueue.defaultQueue enqueueNotification:[NSNotification notificationWithName:@"UpdateQueue" object:self]
                                                 postingStyle:NSPostASAP
                                                 coalesceMask:NSNotificationCoalescingOnName
                                                     forModes:nil];
    }

    //when the torrent is first loaded, update the time machine exclusion
    if (!fTimeMachineExcludeInitialized)
    {
        [self updateTimeMachineExclude];
    }
}

- (void)startTransferIgnoringQueue:(BOOL)ignoreQueue
{
    if ([self alertForRemainingDiskSpace])
    {
        ignoreQueue ? tr_torrentStartNow(fHandle) : tr_torrentStart(fHandle);
        [self update];

        //capture, specifically, stop-seeding settings changing to unlimited
        [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptions" object:nil];
    }
}

- (void)startTransferNoQueue
{
    [self startTransferIgnoringQueue:YES];
}

- (void)startTransfer
{
    [self startTransferIgnoringQueue:NO];
}

- (void)stopTransfer
{
    tr_torrentStop(fHandle);
    [self update];
}

- (void)sleep
{
    if ((fResumeOnWake = self.active))
    {
        tr_torrentStop(fHandle);
    }
}

- (void)wakeUp
{
    if (fResumeOnWake)
    {
        tr_logAddNamedInfo(fInfo->name, "restarting because of wakeUp");
        tr_torrentStart(fHandle);
    }
}

- (NSUInteger)queuePosition
{
    return fStat->queuePosition;
}

- (void)setQueuePosition:(NSUInteger)index
{
    tr_torrentSetQueuePosition(fHandle, index);
}

- (void)manualAnnounce
{
    tr_torrentManualUpdate(fHandle);
}

- (BOOL)canManualAnnounce
{
    return tr_torrentCanManualUpdate(fHandle);
}

- (void)resetCache
{
    tr_torrentVerify(fHandle, NULL, NULL);
    [self update];
}

- (BOOL)isMagnet
{
    return !tr_torrentHasMetadata(fHandle);
}

- (NSString*)magnetLink
{
    return @(tr_torrentGetMagnetLink(fHandle));
}

- (CGFloat)ratio
{
    return fStat->ratio;
}

- (tr_ratiolimit)ratioSetting
{
    return tr_torrentGetRatioMode(fHandle);
}

- (void)setRatioSetting:(tr_ratiolimit)setting
{
    tr_torrentSetRatioMode(fHandle, setting);
}

- (CGFloat)ratioLimit
{
    return tr_torrentGetRatioLimit(fHandle);
}

- (void)setRatioLimit:(CGFloat)limit
{
    NSParameterAssert(limit >= 0);

    tr_torrentSetRatioLimit(fHandle, limit);
}

- (CGFloat)progressStopRatio
{
    return fStat->seedRatioPercentDone;
}

- (tr_idlelimit)idleSetting
{
    return tr_torrentGetIdleMode(fHandle);
}

- (void)setIdleSetting:(tr_idlelimit)setting
{
    tr_torrentSetIdleMode(fHandle, setting);
}

- (NSUInteger)idleLimitMinutes
{
    return tr_torrentGetIdleLimit(fHandle);
}

- (void)setIdleLimitMinutes:(NSUInteger)limit
{
    NSParameterAssert(limit > 0);

    tr_torrentSetIdleLimit(fHandle, limit);
}

- (BOOL)usesSpeedLimit:(BOOL)upload
{
    return tr_torrentUsesSpeedLimit(fHandle, upload ? TR_UP : TR_DOWN);
}

- (void)setUseSpeedLimit:(BOOL)use upload:(BOOL)upload
{
    tr_torrentUseSpeedLimit(fHandle, upload ? TR_UP : TR_DOWN, use);
}

- (NSInteger)speedLimit:(BOOL)upload
{
    return tr_torrentGetSpeedLimit_KBps(fHandle, upload ? TR_UP : TR_DOWN);
}

- (void)setSpeedLimit:(NSInteger)limit upload:(BOOL)upload
{
    tr_torrentSetSpeedLimit_KBps(fHandle, upload ? TR_UP : TR_DOWN, limit);
}

- (BOOL)usesGlobalSpeedLimit
{
    return tr_torrentUsesSessionLimits(fHandle);
}

- (void)setUseGlobalSpeedLimit:(BOOL)use
{
    tr_torrentUseSessionLimits(fHandle, use);
}

- (void)setMaxPeerConnect:(uint16_t)count
{
    NSParameterAssert(count > 0);

    tr_torrentSetPeerLimit(fHandle, count);
}

- (uint16_t)maxPeerConnect
{
    return tr_torrentGetPeerLimit(fHandle);
}
- (BOOL)waitingToStart
{
    return fStat->activity == TR_STATUS_DOWNLOAD_WAIT || fStat->activity == TR_STATUS_SEED_WAIT;
}

- (tr_priority_t)priority
{
    return tr_torrentGetPriority(fHandle);
}

- (void)setPriority:(tr_priority_t)priority
{
    return tr_torrentSetPriority(fHandle, priority);
}

+ (BOOL)trashFile:(NSString*)path error:(NSError**)error
{
    // Attempt to move to trash
    if ([NSFileManager.defaultManager trashItemAtURL:[NSURL fileURLWithPath:path] resultingItemURL:nil error:nil])
    {
        NSLog(@"Old moved to Trash %@", path);
        return YES;
    }

    // If cannot trash, just delete it (will work if it's on a remote volume)
    NSError* localError;
    if ([NSFileManager.defaultManager removeItemAtPath:path error:&localError])
    {
        NSLog(@"Old removed %@", path);
        return YES;
    }

    NSLog(@"Old could not be trashed or removed %@: %@", path, localError.localizedDescription);
    if (error != nil)
    {
        *error = localError;
    }

    return NO;
}

- (void)moveTorrentDataFileTo:(NSString*)folder
{
    NSString* oldFolder = self.currentDirectory;
    if ([oldFolder isEqualToString:folder])
    {
        return;
    }

    //check if moving inside itself
    NSArray *oldComponents = oldFolder.pathComponents, *newComponents = folder.pathComponents;
    NSUInteger const oldCount = oldComponents.count;

    if (oldCount < newComponents.count && [newComponents[oldCount] isEqualToString:self.name] && [folder hasPrefix:oldFolder])
    {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = NSLocalizedString(@"A folder cannot be moved to inside itself.", "Move inside itself alert -> title");
        alert.informativeText = [NSString
            stringWithFormat:NSLocalizedString(@"The move operation of \"%@\" cannot be done.", "Move inside itself alert -> message"),
                             self.name];
        [alert addButtonWithTitle:NSLocalizedString(@"OK", "Move inside itself alert -> button")];

        [alert runModal];

        return;
    }

    volatile int status;
    tr_torrentSetLocation(fHandle, folder.UTF8String, YES, NULL, &status);

    while (status == TR_LOC_MOVING) //block while moving (for now)
    {
        [NSThread sleepForTimeInterval:0.05];
    }

    if (status == TR_LOC_DONE)
    {
        [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateStats" object:nil];
    }
    else
    {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = NSLocalizedString(@"There was an error moving the data file.", "Move error alert -> title");
        alert.informativeText = [NSString
            stringWithFormat:NSLocalizedString(@"The move operation of \"%@\" cannot be done.", "Move error alert -> message"), self.name];
        [alert addButtonWithTitle:NSLocalizedString(@"OK", "Move error alert -> button")];

        [alert runModal];
    }

    [self updateTimeMachineExclude];
}

- (void)copyTorrentFileTo:(NSString*)path
{
    [NSFileManager.defaultManager copyItemAtPath:self.torrentLocation toPath:path error:NULL];
}

- (BOOL)alertForRemainingDiskSpace
{
    if (self.allDownloaded || ![fDefaults boolForKey:@"WarningRemainingSpace"])
    {
        return YES;
    }

    NSString* downloadFolder = self.currentDirectory;
    NSDictionary* systemAttributes;
    if ((systemAttributes = [NSFileManager.defaultManager attributesOfFileSystemForPath:downloadFolder error:NULL]))
    {
        uint64_t const remainingSpace = ((NSNumber*)systemAttributes[NSFileSystemFreeSize]).unsignedLongLongValue;

        //if the remaining space is greater than the size left, then there is enough space regardless of preallocation
        if (remainingSpace < self.sizeLeft && remainingSpace < tr_torrentGetBytesLeftToAllocate(fHandle))
        {
            NSString* volumeName = [NSFileManager.defaultManager componentsToDisplayForPath:downloadFolder][0];

            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = [NSString
                stringWithFormat:NSLocalizedString(@"Not enough remaining disk space to download \"%@\" completely.", "Torrent disk space alert -> title"),
                                 self.name];
            alert.informativeText = [NSString stringWithFormat:NSLocalizedString(
                                                                   @"The transfer will be paused."
                                                                    " Clear up space on %@ or deselect files in the torrent inspector to continue.",
                                                                   "Torrent disk space alert -> message"),
                                                               volumeName];
            [alert addButtonWithTitle:NSLocalizedString(@"OK", "Torrent disk space alert -> button")];
            [alert addButtonWithTitle:NSLocalizedString(@"Download Anyway", "Torrent disk space alert -> button")];

            alert.showsSuppressionButton = YES;
            alert.suppressionButton.title = NSLocalizedString(@"Do not check disk space again", "Torrent disk space alert -> button");

            NSInteger const result = [alert runModal];
            if (alert.suppressionButton.state == NSControlStateValueOn)
            {
                [fDefaults setBool:NO forKey:@"WarningRemainingSpace"];
            }

            return result != NSAlertFirstButtonReturn;
        }
    }
    return YES;
}

- (NSImage*)icon
{
    if (self.magnet)
    {
        return [NSImage imageNamed:@"Magnet"];
    }

    if (!fIcon)
    {
        fIcon = self.folder ? [NSImage imageNamed:NSImageNameFolder] : [NSWorkspace.sharedWorkspace iconForFileType:self.name.pathExtension];
    }
    return fIcon;
}

- (NSString*)name
{
    return fInfo->name != NULL ? @(fInfo->name) : fHashString;
}

- (BOOL)isFolder
{
    return fInfo->isFolder;
}

- (uint64_t)size
{
    return fInfo->totalSize;
}

- (uint64_t)sizeLeft
{
    return fStat->leftUntilDone;
}

- (NSMutableArray*)allTrackerStats
{
    int count;
    tr_tracker_stat* stats = tr_torrentTrackers(fHandle, &count);

    NSMutableArray* trackers = [NSMutableArray arrayWithCapacity:(count > 0 ? count + (stats[count - 1].tier + 1) : 0)];

    int prevTier = -1;
    for (int i = 0; i < count; ++i)
    {
        if (stats[i].tier != prevTier)
        {
            [trackers addObject:@{ @"Tier" : @(stats[i].tier + 1), @"Name" : self.name }];
            prevTier = stats[i].tier;
        }

        TrackerNode* tracker = [[TrackerNode alloc] initWithTrackerStat:&stats[i] torrent:self];
        [trackers addObject:tracker];
    }

    tr_torrentTrackersFree(stats, count);
    return trackers;
}

- (NSArray*)allTrackersFlat
{
    NSMutableArray* allTrackers = [NSMutableArray arrayWithCapacity:fInfo->trackerCount];

    for (NSInteger i = 0; i < fInfo->trackerCount; i++)
    {
        [allTrackers addObject:@(fInfo->trackers[i].announce)];
    }

    return allTrackers;
}

- (BOOL)addTrackerToNewTier:(NSString*)tracker
{
    tracker = [tracker stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];

    if ([tracker rangeOfString:@"://"].location == NSNotFound)
    {
        tracker = [@"http://" stringByAppendingString:tracker];
    }

    //recreate the tracker structure
    int const oldTrackerCount = fInfo->trackerCount;
    tr_tracker_info* trackerStructs = tr_new(tr_tracker_info, oldTrackerCount + 1);
    for (int i = 0; i < oldTrackerCount; ++i)
    {
        trackerStructs[i] = fInfo->trackers[i];
    }

    trackerStructs[oldTrackerCount].announce = (char*)tracker.UTF8String;
    trackerStructs[oldTrackerCount].tier = trackerStructs[oldTrackerCount - 1].tier + 1;
    trackerStructs[oldTrackerCount].id = oldTrackerCount;

    BOOL const success = tr_torrentSetAnnounceList(fHandle, trackerStructs, oldTrackerCount + 1);
    tr_free(trackerStructs);

    return success;
}

- (void)removeTrackers:(NSSet*)trackers
{
    //recreate the tracker structure
    tr_tracker_info* trackerStructs = tr_new(tr_tracker_info, fInfo->trackerCount);

    NSUInteger newCount = 0;
    for (NSUInteger i = 0; i < fInfo->trackerCount; i++)
    {
        if (![trackers containsObject:@(fInfo->trackers[i].announce)])
        {
            trackerStructs[newCount++] = fInfo->trackers[i];
        }
    }

    BOOL const success = tr_torrentSetAnnounceList(fHandle, trackerStructs, newCount);
    NSAssert(success, @"Removing tracker addresses failed");

    tr_free(trackerStructs);
}

- (NSString*)comment
{
    return fInfo->comment ? @(fInfo->comment) : @"";
}

- (NSString*)creator
{
    return fInfo->creator ? @(fInfo->creator) : @"";
}

- (NSDate*)dateCreated
{
    NSInteger date = fInfo->dateCreated;
    return date > 0 ? [NSDate dateWithTimeIntervalSince1970:date] : nil;
}

- (NSInteger)pieceSize
{
    return fInfo->pieceSize;
}

- (NSInteger)pieceCount
{
    return fInfo->pieceCount;
}

- (NSString*)hashString
{
    return fHashString;
}

- (BOOL)privateTorrent
{
    return fInfo->isPrivate;
}

- (NSString*)torrentLocation
{
    return fInfo->torrent ? @(fInfo->torrent) : @"";
}

- (NSString*)dataLocation
{
    if (self.magnet)
    {
        return nil;
    }

    if (self.folder)
    {
        NSString* dataLocation = [self.currentDirectory stringByAppendingPathComponent:self.name];

        if (![NSFileManager.defaultManager fileExistsAtPath:dataLocation])
        {
            return nil;
        }

        return dataLocation;
    }
    else
    {
        char* location = tr_torrentFindFile(fHandle, 0);
        if (location == NULL)
        {
            return nil;
        }

        NSString* dataLocation = @(location);
        free(location);

        return dataLocation;
    }
}

- (NSString*)fileLocation:(FileListNode*)node
{
    if (node.isFolder)
    {
        NSString* basePath = [node.path stringByAppendingPathComponent:node.name];
        NSString* dataLocation = [self.currentDirectory stringByAppendingPathComponent:basePath];

        if (![NSFileManager.defaultManager fileExistsAtPath:dataLocation])
        {
            return nil;
        }

        return dataLocation;
    }
    else
    {
        char* location = tr_torrentFindFile(fHandle, node.indexes.firstIndex);
        if (location == NULL)
        {
            return nil;
        }

        NSString* dataLocation = @(location);
        free(location);

        return dataLocation;
    }
}

- (void)renameTorrent:(NSString*)newName completionHandler:(void (^)(BOOL didRename))completionHandler
{
    NSParameterAssert(newName != nil);
    NSParameterAssert(![newName isEqualToString:@""]);

    NSDictionary* contextInfo = @{ @"Torrent" : self, @"CompletionHandler" : [completionHandler copy] };

    tr_torrentRenamePath(fHandle, fInfo->name, newName.UTF8String, renameCallback, (__bridge_retained void*)(contextInfo));
}

- (void)renameFileNode:(FileListNode*)node
              withName:(NSString*)newName
     completionHandler:(void (^)(BOOL didRename))completionHandler
{
    NSParameterAssert(node.torrent == self);
    NSParameterAssert(newName != nil);
    NSParameterAssert(![newName isEqualToString:@""]);

    NSDictionary* contextInfo = @{ @"Torrent" : self, @"Nodes" : @[ node ], @"CompletionHandler" : [completionHandler copy] };

    NSString* oldPath = [node.path stringByAppendingPathComponent:node.name];
    tr_torrentRenamePath(fHandle, oldPath.UTF8String, newName.UTF8String, renameCallback, (__bridge_retained void*)(contextInfo));
}

- (CGFloat)progress
{
    return fStat->percentComplete;
}

- (CGFloat)progressDone
{
    return fStat->percentDone;
}

- (CGFloat)progressLeft
{
    if (self.size == 0) //magnet links
    {
        return 0.0;
    }

    return (CGFloat)self.sizeLeft / self.size;
}

- (CGFloat)checkingProgress
{
    return fStat->recheckProgress;
}

- (CGFloat)availableDesired
{
    return (CGFloat)fStat->desiredAvailable / self.sizeLeft;
}

- (BOOL)isActive
{
    return fStat->activity != TR_STATUS_STOPPED && fStat->activity != TR_STATUS_DOWNLOAD_WAIT && fStat->activity != TR_STATUS_SEED_WAIT;
}

- (BOOL)isSeeding
{
    return fStat->activity == TR_STATUS_SEED;
}

- (BOOL)isChecking
{
    return fStat->activity == TR_STATUS_CHECK || fStat->activity == TR_STATUS_CHECK_WAIT;
}

- (BOOL)isCheckingWaiting
{
    return fStat->activity == TR_STATUS_CHECK_WAIT;
}

- (BOOL)allDownloaded
{
    return self.sizeLeft == 0 && !self.magnet;
}

- (BOOL)isComplete
{
    return self.progress >= 1.0;
}

- (BOOL)isFinishedSeeding
{
    return fStat->finished;
}

- (BOOL)isError
{
    return fStat->error == TR_STAT_LOCAL_ERROR;
}

- (BOOL)isAnyErrorOrWarning
{
    return fStat->error != TR_STAT_OK;
}

- (NSString*)errorMessage
{
    if (!self.anyErrorOrWarning)
    {
        return @"";
    }

    NSString* error;
    if (!(error = @(fStat->errorString)) && !(error = [NSString stringWithCString:fStat->errorString encoding:NSISOLatin1StringEncoding]))
    {
        error = [NSString stringWithFormat:@"(%@)", NSLocalizedString(@"unreadable error", "Torrent -> error string unreadable")];
    }

    //libtransmission uses "Set Location", Mac client uses "Move data file to..." - very hacky!
    error = [error stringByReplacingOccurrencesOfString:@"Set Location" withString:[@"Move Data File To" stringByAppendingEllipsis]];

    return error;
}

- (NSArray*)peers
{
    int totalPeers;
    tr_peer_stat* peers = tr_torrentPeers(fHandle, &totalPeers);

    NSMutableArray* peerDicts = [NSMutableArray arrayWithCapacity:totalPeers];

    for (int i = 0; i < totalPeers; i++)
    {
        tr_peer_stat* peer = &peers[i];
        NSMutableDictionary* dict = [NSMutableDictionary dictionaryWithCapacity:12];

        dict[@"Name"] = self.name;
        dict[@"From"] = @(peer->from);
        dict[@"IP"] = @(peer->addr);
        dict[@"Port"] = @(peer->port);
        dict[@"Progress"] = @(peer->progress);
        dict[@"Seed"] = @(peer->isSeed);
        dict[@"Encryption"] = @(peer->isEncrypted);
        dict[@"uTP"] = @(peer->isUTP);
        dict[@"Client"] = @(peer->client);
        dict[@"Flags"] = @(peer->flagStr);

        if (peer->isUploadingTo)
        {
            dict[@"UL To Rate"] = @(peer->rateToPeer_KBps);
        }
        if (peer->isDownloadingFrom)
        {
            dict[@"DL From Rate"] = @(peer->rateToClient_KBps);
        }

        [peerDicts addObject:dict];
    }

    tr_torrentPeersFree(peers, totalPeers);

    return peerDicts;
}

- (NSUInteger)webSeedCount
{
    return fInfo->webseedCount;
}

- (NSArray*)webSeeds
{
    NSMutableArray* webSeeds = [NSMutableArray arrayWithCapacity:fInfo->webseedCount];

    double* dlSpeeds = tr_torrentWebSpeeds_KBps(fHandle);

    for (NSInteger i = 0; i < fInfo->webseedCount; i++)
    {
        NSMutableDictionary* dict = [NSMutableDictionary dictionaryWithCapacity:3];

        dict[@"Name"] = self.name;
        dict[@"Address"] = @(fInfo->webseeds[i]);

        if (dlSpeeds[i] != -1.0)
        {
            dict[@"DL From Rate"] = @(dlSpeeds[i]);
        }

        [webSeeds addObject:dict];
    }

    tr_free(dlSpeeds);

    return webSeeds;
}

- (NSString*)progressString
{
    if (self.magnet)
    {
        NSString* progressString = fStat->metadataPercentComplete > 0.0 ?
            [NSString stringWithFormat:NSLocalizedString(@"%@ of torrent metadata retrieved", "Torrent -> progress string"),
                                       [NSString percentString:fStat->metadataPercentComplete longDecimals:YES]] :
            NSLocalizedString(@"torrent metadata needed", "Torrent -> progress string");

        return [NSString stringWithFormat:@"%@ - %@", NSLocalizedString(@"Magnetized transfer", "Torrent -> progress string"), progressString];
    }

    NSString* string;

    if (!self.allDownloaded)
    {
        CGFloat progress;
        if (self.folder && [fDefaults boolForKey:@"DisplayStatusProgressSelected"])
        {
            string = [NSString stringForFilePartialSize:self.haveTotal fullSize:self.totalSizeSelected];
            progress = self.progressDone;
        }
        else
        {
            string = [NSString stringForFilePartialSize:self.haveTotal fullSize:self.size];
            progress = self.progress;
        }

        string = [string stringByAppendingFormat:@" (%@)", [NSString percentString:progress longDecimals:YES]];
    }
    else
    {
        NSString* downloadString;
        if (!self.complete) //only multifile possible
        {
            if ([fDefaults boolForKey:@"DisplayStatusProgressSelected"])
            {
                downloadString = [NSString stringWithFormat:NSLocalizedString(@"%@ selected", "Torrent -> progress string"),
                                                            [NSString stringForFileSize:self.haveTotal]];
            }
            else
            {
                downloadString = [NSString stringForFilePartialSize:self.haveTotal fullSize:self.size];
                downloadString = [downloadString stringByAppendingFormat:@" (%@)", [NSString percentString:self.progress longDecimals:YES]];
            }
        }
        else
        {
            downloadString = [NSString stringForFileSize:self.size];
        }

        NSString* uploadString = [NSString stringWithFormat:NSLocalizedString(@"uploaded %@ (Ratio: %@)", "Torrent -> progress string"),
                                                            [NSString stringForFileSize:self.uploadedTotal],
                                                            [NSString stringForRatio:self.ratio]];

        string = [downloadString stringByAppendingFormat:@", %@", uploadString];
    }

    //add time when downloading or seed limit set
    if (self.shouldShowEta)
    {
        string = [string stringByAppendingFormat:@" - %@", self.etaString];
    }

    return string;
}

- (NSString*)statusString
{
    NSString* string;

    if (self.anyErrorOrWarning)
    {
        switch (fStat->error)
        {
        case TR_STAT_LOCAL_ERROR:
            string = NSLocalizedString(@"Error", "Torrent -> status string");
            break;
        case TR_STAT_TRACKER_ERROR:
            string = NSLocalizedString(@"Tracker returned error", "Torrent -> status string");
            break;
        case TR_STAT_TRACKER_WARNING:
            string = NSLocalizedString(@"Tracker returned warning", "Torrent -> status string");
            break;
        default:
            NSAssert(NO, @"unknown error state");
        }

        NSString* errorString = self.errorMessage;
        if (errorString && ![errorString isEqualToString:@""])
        {
            string = [string stringByAppendingFormat:@": %@", errorString];
        }
    }
    else
    {
        switch (fStat->activity)
        {
        case TR_STATUS_STOPPED:
            if (self.finishedSeeding)
            {
                string = NSLocalizedString(@"Seeding complete", "Torrent -> status string");
            }
            else
            {
                string = NSLocalizedString(@"Paused", "Torrent -> status string");
            }
            break;

        case TR_STATUS_DOWNLOAD_WAIT:
            string = [NSLocalizedString(@"Waiting to download", "Torrent -> status string") stringByAppendingEllipsis];
            break;

        case TR_STATUS_SEED_WAIT:
            string = [NSLocalizedString(@"Waiting to seed", "Torrent -> status string") stringByAppendingEllipsis];
            break;

        case TR_STATUS_CHECK_WAIT:
            string = [NSLocalizedString(@"Waiting to check existing data", "Torrent -> status string") stringByAppendingEllipsis];
            break;

        case TR_STATUS_CHECK:
            string = [NSString stringWithFormat:@"%@ (%@)",
                                                NSLocalizedString(@"Checking existing data", "Torrent -> status string"),
                                                [NSString percentString:self.checkingProgress longDecimals:YES]];
            break;

        case TR_STATUS_DOWNLOAD:
            if (self.totalPeersConnected != 1)
            {
                string = [NSString stringWithFormat:NSLocalizedString(@"Downloading from %d of %d peers", "Torrent -> status string"),
                                                    self.peersSendingToUs,
                                                    self.totalPeersConnected];
            }
            else
            {
                string = [NSString stringWithFormat:NSLocalizedString(@"Downloading from %d of 1 peer", "Torrent -> status string"),
                                                    self.peersSendingToUs];
            }

            if (NSInteger const webSeedCount = fStat->webseedsSendingToUs; webSeedCount > 0)
            {
                NSString* webSeedString;
                if (webSeedCount == 1)
                {
                    webSeedString = NSLocalizedString(@"web seed", "Torrent -> status string");
                }
                else
                {
                    webSeedString = [NSString stringWithFormat:NSLocalizedString(@"%d web seeds", "Torrent -> status string"), webSeedCount];
                }

                string = [string stringByAppendingFormat:@" + %@", webSeedString];
            }

            break;

        case TR_STATUS_SEED:
            if (self.totalPeersConnected != 1)
            {
                string = [NSString stringWithFormat:NSLocalizedString(@"Seeding to %d of %d peers", "Torrent -> status string"),
                                                    self.peersGettingFromUs,
                                                    self.totalPeersConnected];
            }
            else
            {
                string = [NSString stringWithFormat:NSLocalizedString(@"Seeding to %d of 1 peer", "Torrent -> status string"),
                                                    self.peersGettingFromUs];
            }
        }

        if (self.stalled)
        {
            string = [NSLocalizedString(@"Stalled", "Torrent -> status string") stringByAppendingFormat:@", %@", string];
        }
    }

    //append even if error
    if (self.active && !self.checking)
    {
        if (fStat->activity == TR_STATUS_DOWNLOAD)
        {
            string = [string stringByAppendingFormat:@" - %@: %@, %@: %@",
                                                     NSLocalizedString(@"DL", "Torrent -> status string"),
                                                     [NSString stringForSpeed:self.downloadRate],
                                                     NSLocalizedString(@"UL", "Torrent -> status string"),
                                                     [NSString stringForSpeed:self.uploadRate]];
        }
        else
        {
            string = [string stringByAppendingFormat:@" - %@: %@",
                                                     NSLocalizedString(@"UL", "Torrent -> status string"),
                                                     [NSString stringForSpeed:self.uploadRate]];
        }
    }

    return string;
}

- (NSString*)shortStatusString
{
    NSString* string;

    switch (fStat->activity)
    {
    case TR_STATUS_STOPPED:
        if (self.finishedSeeding)
        {
            string = NSLocalizedString(@"Seeding complete", "Torrent -> status string");
        }
        else
        {
            string = NSLocalizedString(@"Paused", "Torrent -> status string");
        }
        break;

    case TR_STATUS_DOWNLOAD_WAIT:
        string = [NSLocalizedString(@"Waiting to download", "Torrent -> status string") stringByAppendingEllipsis];
        break;

    case TR_STATUS_SEED_WAIT:
        string = [NSLocalizedString(@"Waiting to seed", "Torrent -> status string") stringByAppendingEllipsis];
        break;

    case TR_STATUS_CHECK_WAIT:
        string = [NSLocalizedString(@"Waiting to check existing data", "Torrent -> status string") stringByAppendingEllipsis];
        break;

    case TR_STATUS_CHECK:
        string = [NSString stringWithFormat:@"%@ (%@)",
                                            NSLocalizedString(@"Checking existing data", "Torrent -> status string"),
                                            [NSString percentString:self.checkingProgress longDecimals:YES]];
        break;

    case TR_STATUS_DOWNLOAD:
        string = [NSString stringWithFormat:@"%@: %@, %@: %@",
                                            NSLocalizedString(@"DL", "Torrent -> status string"),
                                            [NSString stringForSpeed:self.downloadRate],
                                            NSLocalizedString(@"UL", "Torrent -> status string"),
                                            [NSString stringForSpeed:self.uploadRate]];
        break;

    case TR_STATUS_SEED:
        string = [NSString stringWithFormat:@"%@: %@, %@: %@",
                                            NSLocalizedString(@"Ratio", "Torrent -> status string"),
                                            [NSString stringForRatio:self.ratio],
                                            NSLocalizedString(@"UL", "Torrent -> status string"),
                                            [NSString stringForSpeed:self.uploadRate]];
    }

    return string;
}

- (NSString*)remainingTimeString
{
    if (self.shouldShowEta)
    {
        return self.etaString;
    }
    else
    {
        return self.shortStatusString;
    }
}

- (NSString*)stateString
{
    switch (fStat->activity)
    {
    case TR_STATUS_STOPPED:
    case TR_STATUS_DOWNLOAD_WAIT:
    case TR_STATUS_SEED_WAIT:
        {
            NSString* string = NSLocalizedString(@"Paused", "Torrent -> status string");

            NSString* extra = nil;
            if (self.waitingToStart)
            {
                extra = fStat->activity == TR_STATUS_DOWNLOAD_WAIT ?
                    NSLocalizedString(@"Waiting to download", "Torrent -> status string") :
                    NSLocalizedString(@"Waiting to seed", "Torrent -> status string");
            }
            else if (self.finishedSeeding)
            {
                extra = NSLocalizedString(@"Seeding complete", "Torrent -> status string");
            }

            return extra ? [string stringByAppendingFormat:@" (%@)", extra] : string;
        }

    case TR_STATUS_CHECK_WAIT:
        return [NSLocalizedString(@"Waiting to check existing data", "Torrent -> status string") stringByAppendingEllipsis];

    case TR_STATUS_CHECK:
        return [NSString stringWithFormat:@"%@ (%@)",
                                          NSLocalizedString(@"Checking existing data", "Torrent -> status string"),
                                          [NSString percentString:self.checkingProgress longDecimals:YES]];

    case TR_STATUS_DOWNLOAD:
        return NSLocalizedString(@"Downloading", "Torrent -> status string");

    case TR_STATUS_SEED:
        return NSLocalizedString(@"Seeding", "Torrent -> status string");
    }
}

- (NSInteger)totalPeersConnected
{
    return fStat->peersConnected;
}

- (NSInteger)totalPeersTracker
{
    return fStat->peersFrom[TR_PEER_FROM_TRACKER];
}

- (NSInteger)totalPeersIncoming
{
    return fStat->peersFrom[TR_PEER_FROM_INCOMING];
}

- (NSInteger)totalPeersCache
{
    return fStat->peersFrom[TR_PEER_FROM_RESUME];
}

- (NSInteger)totalPeersPex
{
    return fStat->peersFrom[TR_PEER_FROM_PEX];
}

- (NSInteger)totalPeersDHT
{
    return fStat->peersFrom[TR_PEER_FROM_DHT];
}

- (NSInteger)totalPeersLocal
{
    return fStat->peersFrom[TR_PEER_FROM_LPD];
}

- (NSInteger)totalPeersLTEP
{
    return fStat->peersFrom[TR_PEER_FROM_LTEP];
}

- (NSInteger)peersSendingToUs
{
    return fStat->peersSendingToUs;
}

- (NSInteger)peersGettingFromUs
{
    return fStat->peersGettingFromUs;
}

- (CGFloat)downloadRate
{
    return fStat->pieceDownloadSpeed_KBps;
}

- (CGFloat)uploadRate
{
    return fStat->pieceUploadSpeed_KBps;
}

- (CGFloat)totalRate
{
    return self.downloadRate + self.uploadRate;
}

- (uint64_t)haveVerified
{
    return fStat->haveValid;
}

- (uint64_t)haveTotal
{
    return self.haveVerified + fStat->haveUnchecked;
}

- (uint64_t)totalSizeSelected
{
    return fStat->sizeWhenDone;
}

- (uint64_t)downloadedTotal
{
    return fStat->downloadedEver;
}

- (uint64_t)uploadedTotal
{
    return fStat->uploadedEver;
}

- (uint64_t)failedHash
{
    return fStat->corruptEver;
}

- (NSInteger)groupValue
{
    return fGroupValue;
}

- (void)setGroupValue:(NSInteger)groupValue determinationType:(TorrentDeterminationType)determinationType
{
    if (groupValue != fGroupValue)
    {
        fGroupValue = groupValue;
        [NSNotificationCenter.defaultCenter postNotificationName:kTorrentDidChangeGroupNotification object:self];
    }
    fGroupValueDetermination = determinationType;
}

- (NSInteger)groupOrderValue
{
    return [GroupsController.groups rowValueForIndex:fGroupValue];
}

- (void)checkGroupValueForRemoval:(NSNotification*)notification
{
    if (fGroupValue != -1 && [notification.userInfo[@"Index"] integerValue] == fGroupValue)
    {
        fGroupValue = -1;
    }
}

- (NSArray*)fileList
{
    return fFileList;
}

- (NSArray*)flatFileList
{
    return fFlatFileList;
}

- (NSInteger)fileCount
{
    return fInfo->fileCount;
}

- (CGFloat)fileProgress:(FileListNode*)node
{
    if (self.fileCount == 1 || self.complete)
    {
        return self.progress;
    }

    // #5501
    if (node.size == 0)
    {
        return 1.0;
    }

    uint64_t have = 0;
    NSIndexSet* indexSet = node.indexes;
    for (NSInteger index = indexSet.firstIndex; index != NSNotFound; index = [indexSet indexGreaterThanIndex:index])
    {
        have += tr_torrentFile(fHandle, index).have;
    }

    return (CGFloat)have / node.size;
}

- (BOOL)canChangeDownloadCheckForFile:(NSUInteger)index
{
    NSAssert2((NSInteger)index < self.fileCount, @"Index %ld is greater than file count %ld", index, self.fileCount);

    return [self canChangeDownloadCheckForFiles:[NSIndexSet indexSetWithIndex:index]];
}

- (BOOL)canChangeDownloadCheckForFiles:(NSIndexSet*)indexSet
{
    if (self.fileCount == 1 || self.complete)
    {
        return NO;
    }

    __block BOOL canChange = NO;
    [indexSet enumerateIndexesWithOptions:NSEnumerationConcurrent usingBlock:^(NSUInteger index, BOOL* stop) {
        auto const file = tr_torrentFile(fHandle, index);
        if (file.have < file.length)
        {
            canChange = YES;
            *stop = YES;
        }
    }];
    return canChange;
}

- (NSInteger)checkForFiles:(NSIndexSet*)indexSet
{
    BOOL onState = NO, offState = NO;
    for (NSUInteger index = indexSet.firstIndex; index != NSNotFound; index = [indexSet indexGreaterThanIndex:index])
    {
        auto const file = tr_torrentFile(fHandle, index);
        if (file.wanted || ![self canChangeDownloadCheckForFile:index])
        {
            onState = YES;
        }
        else
        {
            offState = YES;
        }

        if (onState && offState)
        {
            return NSControlStateValueMixed;
        }
    }
    return onState ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)setFileCheckState:(NSInteger)state forIndexes:(NSIndexSet*)indexSet
{
    NSUInteger count = indexSet.count;
    tr_file_index_t* files = static_cast<tr_file_index_t*>(malloc(count * sizeof(tr_file_index_t)));
    for (NSUInteger index = indexSet.firstIndex, i = 0; index != NSNotFound; index = [indexSet indexGreaterThanIndex:index], i++)
    {
        files[i] = index;
    }

    tr_torrentSetFileDLs(fHandle, files, count, state != NSControlStateValueOff);
    free(files);

    [self update];
    [NSNotificationCenter.defaultCenter postNotificationName:@"TorrentFileCheckChange" object:self];
}

- (void)setFilePriority:(tr_priority_t)priority forIndexes:(NSIndexSet*)indexSet
{
    NSUInteger const count = indexSet.count;
    tr_file_index_t* files = static_cast<tr_file_index_t*>(tr_malloc(count * sizeof(tr_file_index_t)));
    for (NSUInteger index = indexSet.firstIndex, i = 0; index != NSNotFound; index = [indexSet indexGreaterThanIndex:index], i++)
    {
        files[i] = index;
    }

    tr_torrentSetFilePriorities(fHandle, files, count, priority);
    tr_free(files);
}

- (BOOL)hasFilePriority:(tr_priority_t)priority forIndexes:(NSIndexSet*)indexSet
{
    for (NSUInteger index = indexSet.firstIndex; index != NSNotFound; index = [indexSet indexGreaterThanIndex:index])
    {
        if (priority == tr_torrentFile(fHandle, index).priority && [self canChangeDownloadCheckForFile:index])
        {
            return YES;
        }
    }
    return NO;
}

- (NSSet*)filePrioritiesForIndexes:(NSIndexSet*)indexSet
{
    BOOL low = NO, normal = NO, high = NO;
    NSMutableSet* priorities = [NSMutableSet setWithCapacity:MIN(indexSet.count, 3u)];

    for (NSUInteger index = indexSet.firstIndex; index != NSNotFound; index = [indexSet indexGreaterThanIndex:index])
    {
        if (![self canChangeDownloadCheckForFile:index])
        {
            continue;
        }

        auto const priority = tr_torrentFile(fHandle, index).priority;
        switch (priority)
        {
        case TR_PRI_LOW:
            if (low)
            {
                continue;
            }
            low = YES;
            break;
        case TR_PRI_NORMAL:
            if (normal)
            {
                continue;
            }
            normal = YES;
            break;
        case TR_PRI_HIGH:
            if (high)
            {
                continue;
            }
            high = YES;
            break;
        default:
            NSAssert2(NO, @"Unknown priority %d for file index %ld", priority, index);
        }

        [priorities addObject:@(priority)];
        if (low && normal && high)
        {
            break;
        }
    }
    return priorities;
}

- (NSDate*)dateAdded
{
    time_t const date = fStat->addedDate;
    return [NSDate dateWithTimeIntervalSince1970:date];
}

- (NSDate*)dateCompleted
{
    time_t const date = fStat->doneDate;
    return date != 0 ? [NSDate dateWithTimeIntervalSince1970:date] : nil;
}

- (NSDate*)dateActivity
{
    time_t const date = fStat->activityDate;
    return date != 0 ? [NSDate dateWithTimeIntervalSince1970:date] : nil;
}

- (NSDate*)dateActivityOrAdd
{
    NSDate* date = self.dateActivity;
    return date ? date : self.dateAdded;
}

- (NSInteger)secondsDownloading
{
    return fStat->secondsDownloading;
}

- (NSInteger)secondsSeeding
{
    return fStat->secondsSeeding;
}

- (NSInteger)stalledMinutes
{
    if (fStat->idleSecs == -1)
    {
        return -1;
    }

    return fStat->idleSecs / 60;
}

- (BOOL)isStalled
{
    return fStat->isStalled;
}

- (void)updateTimeMachineExclude
{
    [self setTimeMachineExclude:!self.allDownloaded];
}

- (NSInteger)stateSortKey
{
    if (!self.active) //paused
    {
        if (self.waitingToStart)
        {
            return 1;
        }
        else
        {
            return 0;
        }
    }
    else if (self.seeding) //seeding
    {
        return 10;
    }
    else //downloading
    {
        return 20;
    }
}

- (NSString*)trackerSortKey
{
    int count;
    tr_tracker_stat* stats = tr_torrentTrackers(fHandle, &count);

    NSString* best = nil;

    for (int i = 0; i < count; ++i)
    {
        NSString* tracker = @(stats[i].host);
        if (!best || [tracker localizedCaseInsensitiveCompare:best] == NSOrderedAscending)
        {
            best = tracker;
        }
    }

    tr_torrentTrackersFree(stats, count);
    return best;
}

- (tr_torrent*)torrentStruct
{
    return fHandle;
}

- (NSURL*)previewItemURL
{
    NSString* location = self.dataLocation;
    return location ? [NSURL fileURLWithPath:location] : nil;
}

@end

@implementation Torrent (Private)

- (instancetype)initWithPath:(NSString*)path
                        hash:(NSString*)hashString
               torrentStruct:(tr_torrent*)torrentStruct
               magnetAddress:(NSString*)magnetAddress
                         lib:(tr_session*)lib
                  groupValue:(NSNumber*)groupValue
     removeWhenFinishSeeding:(NSNumber*)removeWhenFinishSeeding
              downloadFolder:(NSString*)downloadFolder
      legacyIncompleteFolder:(NSString*)incompleteFolder
{
    if (!(self = [super init]))
    {
        return nil;
    }

    fDefaults = NSUserDefaults.standardUserDefaults;

    if (torrentStruct)
    {
        fHandle = torrentStruct;
    }
    else
    {
        //set libtransmission settings for initialization
        tr_ctor* ctor = tr_ctorNew(lib);

        tr_ctorSetPaused(ctor, TR_FORCE, YES);
        if (downloadFolder)
        {
            tr_ctorSetDownloadDir(ctor, TR_FORCE, downloadFolder.UTF8String);
        }
        if (incompleteFolder)
        {
            tr_ctorSetIncompleteDir(ctor, incompleteFolder.UTF8String);
        }

        tr_parse_result result = TR_PARSE_ERR;
        if (path)
        {
            result = static_cast<tr_parse_result>(tr_ctorSetMetainfoFromFile(ctor, path.UTF8String));
        }

        if (result != TR_PARSE_OK && magnetAddress)
        {
            result = static_cast<tr_parse_result>(tr_ctorSetMetainfoFromMagnetLink(ctor, magnetAddress.UTF8String));
        }

        if (result == TR_PARSE_OK)
        {
            fHandle = tr_torrentNew(ctor, NULL, NULL);
        }

        tr_ctorFree(ctor);

        if (!fHandle)
        {
            return nil;
        }
    }

    fInfo = tr_torrentInfo(fHandle);

    tr_torrentSetQueueStartCallback(fHandle, startQueueCallback, (__bridge void*)(self));
    tr_torrentSetCompletenessCallback(fHandle, completenessChangeCallback, (__bridge void*)(self));
    tr_torrentSetRatioLimitHitCallback(fHandle, ratioLimitHitCallback, (__bridge void*)(self));
    tr_torrentSetIdleLimitHitCallback(fHandle, idleLimitHitCallback, (__bridge void*)(self));
    tr_torrentSetMetadataCallback(fHandle, metadataCallback, (__bridge void*)(self));

    fHashString = @(fInfo->hashString);

    fResumeOnWake = NO;

    //don't do after this point - it messes with auto-group functionality
    if (!self.magnet)
    {
        [self createFileList];
    }

    fDownloadFolderDetermination = TorrentDeterminationAutomatic;

    if (groupValue)
    {
        fGroupValueDetermination = TorrentDeterminationUserSpecified;
        fGroupValue = groupValue.intValue;
    }
    else
    {
        fGroupValueDetermination = TorrentDeterminationAutomatic;
        fGroupValue = [GroupsController.groups groupIndexForTorrent:self];
    }

    _removeWhenFinishSeeding = removeWhenFinishSeeding ? removeWhenFinishSeeding.boolValue :
                                                         [fDefaults boolForKey:@"RemoveWhenFinishSeeding"];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(checkGroupValueForRemoval:)
                                               name:@"GroupValueRemoved"
                                             object:nil];

    fTimeMachineExcludeInitialized = NO;
    [self update];

    return self;
}

- (void)createFileList
{
    NSAssert(!self.magnet, @"Cannot create a file list until the torrent is demagnetized");

    if (self.folder)
    {
        NSInteger const count = self.fileCount;
        NSMutableArray* flatFileList = [NSMutableArray arrayWithCapacity:count];

        FileListNode* tempNode = nil;

        for (NSInteger i = 0; i < count; i++)
        {
            auto const file = tr_torrentFile(fHandle, i);

            NSString* fullPath = @(file.name);
            NSArray* pathComponents = fullPath.pathComponents;

            if (!tempNode)
            {
                tempNode = [[FileListNode alloc] initWithFolderName:pathComponents[0] path:@"" torrent:self];
            }

            [self insertPathForComponents:pathComponents
                       withComponentIndex:1
                                forParent:tempNode
                                 fileSize:file.length
                                    index:i
                                 flatList:flatFileList];
        }

        [self sortFileList:tempNode.children];
        [self sortFileList:flatFileList];

        fFileList = [[NSArray alloc] initWithArray:tempNode.children];
        fFlatFileList = [[NSArray alloc] initWithArray:flatFileList];
    }
    else
    {
        FileListNode* node = [[FileListNode alloc] initWithFileName:self.name path:@"" size:self.size index:0 torrent:self];
        fFileList = @[ node ];
        fFlatFileList = fFileList;
    }
}

- (void)insertPathForComponents:(NSArray*)components
             withComponentIndex:(NSUInteger)componentIndex
                      forParent:(FileListNode*)parent
                       fileSize:(uint64_t)size
                          index:(NSInteger)index
                       flatList:(NSMutableArray*)flatFileList
{
    NSParameterAssert(components.count > 0);
    NSParameterAssert(componentIndex < components.count);

    NSString* name = components[componentIndex];
    BOOL const isFolder = componentIndex < (components.count - 1);

    //determine if folder node already exists
    __block FileListNode* node = nil;
    if (isFolder)
    {
        [parent.children enumerateObjectsWithOptions:NSEnumerationConcurrent
                                          usingBlock:^(FileListNode* searchNode, NSUInteger idx, BOOL* stop) {
                                              if ([searchNode.name isEqualToString:name] && searchNode.isFolder)
                                              {
                                                  node = searchNode;
                                                  *stop = YES;
                                              }
                                          }];
    }

    //create new folder or file if it doesn't already exist
    if (!node)
    {
        NSString* path = [parent.path stringByAppendingPathComponent:parent.name];
        if (isFolder)
        {
            node = [[FileListNode alloc] initWithFolderName:name path:path torrent:self];
        }
        else
        {
            node = [[FileListNode alloc] initWithFileName:name path:path size:size index:index torrent:self];
            [flatFileList addObject:node];
        }

        [parent insertChild:node];
    }

    if (isFolder)
    {
        [node insertIndex:index withSize:size];

        [self insertPathForComponents:components withComponentIndex:(componentIndex + 1) forParent:node fileSize:size
                                index:index
                             flatList:flatFileList];
    }
}

- (void)sortFileList:(NSMutableArray*)fileNodes
{
    NSSortDescriptor* descriptor = [NSSortDescriptor sortDescriptorWithKey:@"name" ascending:YES
                                                                  selector:@selector(localizedStandardCompare:)];
    [fileNodes sortUsingDescriptors:@[ descriptor ]];

    [fileNodes enumerateObjectsWithOptions:NSEnumerationConcurrent usingBlock:^(FileListNode* node, NSUInteger idx, BOOL* stop) {
        if (node.isFolder)
            [self sortFileList:node.children];
    }];
}

- (void)startQueue
{
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateQueue" object:self];
}

- (void)completenessChange:(tr_completeness)status wasRunning:(BOOL)wasRunning
{
    fStat = tr_torrentStat(fHandle); //don't call update yet to avoid auto-stop

    switch (status)
    {
    case TR_SEED:
    case TR_PARTIAL_SEED:
        {
            NSDictionary* statusInfo = @{@"Status" : @(status), @"WasRunning" : @(wasRunning)};
            [NSNotificationCenter.defaultCenter postNotificationName:@"TorrentFinishedDownloading" object:self userInfo:statusInfo];

            //quarantine the finished data
            NSString* dataLocation = [self.currentDirectory stringByAppendingPathComponent:self.name];
            NSURL* dataLocationUrl = [NSURL fileURLWithPath:dataLocation];
            NSDictionary* quarantineProperties = @{
                (NSString*)kLSQuarantineTypeKey : (NSString*)kLSQuarantineTypeOtherDownload
            };
            NSError* error = nil;
            if (![dataLocationUrl setResourceValue:quarantineProperties forKey:NSURLQuarantinePropertiesKey error:&error])
            {
                NSLog(@"Failed to quarantine %@: %@", dataLocation, error.description);
            }
            break;
        }
    case TR_LEECH:
        [NSNotificationCenter.defaultCenter postNotificationName:@"TorrentRestartedDownloading" object:self];
        break;
    }

    [self update];
    [self updateTimeMachineExclude];
}

- (void)ratioLimitHit
{
    fStat = tr_torrentStat(fHandle);

    [NSNotificationCenter.defaultCenter postNotificationName:@"TorrentFinishedSeeding" object:self];
}

- (void)idleLimitHit
{
    fStat = tr_torrentStat(fHandle);

    [NSNotificationCenter.defaultCenter postNotificationName:@"TorrentFinishedSeeding" object:self];
}

- (void)metadataRetrieved
{
    fStat = tr_torrentStat(fHandle);

    [self createFileList];

    /* If the torrent is in no group, or the group was automatically determined based on criteria evaluated
     * before we had metadata for this torrent, redetermine the group
     */
    if ((fGroupValueDetermination == TorrentDeterminationAutomatic) || (self.groupValue == -1))
    {
        [self setGroupValue:[GroupsController.groups groupIndexForTorrent:self] determinationType:TorrentDeterminationAutomatic];
    }

    //change the location if the group calls for it and it's either not already set or was set automatically before
    if (((fDownloadFolderDetermination == TorrentDeterminationAutomatic) || !tr_torrentGetCurrentDir(fHandle)) &&
        [GroupsController.groups usesCustomDownloadLocationForIndex:self.groupValue])
    {
        NSString* location = [GroupsController.groups customDownloadLocationForIndex:self.groupValue];
        [self changeDownloadFolderBeforeUsing:location determinationType:TorrentDeterminationAutomatic];
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"ResetInspector" object:self userInfo:@{ @"Torrent" : self }];
}

- (void)renameFinished:(BOOL)success
                 nodes:(NSArray*)nodes
     completionHandler:(void (^)(BOOL))completionHandler
               oldPath:(NSString*)oldPath
               newName:(NSString*)newName
{
    NSParameterAssert(completionHandler != nil);
    NSParameterAssert(oldPath != nil);
    NSParameterAssert(newName != nil);

    NSString* path = oldPath.stringByDeletingLastPathComponent;

    if (success)
    {
        using WeakUpdateNodeAndChildrenForRename = void (^__block __weak)(FileListNode*);
        using UpdateNodeAndChildrenForRename = void (^)(FileListNode*);

        NSString* oldName = oldPath.lastPathComponent;
        WeakUpdateNodeAndChildrenForRename weakUpdateNodeAndChildrenForRename;
        UpdateNodeAndChildrenForRename updateNodeAndChildrenForRename;
        weakUpdateNodeAndChildrenForRename = updateNodeAndChildrenForRename = ^(FileListNode* node) {
            [node updateFromOldName:oldName toNewName:newName inPath:path];

            if (node.isFolder)
            {
                [node.children enumerateObjectsWithOptions:NSEnumerationConcurrent
                                                usingBlock:^(FileListNode* childNode, NSUInteger idx, BOOL* stop) {
                                                    weakUpdateNodeAndChildrenForRename(childNode);
                                                }];
            }
        };

        if (!nodes)
        {
            nodes = fFlatFileList;
        }
        [nodes enumerateObjectsWithOptions:NSEnumerationConcurrent usingBlock:^(FileListNode* node, NSUInteger idx, BOOL* stop) {
            updateNodeAndChildrenForRename(node);
        }];

        //resort lists
        NSMutableArray* fileList = [fFileList mutableCopy];
        [self sortFileList:fileList];
        fFileList = fileList;

        NSMutableArray* flatFileList = [fFlatFileList mutableCopy];
        [self sortFileList:flatFileList];
        fFlatFileList = flatFileList;

        fIcon = nil;
    }
    else
    {
        NSLog(@"Error renaming %@ to %@", oldPath, [path stringByAppendingPathComponent:newName]);
    }

    completionHandler(success);
}

- (BOOL)shouldShowEta
{
    if (fStat->activity == TR_STATUS_DOWNLOAD)
    {
        return YES;
    }
    else if (self.seeding)
    {
        //ratio: show if it's set at all
        if (tr_torrentGetSeedRatio(fHandle, NULL))
        {
            return YES;
        }

        //idle: show only if remaining time is less than cap
        if (fStat->etaIdle != TR_ETA_NOT_AVAIL && fStat->etaIdle < ETA_IDLE_DISPLAY_SEC)
        {
            return YES;
        }
    }

    return NO;
}

- (NSString*)etaString
{
    NSInteger eta;
    BOOL fromIdle;
    //don't check for both, since if there's a regular ETA, the torrent isn't idle so it's meaningless
    if (fStat->eta != TR_ETA_NOT_AVAIL && fStat->eta != TR_ETA_UNKNOWN)
    {
        eta = fStat->eta;
        fromIdle = NO;
    }
    else if (fStat->etaIdle != TR_ETA_NOT_AVAIL && fStat->etaIdle < ETA_IDLE_DISPLAY_SEC)
    {
        eta = fStat->etaIdle;
        fromIdle = YES;
    }
    else
    {
        return NSLocalizedString(@"remaining time unknown", "Torrent -> eta string");
    }

    static NSDateComponentsFormatter* formatter;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        formatter = [NSDateComponentsFormatter new];
        formatter.unitsStyle = NSDateComponentsFormatterUnitsStyleShort;
        formatter.maximumUnitCount = 2;
        formatter.collapsesLargestUnit = YES;
        formatter.includesTimeRemainingPhrase = YES;
    });
    NSString* idleString = [formatter stringFromTimeInterval:eta];

    if (fromIdle)
    {
        idleString = [idleString stringByAppendingFormat:@" (%@)", NSLocalizedString(@"inactive", "Torrent -> eta string")];
    }

    return idleString;
}

- (void)setTimeMachineExclude:(BOOL)exclude
{
    NSString* path;
    if ((path = self.dataLocation))
    {
        CSBackupSetItemExcluded((__bridge CFURLRef)[NSURL fileURLWithPath:path], exclude, false);
        fTimeMachineExcludeInitialized = YES;
    }
}

@end
