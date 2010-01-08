/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2010 Transmission authors and contributors
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

#import "Torrent.h"
#import "GroupsController.h"
#import "FileListNode.h"
#import "NSStringAdditions.h"
#import "TrackerNode.h"
#import "utils.h" //tr_httpIsValidURL

@interface Torrent (Private)

- (id) initWithPath: (NSString *) path hash: (NSString *) hashString torrentStruct: (tr_torrent *) torrentStruct
        magnetAddress: (NSString *) magnetAddress lib: (tr_session *) lib
        waitToStart: (NSNumber *) waitToStart
        groupValue: (NSNumber *) groupValue
        downloadFolder: (NSString *) downloadFolder legacyIncompleteFolder: (NSString *) incompleteFolder;

- (void) createFileList;
- (void) insertPath: (NSMutableArray *) components forParent: (FileListNode *) parent fileSize: (uint64_t) size
    index: (NSInteger) index flatList: (NSMutableArray *) flatFileList;

- (void) completenessChange: (NSNumber *) status;
- (void) ratioLimitHit;
- (void) metadataRetrieved;

- (NSString *) etaString;

- (void) setTimeMachineExclude: (BOOL) exclude forPath: (NSString *) path;

@end

void completenessChangeCallback(tr_torrent * torrent, tr_completeness status, void * torrentData)
{
    [(Torrent *)torrentData performSelectorOnMainThread: @selector(completenessChange:)
        withObject: [[NSNumber alloc] initWithInt: status] waitUntilDone: NO];
}

void ratioLimitHitCallback(tr_torrent * torrent, void * torrentData)
{
    [(Torrent *)torrentData performSelectorOnMainThread: @selector(ratioLimitHit) withObject: nil waitUntilDone: NO];
}

void metadataCallback(tr_torrent * torrent, void * torrentData)
{
    [(Torrent *)torrentData performSelectorOnMainThread: @selector(metadataRetrieved) withObject: nil waitUntilDone: NO];
}

int trashDataFile(const char * filename)
{
    if (filename != NULL)
        [Torrent trashFile: [NSString stringWithUTF8String: filename]];
    return 0;
}

@implementation Torrent

- (id) initWithPath: (NSString *) path location: (NSString *) location deleteTorrentFile: (BOOL) torrentDelete
        lib: (tr_session *) lib
{
    self = [self initWithPath: path hash: nil torrentStruct: NULL magnetAddress: nil lib: lib
            waitToStart: nil groupValue: nil
            downloadFolder: location legacyIncompleteFolder: nil];
    
    if (self)
    {
        if (torrentDelete && ![[self torrentLocation] isEqualToString: path])
            [Torrent trashFile: path];
    }
    return self;
}

- (id) initWithTorrentStruct: (tr_torrent *) torrentStruct location: (NSString *) location lib: (tr_session *) lib
{
    self = [self initWithPath: nil hash: nil torrentStruct: torrentStruct magnetAddress: nil lib: lib
            waitToStart: nil groupValue: nil
            downloadFolder: location legacyIncompleteFolder: nil];
    
    return self;
}

#warning need location (and use it)?
- (id) initWithMagnetAddress: (NSString *) address location: (NSString *) location lib: (tr_session *) lib
{
    self = [self initWithPath: nil hash: nil torrentStruct: nil magnetAddress: address
            lib: lib waitToStart: nil groupValue: nil
            downloadFolder: location legacyIncompleteFolder: nil];
    
    return self;
}

- (id) initWithHistory: (NSDictionary *) history lib: (tr_session *) lib forcePause: (BOOL) pause
{
    self = [self initWithPath: [history objectForKey: @"InternalTorrentPath"]
                hash: [history objectForKey: @"TorrentHash"]
                torrentStruct: NULL
                magnetAddress: nil
                lib: lib
                waitToStart: [history objectForKey: @"WaitToStart"]
                groupValue: [history objectForKey: @"GroupValue"]
                downloadFolder: [history objectForKey: @"DownloadFolder"] //upgrading from versions < 1.80
                legacyIncompleteFolder: [[history objectForKey: @"UseIncompleteFolder"] boolValue] //upgrading from versions < 1.80
                                        ? [history objectForKey: @"IncompleteFolder"] : nil];
    
    if (self)
    {
        //start transfer
        NSNumber * active;
        if (!pause && (active = [history objectForKey: @"Active"]) && [active boolValue])
        {
            fStat = tr_torrentStat(fHandle);
            [self startTransfer];
        }
        
        //upgrading from versions < 1.30: get old added, activity, and done dates
        NSDate * date;
        if ((date = [history objectForKey: @"Date"]))
            tr_torrentSetAddedDate(fHandle, [date timeIntervalSince1970]);
        if ((date = [history objectForKey: @"DateActivity"]))
            tr_torrentSetActivityDate(fHandle, [date timeIntervalSince1970]);
        if ((date = [history objectForKey: @"DateCompleted"]))
            tr_torrentSetDoneDate(fHandle, [date timeIntervalSince1970]);
        
        //upgrading from versions < 1.60: get old stop ratio settings
        NSNumber * ratioSetting;
        if ((ratioSetting = [history objectForKey: @"RatioSetting"]))
        {
            switch ([ratioSetting intValue])
            {
                case NSOnState: [self setRatioSetting: TR_RATIOLIMIT_SINGLE]; break;
                case NSOffState: [self setRatioSetting: TR_RATIOLIMIT_UNLIMITED]; break;
                case NSMixedState: [self setRatioSetting: TR_RATIOLIMIT_GLOBAL]; break;
            }
        }
        NSNumber * ratioLimit;
        if ((ratioLimit = [history objectForKey: @"RatioLimit"]))
            [self setRatioLimit: [ratioLimit floatValue]];
    }
    return self;
}

- (NSDictionary *) history
{
    return [NSDictionary dictionaryWithObjectsAndKeys:
                [self torrentLocation], @"InternalTorrentPath",
                [self hashString], @"TorrentHash",
                [NSNumber numberWithBool: [self isActive]], @"Active",
                [NSNumber numberWithBool: fWaitToStart], @"WaitToStart",
                [NSNumber numberWithInt: fGroupValue], @"GroupValue", nil];
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    if (fFileStat)
        tr_torrentFilesFree(fFileStat, [self fileCount]);
    
    [fPreviousFinishedIndexes release];
    [fPreviousFinishedIndexesDate release];
    
    [fHashString release];
    
    [fIcon release];
    
    [fFileList release];
    [fFlatFileList release];
    
    [fTimeMachineExclude release];
    
    [super dealloc];
}

- (NSString *) description
{
    return [@"Torrent: " stringByAppendingString: [self name]];
}

- (void) closeRemoveTorrent
{
    //allow the file to be index by Time Machine
    if (fTimeMachineExclude)
    {
        [self setTimeMachineExclude: NO forPath: fTimeMachineExclude];
        [fTimeMachineExclude release];
        fTimeMachineExclude = nil;
    }
    
    tr_torrentRemove(fHandle);
}

- (void) changeDownloadFolderBeforeUsing: (NSString *) folder
{
     tr_torrentSetDownloadDir(fHandle, [folder UTF8String]);
     [self updateTimeMachineExclude];
}

- (NSString *) currentDirectory
{
    return [NSString stringWithUTF8String: tr_torrentGetCurrentDir(fHandle)];
}

- (void) getAvailability: (int8_t *) tab size: (NSInteger) size
{
    tr_torrentAvailability(fHandle, tab, size);
}

- (void) getAmountFinished: (float *) tab size: (NSInteger) size
{
    tr_torrentAmountFinished(fHandle, tab, size);
}

- (NSIndexSet *) previousFinishedPieces
{
    //if the torrent hasn't been seen in a bit, and therefore hasn't been refreshed, return nil
    if (fPreviousFinishedIndexesDate && [fPreviousFinishedIndexesDate timeIntervalSinceNow] > -2.0)
        return fPreviousFinishedIndexes;
    else
        return nil;
}

-(void) setPreviousFinishedPieces: (NSIndexSet *) indexes
{
    [fPreviousFinishedIndexes release];
    fPreviousFinishedIndexes = [indexes retain];
    
    [fPreviousFinishedIndexesDate release];
    fPreviousFinishedIndexesDate = indexes != nil ? [[NSDate alloc] init] : nil;
}

- (void) update
{
    //get previous status values before update
    BOOL wasChecking = NO, wasError = NO, wasStalled = NO;
    if (fStat != NULL)
    {
        wasChecking = [self isChecking];
        wasError = [self isError];
        wasStalled = fStalled;
    }
    
    fStat = tr_torrentStat(fHandle);
    
    //check if stalled (stored because based on time and needs to check if it was previously stalled)
    fStalled = [self isActive] && [fDefaults boolForKey: @"CheckStalled"]
                && [self stalledMinutes] > [fDefaults integerForKey: @"StalledMinutes"];
    
    //update queue for checking (from downloading to seeding), stalled, or error
    if ((wasChecking && ![self isChecking]) || (wasStalled != fStalled) || (!wasError && [self isError] && [self isActive]))
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
    
    //when the data first appears, update time machine exclusion
    if (!fTimeMachineExclude)
        [self updateTimeMachineExclude];
}

- (void) startTransfer
{
    fWaitToStart = NO;
    fFinishedSeeding = NO;
    
    if (![self isActive] && [self alertForRemainingDiskSpace])
    {
        tr_torrentStart(fHandle);
        [self update];
    }
}

- (void) stopTransfer
{
    fWaitToStart = NO;
    
    if ([self isActive])
    {
        tr_torrentStop(fHandle);
        [self update];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
    }
}

- (void) sleep
{
    if ((fResumeOnWake = [self isActive]))
        tr_torrentStop(fHandle);
}

- (void) wakeUp
{
    if (fResumeOnWake)
        tr_torrentStart(fHandle);
}

- (void) manualAnnounce
{
    tr_torrentManualUpdate(fHandle);
}

- (BOOL) canManualAnnounce
{
    return tr_torrentCanManualUpdate(fHandle);
}

- (void) resetCache
{
    tr_torrentVerify(fHandle);
    [self update];
}

- (BOOL) isMagnet
{
    return !tr_torrentHasMetadata(fHandle);
}

- (NSString *) magnetLink
{
    return [NSString stringWithUTF8String: tr_torrentGetMagnetLink(fHandle)];
}

- (CGFloat) ratio
{
    return fStat->ratio;
}

- (tr_ratiolimit) ratioSetting
{
    return tr_torrentGetRatioMode(fHandle);
}

- (void) setRatioSetting: (tr_ratiolimit) setting
{
    tr_torrentSetRatioMode(fHandle, setting);
}

- (CGFloat) ratioLimit
{
    return tr_torrentGetRatioLimit(fHandle);
}

- (void) setRatioLimit: (CGFloat) limit
{
    NSAssert(limit >= 0, @"Ratio cannot be negative");
    tr_torrentSetRatioLimit(fHandle, limit);
}

- (BOOL) seedRatioSet
{
    return tr_torrentGetSeedRatio(fHandle, NULL);
}

- (CGFloat) progressStopRatio
{
    return fStat->percentRatio;
}

- (BOOL) usesSpeedLimit: (BOOL) upload
{
    return tr_torrentUsesSpeedLimit(fHandle, upload ? TR_UP : TR_DOWN);
}

- (void) setUseSpeedLimit: (BOOL) use upload: (BOOL) upload
{
    tr_torrentUseSpeedLimit(fHandle, upload ? TR_UP : TR_DOWN, use);
}

- (NSInteger) speedLimit: (BOOL) upload
{
    return tr_torrentGetSpeedLimit(fHandle, upload ? TR_UP : TR_DOWN);
}

- (void) setSpeedLimit: (NSInteger) limit upload: (BOOL) upload
{
    tr_torrentSetSpeedLimit(fHandle, upload ? TR_UP : TR_DOWN, limit);
}

- (BOOL) usesGlobalSpeedLimit
{
    return tr_torrentUsesSessionLimits(fHandle);
}

- (void) setUseGlobalSpeedLimit: (BOOL) use
{
    tr_torrentUseSessionLimits(fHandle, use);
}

- (void) setMaxPeerConnect: (uint16_t) count
{
    NSAssert(count > 0, @"max peer count must be greater than 0");
    
    tr_torrentSetPeerLimit(fHandle, count);
}

- (uint16_t) maxPeerConnect
{
    return tr_torrentGetPeerLimit(fHandle);
}

- (void) setWaitToStart: (BOOL) wait
{
    fWaitToStart = wait;
}

- (BOOL) waitingToStart
{
    return fWaitToStart;
}

- (tr_priority_t) priority
{
    return tr_torrentGetPriority(fHandle);
}

- (void) setPriority: (tr_priority_t) priority
{
    return tr_torrentSetPriority(fHandle, priority);
}

#warning when 10.6-only use recycleURLs:completionHandler:
+ (void) trashFile: (NSString *) path
{
    //attempt to move to trash
    if (![[NSWorkspace sharedWorkspace] performFileOperation: NSWorkspaceRecycleOperation
        source: [path stringByDeletingLastPathComponent] destination: @""
        files: [NSArray arrayWithObject: [path lastPathComponent]] tag: nil])
    {
        //if cannot trash, just delete it (will work if it's on a remote volume)
        NSError * error;
        if (![[NSFileManager defaultManager] removeItemAtPath: path error: &error])
            NSLog(@"Could not trash %@: %@", path, [error localizedDescription]);
    }
}

- (void) trashData
{
    tr_torrentDeleteLocalData(fHandle, trashDataFile);
}

- (void) moveTorrentDataFileTo: (NSString *) folder
{
    NSString * oldFolder = [self currentDirectory];
    if ([oldFolder isEqualToString: folder])
        return;
    
    //check if moving inside itself
    NSArray * oldComponents = [oldFolder pathComponents],
            * newComponents = [folder pathComponents];
    const NSInteger oldCount = [oldComponents count];
    
    if (oldCount < [newComponents count] && [[newComponents objectAtIndex: oldCount] isEqualToString: [self name]]
        && [folder hasPrefix: oldFolder])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: NSLocalizedString(@"A folder cannot be moved to inside itself.",
                                                    "Move inside itself alert -> title")];
        [alert setInformativeText: [NSString stringWithFormat:
                        NSLocalizedString(@"The move operation of \"%@\" cannot be done.",
                                            "Move inside itself alert -> message"), [self name]]];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Move inside itself alert -> button")];
        
        [alert runModal];
        [alert release];
        
        return;
    }
    
    volatile int status;
    tr_torrentSetLocation(fHandle, [folder UTF8String], YES, NULL, &status);
    
    while (status == TR_LOC_MOVING) //block while moving (for now)
        [NSThread sleepForTimeInterval: 0.05];
    
    if (status == TR_LOC_DONE)
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateStats" object: nil];
    else
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: NSLocalizedString(@"There was an error moving the data file.", "Move error alert -> title")];
        [alert setInformativeText: [NSString stringWithFormat:
                NSLocalizedString(@"The move operation of \"%@\" cannot be done.", "Move error alert -> message"), [self name]]];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Move error alert -> button")];
        
        [alert runModal];
        [alert release];
    }
    
    [self updateTimeMachineExclude];
}

- (void) copyTorrentFileTo: (NSString *) path
{
    [[NSFileManager defaultManager] copyItemAtPath: [self torrentLocation] toPath: path error: NULL];
}

- (BOOL) alertForRemainingDiskSpace
{
    if ([self allDownloaded] || ![fDefaults boolForKey: @"WarningRemainingSpace"])
        return YES;
    
    NSString * downloadFolder = [self currentDirectory], * volumeName;
    if ((volumeName = [[[NSFileManager defaultManager] componentsToDisplayForPath: downloadFolder] objectAtIndex: 0]))
    {
        NSDictionary * systemAttributes = [[NSFileManager defaultManager] attributesOfFileSystemForPath: downloadFolder error: NULL];
        uint64_t remainingSpace = [[systemAttributes objectForKey: NSFileSystemFreeSize] unsignedLongLongValue];
        
        //if the remaining space is greater than the size left, then there is enough space regardless of preallocation
        if (remainingSpace < [self sizeLeft] && remainingSpace < tr_torrentGetBytesLeftToAllocate(fHandle))
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert setMessageText: [NSString stringWithFormat:
                                    NSLocalizedString(@"Not enough remaining disk space to download \"%@\" completely.",
                                        "Torrent disk space alert -> title"), [self name]]];
            [alert setInformativeText: [NSString stringWithFormat: NSLocalizedString(@"The transfer will be paused."
                                        " Clear up space on %@ or deselect files in the torrent inspector to continue.",
                                        "Torrent disk space alert -> message"), volumeName]];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Torrent disk space alert -> button")];
            [alert addButtonWithTitle: NSLocalizedString(@"Download Anyway", "Torrent disk space alert -> button")];
            
            [alert setShowsSuppressionButton: YES];
            [[alert suppressionButton] setTitle: NSLocalizedString(@"Do not check disk space again",
                                                    "Torrent disk space alert -> button")];

            const NSInteger result = [alert runModal];
            if ([[alert suppressionButton] state] == NSOnState)
                [fDefaults setBool: NO forKey: @"WarningRemainingSpace"];
            [alert release];
            
            return result != NSAlertFirstButtonReturn;
        }
    }
    return YES;
}

- (NSImage *) icon
{
    if ([self isMagnet])
        return [NSImage imageNamed: @"Magnet.png"];
    
    #warning replace 'fldr' stuff with NSImageNameFolder on 10.6
    if (!fIcon)
        fIcon = [[[NSWorkspace sharedWorkspace] iconForFileType: [self isFolder] ? NSFileTypeForHFSTypeCode('fldr')
                                                                                : [[self name] pathExtension]] retain];
    return fIcon;
}

- (NSString *) name
{
    return fInfo->name != NULL ? [NSString stringWithUTF8String: fInfo->name] : @"";
}

- (BOOL) isFolder
{
    return fInfo->isMultifile;
}

- (uint64_t) size
{
    return fInfo->totalSize;
}

- (uint64_t) sizeLeft
{
    return fStat->leftUntilDone;
}

- (NSMutableArray *) allTrackerStats
{
    int count;
    tr_tracker_stat * stats = tr_torrentTrackers(fHandle, &count);
    
    NSMutableArray * trackers = [NSMutableArray arrayWithCapacity: (count > 0 ? count + stats[count-1].tier : 0)];
    
    int prevTier = -1;
    for (int i=0; i < count; ++i)
    {
        if (stats[i].tier != prevTier)
        {
            [trackers addObject: [NSNumber numberWithInteger: stats[i].tier]];
            prevTier = stats[i].tier;
        }
        
        TrackerNode * tracker = [[TrackerNode alloc] initWithTrackerStat: &stats[i]];
        [trackers addObject: tracker];
        [tracker release];
    }
    
    tr_torrentTrackersFree(stats, count);
    return trackers;
}

- (NSArray *) allTrackersFlat
{
    NSMutableArray * allTrackers = [NSMutableArray arrayWithCapacity: fInfo->trackerCount];
    
    for (NSInteger i=0; i < fInfo->trackerCount; i++)
        [allTrackers addObject: [NSString stringWithUTF8String: fInfo->trackers[i].announce]];
    
    return allTrackers;
}

- (BOOL) addTrackerToNewTier: (NSString *) tracker
{
    tracker = [tracker stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    
    if ([tracker rangeOfString: @"://"].location == NSNotFound)
        tracker = [@"http://" stringByAppendingString: tracker];
    
    //recreate the tracker structure
    const int oldTrackerCount = fInfo->trackerCount;
    tr_tracker_info * trackerStructs = tr_new(tr_tracker_info, oldTrackerCount+1);
    for (NSInteger i=0; i < oldTrackerCount; ++i)
        trackerStructs[i] = fInfo->trackers[i];
    
    trackerStructs[oldTrackerCount].announce = (char *)[tracker UTF8String];
    trackerStructs[oldTrackerCount].tier = trackerStructs[oldTrackerCount-1].tier + 1;
    
    const BOOL success = tr_torrentSetAnnounceList(fHandle, trackerStructs, oldTrackerCount+1);
    tr_free(trackerStructs);
    
    return success;
}

- (void) removeTrackersAtIndexes: (NSIndexSet *) removeIndexes
{
    NSAssert([removeIndexes lastIndex] < fInfo->trackerCount, @"Trying to remove trackers outside the tracker count.");
    
    NSMutableIndexSet * indexes = [NSMutableIndexSet indexSetWithIndexesInRange: NSMakeRange(0, fInfo->trackerCount)];
    [indexes removeIndexes: removeIndexes];
    
    //recreate the tracker structure
    tr_tracker_info * trackerStructs = tr_new(tr_tracker_info, [indexes count]);
    
    int newCount = 0;
    for (NSUInteger oldIndex = [indexes firstIndex]; oldIndex != NSNotFound; oldIndex = [indexes indexGreaterThanIndex: oldIndex])
    {
        NSLog(@"oldIndex: %d %s", oldIndex, fInfo->trackers[oldIndex].announce);
        trackerStructs[newCount++] = fInfo->trackers[oldIndex];
    }
    
    const BOOL success = tr_torrentSetAnnounceList(fHandle, trackerStructs, newCount);
    NSAssert(success, @"Removing tracker addresses failed");
    
    tr_free(trackerStructs);
}

- (NSString *) comment
{
    return fInfo->comment ? [NSString stringWithUTF8String: fInfo->comment] : @"";
}

- (NSString *) creator
{
    return fInfo->creator ? [NSString stringWithUTF8String: fInfo->creator] : @"";
}

- (NSDate *) dateCreated
{
    NSInteger date = fInfo->dateCreated;
    return date > 0 ? [NSDate dateWithTimeIntervalSince1970: date] : nil;
}

- (NSInteger) pieceSize
{
    return fInfo->pieceSize;
}

- (NSInteger) pieceCount
{
    return fInfo->pieceCount;
}

- (NSString *) hashString
{
    return fHashString;
}

- (BOOL) privateTorrent
{
    return fInfo->isPrivate;
}

- (NSString *) torrentLocation
{
    return fInfo->torrent ? [NSString stringWithUTF8String: fInfo->torrent] : @"";
}

- (NSString *) dataLocation
{
    if ([self isMagnet])
        return nil;
    
    if ([self isFolder])
    {
        NSString * dataLocation = [[self currentDirectory] stringByAppendingPathComponent: [self name]];
        
        if (![[NSFileManager defaultManager] fileExistsAtPath: dataLocation])
            return nil;
        
        return dataLocation;
    }
    else
    {
        char * location = tr_torrentFindFile(fHandle, 0);
        if (location == NULL)
            return nil;
        
        NSString * dataLocation = [NSString stringWithUTF8String: location];
        free(location);
        
        return dataLocation;
    }
}

- (NSString *) fileLocation: (FileListNode *) node
{
    if ([node isFolder])
    {
        NSString * basePath = [[node path] stringByAppendingPathComponent: [node name]];
        NSString * dataLocation = [[self currentDirectory] stringByAppendingPathComponent: basePath];
        
        if (![[NSFileManager defaultManager] fileExistsAtPath: dataLocation])
            return nil;
        
        return dataLocation;
    }
    else
    {
        char * location = tr_torrentFindFile(fHandle, [[node indexes] firstIndex]);
        if (location == NULL)
            return nil;
        
        NSString * dataLocation = [NSString stringWithUTF8String: location];
        free(location);
        
        return dataLocation;
    }
}

- (CGFloat) progress
{
    return fStat->percentComplete;
}

- (CGFloat) progressDone
{
    return fStat->percentDone;
}

- (CGFloat) progressLeft
{
    if ([self size] == 0) //magnet links
        return 0.0;
    
    return (CGFloat)[self sizeLeft] / [self size];
}

- (CGFloat) checkingProgress
{
    return fStat->recheckProgress;
}

- (NSInteger) eta
{
    return fStat->eta;
}

- (CGFloat) availableDesired
{
    return (CGFloat)fStat->desiredAvailable / [self sizeLeft];
}

- (BOOL) isActive
{
    return fStat->activity != TR_STATUS_STOPPED;
}

- (BOOL) isSeeding
{
    return fStat->activity == TR_STATUS_SEED;
}

- (BOOL) isChecking
{
    return fStat->activity == TR_STATUS_CHECK || fStat->activity == TR_STATUS_CHECK_WAIT;
}

- (BOOL) isCheckingWaiting
{
    return fStat->activity == TR_STATUS_CHECK_WAIT;
}

- (BOOL) allDownloaded
{
    return [self sizeLeft] == 0 && ![self isMagnet];
}

- (BOOL) isComplete
{
    return [self progress] >= 1.0;
}

- (BOOL) isError
{
    return fStat->error == TR_STAT_LOCAL_ERROR;
}

- (BOOL) isAnyErrorOrWarning
{
    return fStat->error != TR_STAT_OK;
}

- (NSString *) errorMessage
{
    if (![self isAnyErrorOrWarning])
        return @"";
    
    NSString * error;
    if (!(error = [NSString stringWithUTF8String: fStat->errorString])
        && !(error = [NSString stringWithCString: fStat->errorString encoding: NSISOLatin1StringEncoding]))
        error = [NSString stringWithFormat: @"(%@)", NSLocalizedString(@"unreadable error", "Torrent -> error string unreadable")];
    
    //libtransmission uses "Set Location", Mac client uses "Move data file to..." - very hacky!
    error = [error stringByReplacingOccurrencesOfString: @"Set Location" withString: [@"Move Data File To" stringByAppendingEllipsis]];
    
    return error;
}

- (NSArray *) peers
{
    int totalPeers;
    tr_peer_stat * peers = tr_torrentPeers(fHandle, &totalPeers);
    
    NSMutableArray * peerDicts = [NSMutableArray arrayWithCapacity: totalPeers];
    
    for (int i = 0; i < totalPeers; i++)
    {
        tr_peer_stat * peer = &peers[i];
        NSMutableDictionary * dict = [NSMutableDictionary dictionaryWithCapacity: 10];
        
        [dict setObject: [NSNumber numberWithInt: peer->from] forKey: @"From"];
        [dict setObject: [NSString stringWithUTF8String: peer->addr] forKey: @"IP"];
        [dict setObject: [NSNumber numberWithInt: peer->port] forKey: @"Port"];
        [dict setObject: [NSNumber numberWithFloat: peer->progress] forKey: @"Progress"];
        [dict setObject: [NSNumber numberWithBool: peer->isSeed] forKey: @"Seed"];
        [dict setObject: [NSNumber numberWithBool: peer->isEncrypted] forKey: @"Encryption"];
        [dict setObject: [NSString stringWithUTF8String: peer->client] forKey: @"Client"];
        [dict setObject: [NSString stringWithUTF8String: peer->flagStr] forKey: @"Flags"];
        
        if (peer->isUploadingTo)
            [dict setObject: [NSNumber numberWithFloat: peer->rateToPeer] forKey: @"UL To Rate"];
        if (peer->isDownloadingFrom)
            [dict setObject: [NSNumber numberWithFloat: peer->rateToClient] forKey: @"DL From Rate"];
        
        [peerDicts addObject: dict];
    }
    
    tr_torrentPeersFree(peers, totalPeers);
    
    return peerDicts;
}

- (NSUInteger) webSeedCount
{
    return fInfo->webseedCount;
}

- (NSArray *) webSeeds
{
    const NSInteger webSeedCount = fInfo->webseedCount;
    NSMutableArray * webSeeds = [NSMutableArray arrayWithCapacity: webSeedCount];
    
    float * dlSpeeds = tr_torrentWebSpeeds(fHandle);
    
    for (NSInteger i = 0; i < webSeedCount; i++)
    {
        NSMutableDictionary * dict = [NSMutableDictionary dictionaryWithCapacity: 2];
        
        [dict setObject: [NSString stringWithUTF8String: fInfo->webseeds[i]] forKey: @"Address"];
        
        if (dlSpeeds[i] != -1.0)
            [dict setObject: [NSNumber numberWithFloat: dlSpeeds[i]] forKey: @"DL From Rate"];
        
        [webSeeds addObject: dict];
    }
    
    tr_free(dlSpeeds);
    
    return webSeeds;
}

- (NSString *) progressString
{
    if ([self isMagnet])
    {
        NSString * progressString = fStat->metadataPercentComplete > 0.0
                    ? [NSString localizedStringWithFormat: NSLocalizedString(@"%.2f%% of torrent metadata retrieved",
                        "Torrent -> progress string"), 100.0 * fStat->metadataPercentComplete]
                    : NSLocalizedString(@"torrent metadata needed", "Torrent -> progress string");
        
        return [NSString stringWithFormat: @"%@ - %@", NSLocalizedString(@"Magnetized transfer", "Torrent -> progress string"),
                                            progressString];
    }
    
    NSString * string;
    
    if (![self allDownloaded])
    {
        CGFloat progress;
        if ([self isFolder] && [fDefaults boolForKey: @"DisplayStatusProgressSelected"])
        {
            string = [NSString stringWithFormat: NSLocalizedString(@"%@ of %@ selected", "Torrent -> progress string"),
                        [NSString stringForFileSize: [self haveTotal]], [NSString stringForFileSize: [self totalSizeSelected]]];
            progress = 100.0 * [self progressDone];
        }
        else
        {
            string = [NSString stringWithFormat: NSLocalizedString(@"%@ of %@", "Torrent -> progress string"),
                        [NSString stringForFileSize: [self haveTotal]], [NSString stringForFileSize: [self size]]];
            progress = 100.0 * [self progress];
        }
        
        string = [NSString localizedStringWithFormat: @"%@ (%.2f%%)", string, progress];
    }
    else
    {
        NSString * downloadString;
        if (![self isComplete]) //only multifile possible
        {
            if ([fDefaults boolForKey: @"DisplayStatusProgressSelected"])
                downloadString = [NSString stringWithFormat: NSLocalizedString(@"%@ selected", "Torrent -> progress string"),
                                    [NSString stringForFileSize: [self haveTotal]]];
            else
            {
                downloadString = [NSString stringWithFormat: NSLocalizedString(@"%@ of %@", "Torrent -> progress string"),
                                    [NSString stringForFileSize: [self haveTotal]], [NSString stringForFileSize: [self size]]];
                
                downloadString = [NSString localizedStringWithFormat: @"%@ (%.2f%%)", downloadString, 100.0 * [self progress]];
            }
        }
        else
            downloadString = [NSString stringForFileSize: [self size]];
        
        NSString * uploadString = [NSString stringWithFormat: NSLocalizedString(@"uploaded %@ (Ratio: %@)",
                                    "Torrent -> progress string"), [NSString stringForFileSize: [self uploadedTotal]],
                                    [NSString stringForRatio: [self ratio]]];
        
        string = [downloadString stringByAppendingFormat: @", %@", uploadString];
    }
    
    //add time when downloading
    if (fStat->activity == TR_STATUS_DOWNLOAD || ([self isSeeding] && [self seedRatioSet]))
        string = [string stringByAppendingFormat: @" - %@", [self etaString]];
    
    return string;
}

- (NSString *) statusString
{
    NSString * string;
    
    if ([self isAnyErrorOrWarning])
    {
        switch (fStat->error)
        {
            case TR_STAT_LOCAL_ERROR: string = NSLocalizedString(@"Error", "Torrent -> status string"); break;
            case TR_STAT_TRACKER_ERROR: string = NSLocalizedString(@"Tracker returned an error", "Torrent -> status string"); break;
            case TR_STAT_TRACKER_WARNING: string = NSLocalizedString(@"Tracker returned a warning", "Torrent -> status string"); break;
            default: NSAssert(NO, @"unknown error state");
        }
        
        NSString * errorString = [self errorMessage];
        if (errorString && ![errorString isEqualToString: @""])
            string = [string stringByAppendingFormat: @": %@", errorString];
    }
    else
    {
        switch (fStat->activity)
        {
            case TR_STATUS_STOPPED:
                if (fWaitToStart)
                {
                    string = ![self allDownloaded]
                            ? [NSLocalizedString(@"Waiting to download", "Torrent -> status string") stringByAppendingEllipsis]
                            : [NSLocalizedString(@"Waiting to seed", "Torrent -> status string") stringByAppendingEllipsis];
                }
                else if (fFinishedSeeding)
                    string = NSLocalizedString(@"Seeding complete", "Torrent -> status string");
                else
                    string = NSLocalizedString(@"Paused", "Torrent -> status string");
                break;

            case TR_STATUS_CHECK_WAIT:
                string = [NSLocalizedString(@"Waiting to check existing data", "Torrent -> status string") stringByAppendingEllipsis];
                break;

            case TR_STATUS_CHECK:
                string = [NSString localizedStringWithFormat: @"%@ (%.2f%%)",
                            NSLocalizedString(@"Checking existing data", "Torrent -> status string"), 100.0 * [self checkingProgress]];
                break;

            case TR_STATUS_DOWNLOAD:
                if ([self totalPeersConnected] != 1)
                    string = [NSString stringWithFormat: NSLocalizedString(@"Downloading from %d of %d peers",
                                                    "Torrent -> status string"), [self peersSendingToUs], [self totalPeersConnected]];
                else
                    string = [NSString stringWithFormat: NSLocalizedString(@"Downloading from %d of 1 peer",
                                                    "Torrent -> status string"), [self peersSendingToUs]];
                
                const NSInteger webSeedCount = fStat->webseedsSendingToUs;
                if (webSeedCount > 0)
                {
                    NSString * webSeedString;
                    if (webSeedCount == 1)
                        webSeedString = NSLocalizedString(@"web seed", "Torrent -> status string");
                    else
                        webSeedString = [NSString stringWithFormat: NSLocalizedString(@"%d web seeds", "Torrent -> status string"),
                                                                    webSeedCount];
                    
                    string = [string stringByAppendingFormat: @" + %@", webSeedString];
                }
                
                break;

            case TR_STATUS_SEED:
                if ([self totalPeersConnected] != 1)
                    string = [NSString stringWithFormat: NSLocalizedString(@"Seeding to %d of %d peers", "Torrent -> status string"),
                                                    [self peersGettingFromUs], [self totalPeersConnected]];
                else
                    string = [NSString stringWithFormat: NSLocalizedString(@"Seeding to %d of 1 peer", "Torrent -> status string"),
                                                    [self peersGettingFromUs]];
        }
        
        if (fStalled)
            string = [NSLocalizedString(@"Stalled", "Torrent -> status string") stringByAppendingFormat: @", %@", string];
    }
    
    //append even if error
    if ([self isActive] && ![self isChecking])
    {
        if (fStat->activity == TR_STATUS_DOWNLOAD)
            string = [string stringByAppendingFormat: @" - %@: %@, %@: %@",
                        NSLocalizedString(@"DL", "Torrent -> status string"), [NSString stringForSpeed: [self downloadRate]],
                        NSLocalizedString(@"UL", "Torrent -> status string"), [NSString stringForSpeed: [self uploadRate]]];
        else
            string = [string stringByAppendingFormat: @" - %@: %@",
                        NSLocalizedString(@"UL", "Torrent -> status string"), [NSString stringForSpeed: [self uploadRate]]];
    }
    
    return string;
}

- (NSString *) shortStatusString
{
    NSString * string;
    
    switch (fStat->activity)
    {
        case TR_STATUS_STOPPED:
            if (fWaitToStart)
            {
                string = ![self allDownloaded]
                        ? [NSLocalizedString(@"Waiting to download", "Torrent -> status string") stringByAppendingEllipsis]
                        : [NSLocalizedString(@"Waiting to seed", "Torrent -> status string") stringByAppendingEllipsis];
            }
            else if (fFinishedSeeding)
                string = NSLocalizedString(@"Seeding complete", "Torrent -> status string");
            else
                string = NSLocalizedString(@"Paused", "Torrent -> status string");
            break;

        case TR_STATUS_CHECK_WAIT:
            string = [NSLocalizedString(@"Waiting to check existing data", "Torrent -> status string") stringByAppendingEllipsis];
            break;

        case TR_STATUS_CHECK:
            string = [NSString localizedStringWithFormat: @"%@ (%.2f%%)",
                        NSLocalizedString(@"Checking existing data", "Torrent -> status string"), 100.0 * [self checkingProgress]];
            break;
        
        case TR_STATUS_DOWNLOAD:
            string = [NSString stringWithFormat: @"%@: %@, %@: %@",
                            NSLocalizedString(@"DL", "Torrent -> status string"), [NSString stringForSpeed: [self downloadRate]],
                            NSLocalizedString(@"UL", "Torrent -> status string"), [NSString stringForSpeed: [self uploadRate]]];
            break;
        
        case TR_STATUS_SEED:
            string = [NSString stringWithFormat: @"%@: %@, %@: %@",
                            NSLocalizedString(@"Ratio", "Torrent -> status string"), [NSString stringForRatio: [self ratio]],
                            NSLocalizedString(@"UL", "Torrent -> status string"), [NSString stringForSpeed: [self uploadRate]]];
    }
    
    return string;
}

- (NSString *) remainingTimeString
{
    if (fStat->activity == TR_STATUS_DOWNLOAD || ([self isSeeding] && [self seedRatioSet]))
        return [self etaString];
    else
        return [self shortStatusString];
}

- (NSString *) stateString
{
    switch (fStat->activity)
    {
        case TR_STATUS_STOPPED:
            return NSLocalizedString(@"Paused", "Torrent -> status string");

        case TR_STATUS_CHECK:
            return [NSString localizedStringWithFormat: @"%@ (%.2f%%)",
                    NSLocalizedString(@"Checking existing data", "Torrent -> status string"), 100.0 * [self checkingProgress]];
        
        case TR_STATUS_CHECK_WAIT:
            return [NSLocalizedString(@"Waiting to check existing data", "Torrent -> status string") stringByAppendingEllipsis];

        case TR_STATUS_DOWNLOAD:
            return NSLocalizedString(@"Downloading", "Torrent -> status string");

        case TR_STATUS_SEED:
            return NSLocalizedString(@"Seeding", "Torrent -> status string");
    }
}

- (NSInteger) totalPeersConnected
{
    return fStat->peersConnected;
}

- (NSInteger) totalPeersTracker
{
    return fStat->peersFrom[TR_PEER_FROM_TRACKER];
}

- (NSInteger) totalPeersIncoming
{
    return fStat->peersFrom[TR_PEER_FROM_INCOMING];
}

- (NSInteger) totalPeersCache
{
    return fStat->peersFrom[TR_PEER_FROM_RESUME];
}

- (NSInteger) totalPeersPex
{
    return fStat->peersFrom[TR_PEER_FROM_PEX];
}

- (NSInteger) totalPeersDHT
{
    return fStat->peersFrom[TR_PEER_FROM_DHT];
}

- (NSInteger) totalPeersLTEP
{
    return fStat->peersFrom[TR_PEER_FROM_LTEP];
}

- (NSInteger) totalPeersKnown
{
    return fStat->peersKnown;
}

- (NSInteger) peersSendingToUs
{
    return fStat->peersSendingToUs;
}

- (NSInteger) peersGettingFromUs
{
    return fStat->peersGettingFromUs;
}

- (CGFloat) downloadRate
{
    return fStat->pieceDownloadSpeed;
}

- (CGFloat) uploadRate
{
    return fStat->pieceUploadSpeed;
}

- (CGFloat) totalRate
{
    return [self downloadRate] + [self uploadRate];
}

- (uint64_t) haveVerified
{
    return fStat->haveValid;
}

- (uint64_t) haveTotal
{
    return [self haveVerified] + fStat->haveUnchecked;
}

- (uint64_t) totalSizeSelected
{
    return fStat->sizeWhenDone;
}

- (uint64_t) downloadedTotal
{
    return fStat->downloadedEver;
}

- (uint64_t) uploadedTotal
{
    return fStat->uploadedEver;
}

- (uint64_t) failedHash
{
    return fStat->corruptEver;
}

- (NSInteger) groupValue
{
    return fGroupValue;
}

- (void) setGroupValue: (NSInteger) goupValue
{
    fGroupValue = goupValue;
}

- (NSInteger) groupOrderValue
{
    return [[GroupsController groups] rowValueForIndex: fGroupValue];
}

- (void) checkGroupValueForRemoval: (NSNotification *) notification
{
    if (fGroupValue != -1 && [[[notification userInfo] objectForKey: @"Index"] integerValue] == fGroupValue)
        fGroupValue = -1;
}

- (NSArray *) fileList
{
    if (!fFileList && ![self isMagnet])
        [self createFileList];
    
    return fFileList;
}

- (NSArray *) flatFileList
{
    if (!fFlatFileList && ![self isMagnet])
        [self createFileList];
    
    return fFlatFileList;
}

- (NSInteger) fileCount
{
    return fInfo->fileCount;
}

- (void) updateFileStat
{
    if (fFileStat)
        tr_torrentFilesFree(fFileStat, [self fileCount]);
    
    fFileStat = tr_torrentFiles(fHandle, NULL);
}

- (CGFloat) fileProgress: (FileListNode *) node
{
    if ([self fileCount] == 1 || [self isComplete])
        return [self progress];
    
    if (!fFileStat)
        [self updateFileStat];
    
    NSIndexSet * indexSet = [node indexes];
    
    if ([indexSet count] == 1)
        return fFileStat[[indexSet firstIndex]].progress;
    
    uint64_t have = 0;
    for (NSInteger index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
        have += fFileStat[index].bytesCompleted;
    
    NSAssert([node size], @"directory in torrent file has size 0");
    return (CGFloat)have / [node size];
}

- (BOOL) canChangeDownloadCheckForFile: (NSUInteger) index
{
    NSAssert2(index < [self fileCount], @"Index %d is greater than file count %d", index, [self fileCount]);
    
    if ([self fileCount] == 1 || [self isComplete])
        return NO;
    
    if (!fFileStat)
        [self updateFileStat];
    
    return fFileStat[index].progress < 1.0;
}

- (BOOL) canChangeDownloadCheckForFiles: (NSIndexSet *) indexSet
{
    if ([self fileCount] == 1 || [self isComplete])
        return NO;
    
    if (!fFileStat)
        [self updateFileStat];
    
    for (NSUInteger index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
        if (fFileStat[index].progress < 1.0)
            return YES;
    return NO;
}

- (NSInteger) checkForFiles: (NSIndexSet *) indexSet
{
    BOOL onState = NO, offState = NO;
    for (NSUInteger index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
    {
        if (tr_torrentGetFileDL(fHandle, index) || ![self canChangeDownloadCheckForFile: index])
            onState = YES;
        else
            offState = YES;
        
        if (onState && offState)
            return NSMixedState;
    }
    return onState ? NSOnState : NSOffState;
}

- (void) setFileCheckState: (NSInteger) state forIndexes: (NSIndexSet *) indexSet
{
    NSUInteger count = [indexSet count];
    tr_file_index_t * files = malloc(count * sizeof(tr_file_index_t));
    for (NSUInteger index = [indexSet firstIndex], i = 0; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index], i++)
        files[i] = index;
    
    tr_torrentSetFileDLs(fHandle, files, count, state != NSOffState);
    free(files);
    
    [self update];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentFileCheckChange" object: self];
}

- (void) setFilePriority: (tr_priority_t) priority forIndexes: (NSIndexSet *) indexSet
{
    const NSUInteger count = [indexSet count];
    tr_file_index_t * files = tr_malloc(count * sizeof(tr_file_index_t));
    for (NSUInteger index = [indexSet firstIndex], i = 0; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index], i++)
        files[i] = index;
    
    tr_torrentSetFilePriorities(fHandle, files, count, priority);
    tr_free(files);
}

- (BOOL) hasFilePriority: (tr_priority_t) priority forIndexes: (NSIndexSet *) indexSet
{
    for (NSUInteger index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
        if (priority == tr_torrentGetFilePriority(fHandle, index) && [self canChangeDownloadCheckForFile: index])
            return YES;
    return NO;
}

- (NSSet *) filePrioritiesForIndexes: (NSIndexSet *) indexSet
{
    BOOL low = NO, normal = NO, high = NO;
    NSMutableSet * priorities = [NSMutableSet setWithCapacity: MIN([indexSet count], 3)];
    
    for (NSUInteger index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
    {
        if (![self canChangeDownloadCheckForFile: index])
            continue;
        
        const tr_priority_t priority = tr_torrentGetFilePriority(fHandle, index);
        if (priority == TR_PRI_LOW)
        {
            if (low)
                continue;
            low = YES;
        }
        else if (priority == TR_PRI_HIGH)
        {
            if (high)
                continue;
            high = YES;
        }
        else
        {
            if (normal)
                continue;
            normal = YES;
        }
        
        [priorities addObject: [NSNumber numberWithInteger: priority]];
        if (low && normal && high)
            break;
    }
    return priorities;
}

- (NSDate *) dateAdded
{
    const time_t date = fStat->addedDate;
    return [NSDate dateWithTimeIntervalSince1970: date];
}

- (NSDate *) dateCompleted
{
    const time_t date = fStat->doneDate;
    return date != 0 ? [NSDate dateWithTimeIntervalSince1970: date] : nil;
}

- (NSDate *) dateActivity
{
    const time_t date = fStat->activityDate;
    return date != 0 ? [NSDate dateWithTimeIntervalSince1970: date] : nil;
}

- (NSDate *) dateActivityOrAdd
{
    NSDate * date = [self dateActivity];
    return date ? date : [self dateAdded];
}

- (NSInteger) stalledMinutes
{
    time_t start = fStat->startDate;
    if (start == 0)
        return -1;
    
    NSDate * started = [NSDate dateWithTimeIntervalSince1970: start],
            * activity = [self dateActivity];
    
    NSDate * laterDate = activity ? [started laterDate: activity] : started;
    return -1 * [laterDate timeIntervalSinceNow] / 60;
}

- (BOOL) isStalled
{
    return fStalled;
}

- (void) updateTimeMachineExclude
{
    NSString * currentLocation = ![self allDownloaded] ? [self dataLocation] : nil;
    
    //return if the locations are the same
    if (fTimeMachineExclude && currentLocation && [fTimeMachineExclude isEqualToString: currentLocation])
        return;
    
    //remove old location...
    if (fTimeMachineExclude)
    {
        [self setTimeMachineExclude: NO forPath: fTimeMachineExclude];
        [fTimeMachineExclude release];
        fTimeMachineExclude = nil;
    }
    
    //...set new location
    if (currentLocation)
    {
        [self setTimeMachineExclude: YES forPath: currentLocation];
        fTimeMachineExclude = [currentLocation retain];
    }
}

- (NSInteger) stateSortKey
{
    if (![self isActive]) //paused
    {
        if (fWaitToStart)
            return 1;
        else
            return 0;
    }
    else if ([self isSeeding]) //seeding
        return 10;
    else //downloading
        return 20;
}

- (NSString *) trackerSortKey
{
    int count;
    tr_tracker_stat * stats = tr_torrentTrackers(fHandle, &count);
    
    NSString * best = nil;
    
    for (int i=0; i < count; ++i)
    {
        NSString * tracker = [NSString stringWithUTF8String: stats[i].host];
        if (!best || [tracker localizedCaseInsensitiveCompare: best] == NSOrderedAscending)
            best = tracker;
    }
    
    tr_torrentTrackersFree(stats, count);
    return best;
}

- (tr_torrent *) torrentStruct
{
    return fHandle;
}

- (NSURL *) previewItemURL
{
    NSString * location = [self dataLocation];
    return location ? [NSURL fileURLWithPath: location] : nil;
}

@end

@implementation Torrent (Private)

- (id) initWithPath: (NSString *) path hash: (NSString *) hashString torrentStruct: (tr_torrent *) torrentStruct
        magnetAddress: (NSString *) magnetAddress lib: (tr_session *) lib
        waitToStart: (NSNumber *) waitToStart
        groupValue: (NSNumber *) groupValue
        downloadFolder: (NSString *) downloadFolder legacyIncompleteFolder: (NSString *) incompleteFolder
{
    if (!(self = [super init]))
        return nil;
    
    fDefaults = [NSUserDefaults standardUserDefaults];
    
    if (torrentStruct)
        fHandle = torrentStruct;
    else
    {
        //set libtransmission settings for initialization
        tr_ctor * ctor = tr_ctorNew(lib);
        
        tr_ctorSetPaused(ctor, TR_FORCE, YES);
        if (downloadFolder)
            tr_ctorSetDownloadDir(ctor, TR_FORCE, [downloadFolder UTF8String]);
        if (incompleteFolder)
            tr_ctorSetIncompleteDir(ctor, [incompleteFolder UTF8String]);
        
        tr_parse_result result = TR_PARSE_ERR;
        if (path)
            result = tr_ctorSetMetainfoFromFile(ctor, [path UTF8String]);
        
        if (result != TR_PARSE_OK && magnetAddress)
            result = tr_ctorSetMagnet(ctor, [magnetAddress UTF8String]);
        
        //backup - shouldn't be needed after upgrade to 1.70
        if (result != TR_PARSE_OK && hashString)
            result = tr_ctorSetMetainfoFromHash(ctor, [hashString UTF8String]);
        
        if (result == TR_PARSE_OK)
            fHandle = tr_torrentNew(ctor, NULL);
        
        tr_ctorFree(ctor);
        
        if (!fHandle)
        {
            [self release];
            return nil;
        }
    }

    fInfo = tr_torrentInfo(fHandle);
    
    tr_torrentSetCompletenessCallback(fHandle, completenessChangeCallback, self);
    tr_torrentSetRatioLimitHitCallback(fHandle, ratioLimitHitCallback, self);
    tr_torrentSetMetadataCallback(fHandle, metadataCallback, self);
    
    fHashString = [[NSString alloc] initWithUTF8String: fInfo->hashString];
	
    fFinishedSeeding = NO;
    
    fWaitToStart = waitToStart && [waitToStart boolValue];
    fResumeOnWake = NO;
	
    fGroupValue = groupValue ? [groupValue intValue] : [[GroupsController groups] groupIndexForTorrent: self]; 
    
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(checkGroupValueForRemoval:)
        name: @"GroupValueRemoved" object: nil];
    
    fTimeMachineExclude = nil;
    [self update];
    
    return self;
}

- (void) createFileList
{
    NSAssert(![self isMagnet], @"Cannot create a file list until the torrent is demagnetized");
    
    if ([self isFolder])
    {
        NSInteger count = [self fileCount];
        NSMutableArray * fileList = [NSMutableArray arrayWithCapacity: count],
                    * flatFileList = [NSMutableArray arrayWithCapacity: count];
        
        for (NSInteger i = 0; i < count; i++)
        {
            tr_file * file = &fInfo->files[i];
            
            NSString * fullPath = [NSString stringWithUTF8String: file->name];
            NSArray * pathComponents = [fullPath pathComponents];
            NSAssert1([pathComponents count] >= 2, @"Not enough components in path %@", fullPath);
            
            NSString * path = [pathComponents objectAtIndex: 0];
            NSString * name = [pathComponents objectAtIndex: 1];
            
            if ([pathComponents count] > 2)
            {
                //determine if folder node already exists
                FileListNode * node;
                for (node in fileList)
                    if ([[node name] isEqualToString: name] && [node isFolder])
                        break;
                
                if (!node)
                {
                    node = [[FileListNode alloc] initWithFolderName: name path: path];
                    [fileList addObject: node];
                    [node release];
                }
                
                NSMutableArray * trimmedComponents = [NSMutableArray arrayWithArray: [pathComponents subarrayWithRange:
                                                        NSMakeRange(2, [pathComponents count]-2)]];
                
                [node insertIndex: i withSize: file->length];
                [self insertPath: trimmedComponents forParent: node fileSize: file->length index: i flatList: flatFileList];
            }
            else
            {
                FileListNode * node = [[FileListNode alloc] initWithFileName: name path: path size: file->length index: i];
                [fileList addObject: node];
                [flatFileList addObject: node];
                [node release];
            }
        }
        
        fFileList = [[NSArray alloc] initWithArray: fileList];
        fFlatFileList = [[NSArray alloc] initWithArray: flatFileList];
    }
    else
    {
        FileListNode * node = [[FileListNode alloc] initWithFileName: [self name] path: @"" size: [self size] index: 0];
        fFileList = [[NSArray arrayWithObject: node] retain];
        fFlatFileList = [fFileList retain];
        [node release];
    }
}

- (void) insertPath: (NSMutableArray *) components forParent: (FileListNode *) parent fileSize: (uint64_t) size
    index: (NSInteger) index flatList: (NSMutableArray *) flatFileList
{
    NSString * name = [components objectAtIndex: 0];
    const BOOL isFolder = [components count] > 1;
    
    FileListNode * node = nil;
    if (isFolder)
    {
        for (node in [parent children])
            if ([[node name] isEqualToString: name] && [node isFolder])
                break;
    }
    
    //create new folder or file if it doesn't already exist
    if (!node)
    {
        NSString * path = [[parent path] stringByAppendingPathComponent: [parent name]];
        if (isFolder)
            node = [[FileListNode alloc] initWithFolderName: name path: path];
        else
        {
            node = [[FileListNode alloc] initWithFileName: name path: path size: size index: index];
            [flatFileList addObject: node];
        }
        
        [parent insertChild: node];
        [node release];
    }
    
    if (isFolder)
    {
        [node insertIndex: index withSize: size];
        
        [components removeObjectAtIndex: 0];
        [self insertPath: components forParent: node fileSize: size index: index flatList: flatFileList];
    }
}

//status has been retained
- (void) completenessChange: (NSNumber *) status
{
    fStat = tr_torrentStat(fHandle); //don't call update yet to avoid auto-stop
    
    switch ([status intValue])
    {
        case TR_SEED:
        case TR_PARTIAL_SEED:
            [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentFinishedDownloading" object: self];
            break;
        
        case TR_LEECH:
            [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentRestartedDownloading" object: self];
            break;
    }
    [status release];
    
    [self update];
    [self updateTimeMachineExclude];
}

- (void) ratioLimitHit
{
    fStat = tr_torrentStat(fHandle);
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentStoppedForRatio" object: self];
    
    fFinishedSeeding = YES;
}

- (void) metadataRetrieved
{
    fStat = tr_torrentStat(fHandle);
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"ResetInspector" object: self];
}

- (NSString *) etaString
{
    const NSInteger eta = [self eta];
    switch (eta)
    {
        case TR_ETA_NOT_AVAIL:
        case TR_ETA_UNKNOWN:
            return NSLocalizedString(@"remaining time unknown", "Torrent -> eta string");
        default:
            return [NSString stringWithFormat: NSLocalizedString(@"%@ remaining", "Torrent -> eta string"),
                        [NSString timeString: eta showSeconds: YES maxFields: 2]];
    }
}

- (void) setTimeMachineExclude: (BOOL) exclude forPath: (NSString *) path
{
    CSBackupSetItemExcluded((CFURLRef)[NSURL fileURLWithPath: path], exclude, true);
}

@end
