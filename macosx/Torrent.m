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

#import "Torrent.h"
#import "GroupsController.h"
#import "FileListNode.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "metainfo.h"
#import "utils.h" //tr_httpIsValidURL

@interface Torrent (Private)

- (id) initWithHash: (NSString *) hashString path: (NSString *) path torrentStruct: (tr_torrent *) torrentStruct lib: (tr_handle *) lib
        publicTorrent: (NSNumber *) publicTorrent
        downloadFolder: (NSString *) downloadFolder
        useIncompleteFolder: (NSNumber *) useIncompleteFolder incompleteFolder: (NSString *) incompleteFolder
        ratioSetting: (NSNumber *) ratioSetting ratioLimit: (NSNumber *) ratioLimit
        waitToStart: (NSNumber *) waitToStart
        orderValue: (NSNumber *) orderValue groupValue: (NSNumber *) groupValue addedTrackers: (NSNumber *) addedTrackers;

- (BOOL) shouldUseIncompleteFolderForName: (NSString *) name;
- (void) updateDownloadFolder;

- (void) createFileList;
- (void) insertPath: (NSMutableArray *) components forParent: (FileListNode *) parent fileSize: (uint64_t) size index: (NSInteger) index;

- (void) completenessChange: (NSNumber *) status;

- (void) quickPause;
- (void) endQuickPause;

- (NSString *) etaString: (NSInteger) eta;

- (void) updateAllTrackers: (NSMutableArray *) trackers;

- (void) trashFile: (NSString *) path;

- (void) setTimeMachineExclude: (BOOL) exclude forPath: (NSString *) path;

@end

void completenessChangeCallback(tr_torrent * torrent, tr_completeness status, void * torrentData)
{
    [(Torrent *)torrentData performSelectorOnMainThread: @selector(completenessChange:)
                withObject: [[NSNumber alloc] initWithInt: status] waitUntilDone: NO];
}

@implementation Torrent

- (id) initWithPath: (NSString *) path location: (NSString *) location deleteTorrentFile: (torrentFileState) torrentDelete
        lib: (tr_handle *) lib
{
    self = [self initWithHash: nil path: path torrentStruct: NULL lib: lib
            publicTorrent: torrentDelete != TORRENT_FILE_DEFAULT ? [NSNumber numberWithBool: torrentDelete == TORRENT_FILE_SAVE] : nil
            downloadFolder: location
            useIncompleteFolder: nil incompleteFolder: nil
            ratioSetting: nil ratioLimit: nil
            waitToStart: nil orderValue: nil groupValue: nil addedTrackers: nil];
    
    if (self)
    {
        //if the public and private torrent files are the same, then there is no public torrent
        if ([[self torrentLocation] isEqualToString: path])
        {
            fPublicTorrent = NO;
            [fPublicTorrentLocation release];
            fPublicTorrentLocation = nil;
        }
        else if (!fPublicTorrent)
            [self trashFile: path];
        else;
    }
    return self;
}

- (id) initWithTorrentStruct: (tr_torrent *) torrentStruct location: (NSString *) location lib: (tr_handle *) lib
{
    self = [self initWithHash: nil path: nil torrentStruct: torrentStruct lib: lib
            publicTorrent: [NSNumber numberWithBool: NO]
            downloadFolder: location
            useIncompleteFolder: nil incompleteFolder: nil
            ratioSetting: nil ratioLimit: nil
            waitToStart: nil orderValue: nil groupValue: nil addedTrackers: nil];
    
    return self;
}

- (id) initWithHistory: (NSDictionary *) history lib: (tr_handle *) lib
{
    self = [self initWithHash: [history objectForKey: @"TorrentHash"]
                path: [history objectForKey: @"TorrentPath"] torrentStruct: NULL lib: lib
                publicTorrent: [history objectForKey: @"PublicCopy"]
                downloadFolder: [history objectForKey: @"DownloadFolder"]
                useIncompleteFolder: [history objectForKey: @"UseIncompleteFolder"]
                incompleteFolder: [history objectForKey: @"IncompleteFolder"]
                ratioSetting: [history objectForKey: @"RatioSetting"]
                ratioLimit: [history objectForKey: @"RatioLimit"]
                waitToStart: [history objectForKey: @"WaitToStart"]
                orderValue: [history objectForKey: @"OrderValue"]
                groupValue: [history objectForKey: @"GroupValue"]
                addedTrackers: [history objectForKey: @"AddedTrackers"]];
    
    if (self)
    {
        //start transfer
        NSNumber * active;
        if ((active = [history objectForKey: @"Active"]) && [active boolValue])
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
    }
    return self;
}

- (NSDictionary *) history
{
    NSMutableDictionary * history = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                    [NSNumber numberWithBool: fPublicTorrent], @"PublicCopy",
                    [self hashString], @"TorrentHash",
                    fDownloadFolder, @"DownloadFolder",
                    [NSNumber numberWithBool: fUseIncompleteFolder], @"UseIncompleteFolder",
                    [NSNumber numberWithBool: [self isActive]], @"Active",
                    [NSNumber numberWithInt: fRatioSetting], @"RatioSetting",
                    [NSNumber numberWithFloat: fRatioLimit], @"RatioLimit",
                    [NSNumber numberWithBool: fWaitToStart], @"WaitToStart",
                    [NSNumber numberWithInt: fOrderValue], @"OrderValue",
                    [NSNumber numberWithInt: fGroupValue], @"GroupValue",
                    [NSNumber numberWithBool: fAddedTrackers], @"AddedTrackers", nil];
    
    if (fIncompleteFolder)
        [history setObject: fIncompleteFolder forKey: @"IncompleteFolder"];

    if (fPublicTorrent)
        [history setObject: [self publicTorrentLocation] forKey: @"TorrentPath"];
	
    return history;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    if (fFileStat)
        tr_torrentFilesFree(fFileStat, [self fileCount]);
    
    [fPreviousFinishedIndexes release];
    [fPreviousFinishedIndexesDate release];
    
    [fNameString release];
    [fHashString release];
    
    [fDownloadFolder release];
    [fIncompleteFolder release];
    
    [fPublicTorrentLocation release];
    
    [fIcon release];
    
    [fFileList release];
    
    [fQuickPauseDict release];
    
    [super dealloc];
}

- (NSString *) description
{
    return [@"Torrent: " stringByAppendingString: [self name]];
}

- (void) closeRemoveTorrent
{
    //allow the file to be index by Time Machine
    [self setTimeMachineExclude: NO forPath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]];
    
    tr_torrentRemove(fHandle);
}

- (void) changeIncompleteDownloadFolder: (NSString *) folder
{
    fUseIncompleteFolder = folder != nil;
    
    [fIncompleteFolder release];
    fIncompleteFolder = fUseIncompleteFolder ? [folder retain] : nil;
    
    [self updateDownloadFolder];
}

- (void) changeDownloadFolder: (NSString *) folder
{
    [fDownloadFolder release];
    fDownloadFolder = [folder retain];
    
    [self updateDownloadFolder];
}

- (NSString *) downloadFolder
{
    return [NSString stringWithUTF8String: tr_torrentGetDownloadDir(fHandle)];
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
    
    //check to stop for ratio
    CGFloat stopRatio;
    if ([self isSeeding] && (stopRatio = [self actualStopRatio]) != INVALID && [self ratio] >= stopRatio)
    {
        [self setRatioSetting: NSOffState];
        [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentStoppedForRatio" object: self];
        
        [self stopTransfer];
        fStat = tr_torrentStat(fHandle);
        
        fFinishedSeeding = YES;
    }
    
    //check if stalled (stored because based on time and needs to check if it was previously stalled)
    fStalled = [self isActive] && [fDefaults boolForKey: @"CheckStalled"]
                && [self stalledMinutes] > [fDefaults integerForKey: @"StalledMinutes"];
    
    //update queue for checking (from downloading to seeding), stalled, or error
    if ((wasChecking && ![self isChecking]) || (wasStalled != fStalled) || (!wasError && [self isError] && [self isActive]))
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
}

- (void) startTransfer
{
    fWaitToStart = NO;
    fFinishedSeeding = NO;
    
    if (![self isActive] && [self alertForFolderAvailable] && [self alertForRemainingDiskSpace])
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

- (CGFloat) ratio
{
    return fStat->ratio;
}

- (NSInteger) ratioSetting
{
    return fRatioSetting;
}

- (void) setRatioSetting: (NSInteger) setting
{
    fRatioSetting = setting;
}

- (CGFloat) ratioLimit
{
    return fRatioLimit;
}

- (void) setRatioLimit: (CGFloat) limit
{
    if (limit >= 0)
        fRatioLimit = limit;
}

- (CGFloat) actualStopRatio
{
    if (fRatioSetting == NSOnState)
        return fRatioLimit;
    else if (fRatioSetting == NSMixedState && [fDefaults boolForKey: @"RatioCheck"])
        return [fDefaults floatForKey: @"RatioLimit"];
    else
        return INVALID;
}

- (CGFloat) progressStopRatio
{
    CGFloat stopRatio, ratio;
    if ((stopRatio = [self actualStopRatio]) == INVALID || (ratio = [self ratio]) >= stopRatio)
        return 1.0;
    else if (stopRatio > 0.0)
        return ratio / stopRatio;
    else
        return 0.0;
}

- (tr_speedlimit) speedMode: (BOOL) upload
{
    return tr_torrentGetSpeedMode(fHandle, upload ? TR_UP : TR_DOWN);
}

- (void) setSpeedMode: (tr_speedlimit) mode upload: (BOOL) upload
{
    tr_torrentSetSpeedMode(fHandle, upload ? TR_UP : TR_DOWN, mode);
}

- (NSInteger) speedLimit: (BOOL) upload
{
    return tr_torrentGetSpeedLimit(fHandle, upload ? TR_UP : TR_DOWN);
}

- (void) setSpeedLimit: (NSInteger) limit upload: (BOOL) upload
{
    tr_torrentSetSpeedLimit(fHandle, upload ? TR_UP : TR_DOWN, limit);
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

- (void) revealData
{
    [[NSWorkspace sharedWorkspace] selectFile: [self dataLocation] inFileViewerRootedAtPath: nil];
}

- (void) revealPublicTorrent
{
    if (fPublicTorrent)
        [[NSWorkspace sharedWorkspace] selectFile: fPublicTorrentLocation inFileViewerRootedAtPath: nil];
}

- (void) trashData
{
    [self trashFile: [self dataLocation]];
}

- (void) trashTorrent
{
    if (fPublicTorrent)
    {
        [self trashFile: fPublicTorrentLocation];
        [fPublicTorrentLocation release];
        fPublicTorrentLocation = nil;
        
        fPublicTorrent = NO;
    }
}

- (void) moveTorrentDataFileTo: (NSString *) folder
{
    NSString * oldFolder = [self downloadFolder];
    if (![oldFolder isEqualToString: folder] || ![fDownloadFolder isEqualToString: folder])
    {
        //check if moving inside itself
        NSArray * oldComponents = [oldFolder pathComponents],
                * newComponents = [folder pathComponents];
        NSInteger count;
        
        if ((count = [oldComponents count]) < [newComponents count]
                && [[newComponents objectAtIndex: count] isEqualToString: [self name]]
                && [oldComponents isEqualToArray:
                        [newComponents objectsAtIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, count)]]])
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
        
        [self quickPause];
        
        //allow if file can be moved or does not exist
        if ([[NSFileManager defaultManager] movePath: [oldFolder stringByAppendingPathComponent: [self name]]
                            toPath: [folder stringByAppendingPathComponent: [self name]] handler: nil]
            || ![[NSFileManager defaultManager] fileExistsAtPath: [oldFolder stringByAppendingPathComponent: [self name]]])
        {
            //get rid of both incomplete folder and old download folder, even if move failed
            fUseIncompleteFolder = NO;
            if (fIncompleteFolder)
            {
                [fIncompleteFolder release];
                fIncompleteFolder = nil;
            }
            [self changeDownloadFolder: folder];
            
            [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateStats" object: nil];
            
            [self endQuickPause];
        }
        else
        {
            [self endQuickPause];
        
            NSAlert * alert = [[NSAlert alloc] init];
            [alert setMessageText: NSLocalizedString(@"There was an error moving the data file.", "Move error alert -> title")];
            [alert setInformativeText: [NSString stringWithFormat:
                            NSLocalizedString(@"The move operation of \"%@\" cannot be done.",
                                                "Move error alert -> message"), [self name]]];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Move error alert -> button")];
            
            [alert runModal];
            [alert release];
        }
    }
}

- (void) copyTorrentFileTo: (NSString *) path
{
    [[NSFileManager defaultManager] copyPath: [self torrentLocation] toPath: path handler: nil];
}

- (BOOL) alertForRemainingDiskSpace
{
    if ([self allDownloaded] || ![fDefaults boolForKey: @"WarningRemainingSpace"])
        return YES;
    
    NSFileManager * fileManager = [NSFileManager defaultManager];
    NSString * downloadFolder = [self downloadFolder];
    
    NSString * volumeName;
    if ((volumeName = [[fileManager componentsToDisplayForPath: downloadFolder] objectAtIndex: 0]))
    {
        BOOL onLeopard = [NSApp isOnLeopardOrBetter];
        
        NSDictionary * systemAttributes = onLeopard ? [fileManager attributesOfFileSystemForPath: downloadFolder error: NULL]
                                            : [fileManager fileSystemAttributesAtPath: downloadFolder];
        uint64_t remainingSpace = [[systemAttributes objectForKey: NSFileSystemFreeSize] unsignedLongLongValue], neededSpace = 0;
        
        //if the size left is less then remaining space, then there is enough space regardless of preallocation
        if (remainingSpace < [self sizeLeft])
        {
            [self updateFileStat];
            
            //determine amount needed
            for (NSInteger i = 0; i < [self fileCount]; i++)
            {
                if (tr_torrentGetFileDL(fHandle, i))
                {
                    tr_file * file = &fInfo->files[i];
                    
                    neededSpace += file->length;
                    
                    NSString * path = [downloadFolder stringByAppendingPathComponent: [NSString stringWithUTF8String: file->name]];
                    NSDictionary * fileAttributes = onLeopard ? [fileManager attributesOfItemAtPath: path error: NULL]
                                                        : [fileManager fileAttributesAtPath: path traverseLink: NO];
                    if (fileAttributes)
                    {
                        unsigned long long fileSize = [[fileAttributes objectForKey: NSFileSize] unsignedLongLongValue];
                        if (fileSize < neededSpace)
                            neededSpace -= fileSize;
                        else
                            neededSpace = 0;
                    }
                }
            }
            
            if (remainingSpace < neededSpace)
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
                
                if (onLeopard)
                {
                    [alert setShowsSuppressionButton: YES];
                    [[alert suppressionButton] setTitle: NSLocalizedString(@"Do not check disk space again",
                                                            "Torrent disk space alert -> button")];
                }
                else
                    [alert addButtonWithTitle: NSLocalizedString(@"Always Download", "Torrent disk space alert -> button")];

                NSInteger result = [alert runModal];
                if ((onLeopard ? [[alert suppressionButton] state] == NSOnState : result == NSAlertThirdButtonReturn))
                    [fDefaults setBool: NO forKey: @"WarningRemainingSpace"];
                [alert release];
                
                return result != NSAlertFirstButtonReturn;
            }
        }
    }
    return YES;
}

- (BOOL) alertForFolderAvailable
{
    #warning check for change from incomplete to download folder first
    if (access(tr_torrentGetDownloadDir(fHandle), 0))
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: [NSString stringWithFormat:
                                NSLocalizedString(@"The folder for downloading \"%@\" cannot be used.",
                                    "Folder cannot be used alert -> title"), [self name]]];
        [alert setInformativeText: [NSString stringWithFormat:
                        NSLocalizedString(@"\"%@\" cannot be used. The transfer will be paused.",
                                            "Folder cannot be used alert -> message"), [self downloadFolder]]];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Folder cannot be used alert -> button")];
        [alert addButtonWithTitle: [NSLocalizedString(@"Choose New Location",
                                    "Folder cannot be used alert -> location button") stringByAppendingEllipsis]];
        
        if ([alert runModal] != NSAlertFirstButtonReturn)
        {
            NSOpenPanel * panel = [NSOpenPanel openPanel];
            
            [panel setPrompt: NSLocalizedString(@"Select", "Folder cannot be used alert -> prompt")];
            [panel setAllowsMultipleSelection: NO];
            [panel setCanChooseFiles: NO];
            [panel setCanChooseDirectories: YES];
            [panel setCanCreateDirectories: YES];

            [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the download folder for \"%@\"",
                                "Folder cannot be used alert -> select destination folder"), [self name]]];
            
            [[NSNotificationCenter defaultCenter] postNotificationName: @"MakeWindowKey" object: nil];
            [panel beginSheetForDirectory: nil file: nil types: nil modalForWindow: [NSApp keyWindow] modalDelegate: self
                    didEndSelector: @selector(destinationChoiceClosed:returnCode:contextInfo:) contextInfo: nil];
        }
        
        [alert release];
        
        return NO;
    }
    return YES;
}

- (void) destinationChoiceClosed: (NSOpenPanel *) openPanel returnCode: (NSInteger) code contextInfo: (void *) context
{
    if (code != NSOKButton)
        return;
    
    [self changeDownloadFolder: [[openPanel filenames] objectAtIndex: 0]];
    
    [self startTransfer];
    [self update];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateStats" object: nil];
}

- (BOOL) alertForMoveFolderAvailable
{
    if (access([fDownloadFolder UTF8String], 0))
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: [NSString stringWithFormat:
                                NSLocalizedString(@"The folder for moving the completed \"%@\" cannot be used.",
                                    "Move folder cannot be used alert -> title"), [self name]]];
        [alert setInformativeText: [NSString stringWithFormat:
                                NSLocalizedString(@"\"%@\" cannot be used. The file will remain in its current location.",
                                    "Move folder cannot be used alert -> message"), fDownloadFolder]];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Move folder cannot be used alert -> button")];
        
        [alert runModal];
        [alert release];
        
        return NO;
    }
    
    return YES;
}

- (NSImage *) icon
{
    if (!fIcon)
    {
        fIcon = [[[NSWorkspace sharedWorkspace] iconForFileType: [self isFolder] ? NSFileTypeForHFSTypeCode('fldr')
                                                : [[self name] pathExtension]] retain];
        [fIcon setFlipped: YES];
    }
    return fIcon;
}

- (NSString *) name
{
    return fNameString;
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

- (NSString *) trackerAddressAnnounce
{
    return fStat->announceURL ? [NSString stringWithUTF8String: fStat->announceURL] : nil;
}

- (NSDate *) lastAnnounceTime
{
    NSInteger date = fStat->lastAnnounceTime;
    return date > 0 ? [NSDate dateWithTimeIntervalSince1970: date] : nil;
}

- (NSInteger) nextAnnounceTime
{
    NSInteger date = fStat->nextAnnounceTime;
    NSTimeInterval difference;
    switch (date)
    {
        case 0:
            return STAT_TIME_NONE;
        case 1:
            return STAT_TIME_NOW;
        default:
            difference = [[NSDate dateWithTimeIntervalSince1970: date] timeIntervalSinceNow];
            return difference > 0 ? (NSInteger)difference : STAT_TIME_NONE;
    }
}

- (NSString *) announceResponse
{
    return [NSString stringWithUTF8String: fStat->announceResponse];
}

- (NSString *) trackerAddressScrape
{
    return fStat->scrapeURL ? [NSString stringWithUTF8String: fStat->scrapeURL] : nil;
}

- (NSDate *) lastScrapeTime
{
    NSInteger date = fStat->lastScrapeTime;
    return date > 0 ? [NSDate dateWithTimeIntervalSince1970: date] : nil;
}

- (NSInteger) nextScrapeTime
{
    NSInteger date = fStat->nextScrapeTime;
    NSTimeInterval difference;
    switch (date)
    {
        case 0:
            return STAT_TIME_NONE;
        case 1:
            return STAT_TIME_NOW;
        default:
            difference = [[NSDate dateWithTimeIntervalSince1970: date] timeIntervalSinceNow];
            return difference > 0 ? (NSInteger)difference : STAT_TIME_NONE;
    }
}

- (NSString *) scrapeResponse
{
    return [NSString stringWithUTF8String: fStat->scrapeResponse];
}

- (NSMutableArray *) allTrackers: (BOOL) separators
{
    NSInteger count = fInfo->trackerCount, capacity = count;
    if (separators)
        capacity += fInfo->trackers[count-1].tier + 1;
    NSMutableArray * allTrackers = [NSMutableArray arrayWithCapacity: capacity];
    
    for (NSInteger i = 0, tier = -1; i < count; i++)
    {
        if (separators && tier != fInfo->trackers[i].tier)
        {
            tier = fInfo->trackers[i].tier;
            [allTrackers addObject: [NSNumber numberWithInt: fAddedTrackers ? tier : tier + 1]];
        }
        
        [allTrackers addObject: [NSString stringWithUTF8String: fInfo->trackers[i].announce]];
    }
    
    return allTrackers;
}

- (BOOL) updateAllTrackersForAdd: (NSMutableArray *) trackers
{
    //find added tracker at end of first tier
    NSInteger i;
    for (i = 1; i < [trackers count]; i++)
        if ([[trackers objectAtIndex: i] isKindOfClass: [NSNumber class]])
            break;
    i--;
    
    NSString * tracker = [trackers objectAtIndex: i];
    
    tracker = [tracker stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    
    if ([tracker rangeOfString: @"://"].location == NSNotFound)
    {
        tracker = [@"http://" stringByAppendingString: tracker];
        [trackers replaceObjectAtIndex: i withObject: tracker];
    }
    
    if (!tr_httpIsValidURL([tracker UTF8String]))
        return NO;
    
    [self updateAllTrackers: trackers];
    
    fAddedTrackers = YES;
    return YES;
}

- (void) updateAllTrackersForRemove: (NSMutableArray *) trackers
{
    //check if no user-added groups
    if ([[trackers objectAtIndex: 0] intValue] != 0)
        fAddedTrackers = NO;
    
    [self updateAllTrackers: trackers];
}

- (BOOL) hasAddedTrackers
{
    return fAddedTrackers;
}

- (NSString *) comment
{
    return [NSString stringWithUTF8String: fInfo->comment];
}

- (NSString *) creator
{
    return [NSString stringWithUTF8String: fInfo->creator];
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
    return [NSString stringWithUTF8String: fInfo->torrent];
}

- (NSString *) publicTorrentLocation
{
    return fPublicTorrentLocation;
}

- (NSString *) dataLocation
{
    return [[self downloadFolder] stringByAppendingPathComponent: [self name]];
}

- (BOOL) publicTorrent
{
    return fPublicTorrent;
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

- (NSInteger) etaRatio
{
    if (![self isSeeding])
        return TR_ETA_UNKNOWN;
    
    CGFloat uploadRate = [self uploadRate];
    if (uploadRate < 0.1)
        return TR_ETA_UNKNOWN;
    
    CGFloat stopRatio = [self actualStopRatio], ratio = [self ratio];
    if (stopRatio == INVALID || ratio >= stopRatio)
        return TR_ETA_UNKNOWN;
    
    return (CGFloat)MAX([self downloadedTotal], [self haveTotal]) * (stopRatio - ratio) / uploadRate / 1024.0;
}

- (CGFloat) notAvailableDesired
{
    return 1.0 - (CGFloat)fStat->desiredAvailable / [self sizeLeft];
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
    return [self progressDone] >= 1.0;
}

- (BOOL) isComplete
{
    return [self progress] >= 1.0;
}

- (BOOL) isError
{
    return fStat->error != TR_OK;
}

- (NSString *) errorMessage
{
    if (![self isError])
        return @"";
    
    NSString * error;
    if (!(error = [NSString stringWithUTF8String: fStat->errorString])
        && !(error = [NSString stringWithCString: fStat->errorString encoding: NSISOLatin1StringEncoding]))
        error = [NSString stringWithFormat: @"(%@)", NSLocalizedString(@"unreadable error", "Torrent -> error string unreadable")];
    
    return error;
}

- (NSArray *) peers
{
    NSInteger totalPeers;
    tr_peer_stat * peers = tr_torrentPeers(fHandle, &totalPeers);
    
    NSMutableArray * peerDicts = [NSMutableArray arrayWithCapacity: totalPeers];
    
    for (NSInteger i = 0; i < totalPeers; i++)
    {
        tr_peer_stat * peer = &peers[i];
        NSMutableDictionary * dict = [NSMutableDictionary dictionaryWithCapacity: 9];
        
        [dict setObject: [NSNumber numberWithInt: peer->from] forKey: @"From"];
        [dict setObject: [NSString stringWithUTF8String: peer->addr] forKey: @"IP"];
        [dict setObject: [NSNumber numberWithInt: peer->port] forKey: @"Port"];
        [dict setObject: [NSNumber numberWithFloat: peer->progress] forKey: @"Progress"];
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
    if (fStat->activity == TR_STATUS_DOWNLOAD || ([self isSeeding]
        && (fRatioSetting == NSOnState || (fRatioSetting == NSMixedState && [fDefaults boolForKey: @"RatioCheck"]))))
    {
        NSInteger eta = fStat->activity == TR_STATUS_DOWNLOAD ? [self eta] : [self etaRatio];
        string = [string stringByAppendingFormat: @" - %@", [self etaString: eta]];
    }
    
    return string;
}

- (NSString *) statusString
{
    NSString * string;
    
    if ([self isError])
    {
        string = NSLocalizedString(@"Error", "Torrent -> status string");
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
                string = [NSString localizedStringWithFormat: NSLocalizedString(@"Checking existing data (%.2f%%)",
                                        "Torrent -> status string"), 100.0 * [self checkingProgress]];
                break;

            case TR_STATUS_DOWNLOAD:
                if ([self totalPeersConnected] != 1)
                    string = [NSString stringWithFormat: NSLocalizedString(@"Downloading from %d of %d peers",
                                                    "Torrent -> status string"), [self peersSendingToUs], [self totalPeersConnected]];
                else
                    string = [NSString stringWithFormat: NSLocalizedString(@"Downloading from %d of 1 peer",
                                                    "Torrent -> status string"), [self peersSendingToUs]];
                
                NSInteger webSeedCount = fStat->webseedsSendingToUs;
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
            string = [NSString localizedStringWithFormat: NSLocalizedString(@"Checking existing data (%.2f%%)",
                                    "Torrent -> status string"), 100.0 * [self checkingProgress]];
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
    if (![self isActive] || ([self isSeeding]
        && !(fRatioSetting == NSOnState || (fRatioSetting == NSMixedState && [fDefaults boolForKey: @"RatioCheck"]))))
        return [self shortStatusString];
    
    return [self etaString: [self isSeeding] ? [self etaRatio] : [self eta]];
}

- (NSString *) stateString
{
    switch (fStat->activity)
    {
        case TR_STATUS_STOPPED:
            return NSLocalizedString(@"Paused", "Torrent -> status string");

        case TR_STATUS_CHECK:
            return [NSString localizedStringWithFormat: NSLocalizedString(@"Checking existing data (%.2f%%)",
                                    "Torrent -> status string"), 100.0 * [self checkingProgress]];
        
        case TR_STATUS_CHECK_WAIT:
            return [NSLocalizedString(@"Waiting to check existing data", "Torrent -> status string") stringByAppendingEllipsis];

        case TR_STATUS_DOWNLOAD:
            return NSLocalizedString(@"Downloading", "Torrent -> status string");

        case TR_STATUS_SEED:
            return NSLocalizedString(@"Seeding", "Torrent -> status string");
    }
}

- (NSInteger) seeders
{
    return fStat->seeders;
}

- (NSInteger) leechers
{
    return fStat->leechers;
}

- (NSInteger) completedFromTracker
{
    return fStat->timesCompleted;
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
    return fStat->peersFrom[TR_PEER_FROM_CACHE];
}

- (NSInteger) totalPeersPex
{
    return fStat->peersFrom[TR_PEER_FROM_PEX];
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
    return fStat->rateDownload;
}

- (CGFloat) uploadRate
{
    return fStat->rateUpload;
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

- (CGFloat) swarmSpeed
{
    return fStat->swarmSpeed;
}

- (NSInteger) orderValue
{
    return fOrderValue;
}

- (void) setOrderValue: (NSInteger) orderValue
{
    fOrderValue = orderValue;
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
    if (fGroupValue != -1 && [[[notification userInfo] objectForKey: @"Indexes"] containsIndex: fGroupValue])
        fGroupValue = -1;
}

- (NSArray *) fileList
{
    return fFileList;
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
    if ([self isComplete])
        return 1.0;
    
    if (!fFileStat)
        [self updateFileStat];
    
    NSIndexSet * indexSet = [node indexes];
    
    if ([indexSet count] == 1)
        return fFileStat[[indexSet firstIndex]].progress;
    
    uint64_t have = 0;
    for (NSInteger index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
        have += fFileStat[index].bytesCompleted;
    
    NSAssert([node size], @"director in torrent file has size 0");
    return (CGFloat)have / [node size];
}

- (BOOL) canChangeDownloadCheckForFile: (NSInteger) index
{
    if (!fFileStat)
        [self updateFileStat];
    
    return [self fileCount] > 1 && fFileStat[index].progress < 1.0;
}

- (BOOL) canChangeDownloadCheckForFiles: (NSIndexSet *) indexSet
{
    if ([self fileCount] <= 1 || [self isComplete])
        return NO;
    
    if (!fFileStat)
        [self updateFileStat];
    
    for (NSInteger index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
        if (fFileStat[index].progress < 1.0)
            return YES;
    return NO;
}

- (NSInteger) checkForFiles: (NSIndexSet *) indexSet
{
    BOOL onState = NO, offState = NO;
    for (NSInteger index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
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

- (void) setFilePriority: (NSInteger) priority forIndexes: (NSIndexSet *) indexSet
{
    const NSUInteger count = [indexSet count];
    tr_file_index_t * files = malloc(count * sizeof(tr_file_index_t));
    for (NSUInteger index = [indexSet firstIndex], i = 0; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index], i++)
        files[i] = index;
    
    tr_torrentSetFilePriorities(fHandle, files, count, priority);
    free(files);
}

- (BOOL) hasFilePriority: (NSInteger) priority forIndexes: (NSIndexSet *) indexSet
{
    for (NSInteger index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
        if (priority == tr_torrentGetFilePriority(fHandle, index) && [self canChangeDownloadCheckForFile: index])
            return YES;
    return NO;
}

- (NSSet *) filePrioritiesForIndexes: (NSIndexSet *) indexSet
{
    BOOL low = NO, normal = NO, high = NO;
    NSMutableSet * priorities = [NSMutableSet setWithCapacity: 3];
    
    for (NSInteger index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
    {
        if (![self canChangeDownloadCheckForFile: index])
            continue;
        
        NSInteger priority = tr_torrentGetFilePriority(fHandle, index);
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
        
        [priorities addObject: [NSNumber numberWithInt: priority]];
        if (low && normal && high)
            break;
    }
    return priorities;
}

- (NSDate *) dateAdded
{
    time_t date = fStat->addedDate;
    return [NSDate dateWithTimeIntervalSince1970: date];
}

- (NSDate *) dateCompleted
{
    time_t date = fStat->doneDate;
    return date != 0 ? [NSDate dateWithTimeIntervalSince1970: date] : nil;
}

- (NSDate *) dateActivity
{
    time_t date = fStat->activityDate;
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

- (NSInteger) stateSortKey
{
    if (![self isActive]) //paused
        return 0;
    else if ([self isSeeding]) //seeding
        return 1;
    else //downloading
        return 2;
}

- (tr_torrent *) torrentStruct
{
    return fHandle;
}

@end

@implementation Torrent (Private)

//if a hash is given, attempt to load that; otherwise, attempt to open file at path
- (id) initWithHash: (NSString *) hashString path: (NSString *) path torrentStruct: (tr_torrent *) torrentStruct lib: (tr_handle *) lib
        publicTorrent: (NSNumber *) publicTorrent
        downloadFolder: (NSString *) downloadFolder
        useIncompleteFolder: (NSNumber *) useIncompleteFolder incompleteFolder: (NSString *) incompleteFolder
        ratioSetting: (NSNumber *) ratioSetting ratioLimit: (NSNumber *) ratioLimit
        waitToStart: (NSNumber *) waitToStart
        orderValue: (NSNumber *) orderValue groupValue: (NSNumber *) groupValue addedTrackers: (NSNumber *) addedTrackers
{
    if (!(self = [super init]))
        return nil;
    
    fDefaults = [NSUserDefaults standardUserDefaults];

    fPublicTorrent = path && (publicTorrent ? [publicTorrent boolValue] : ![fDefaults boolForKey: @"DeleteOriginalTorrent"]);
    if (fPublicTorrent)
        fPublicTorrentLocation = [path retain];
    
    fDownloadFolder = downloadFolder ? downloadFolder : [fDefaults stringForKey: @"DownloadFolder"];
    fDownloadFolder = [[fDownloadFolder stringByExpandingTildeInPath] retain];
    
    fUseIncompleteFolder = useIncompleteFolder ? [useIncompleteFolder boolValue]
                                : [fDefaults boolForKey: @"UseIncompleteDownloadFolder"];
    if (fUseIncompleteFolder)
    {
        fIncompleteFolder = incompleteFolder ? incompleteFolder : [fDefaults stringForKey: @"IncompleteDownloadFolder"];
        fIncompleteFolder = [[fIncompleteFolder stringByExpandingTildeInPath] retain];
    }
    
    if (torrentStruct)
    {
        fHandle = torrentStruct;
        fInfo = tr_torrentInfo(fHandle);
        
        NSString * currentDownloadFolder = [self shouldUseIncompleteFolderForName: [NSString stringWithUTF8String: fInfo->name]]
                                                ? fIncompleteFolder : fDownloadFolder;
        tr_torrentSetDownloadDir(fHandle, [currentDownloadFolder UTF8String]);
    }
    else
    {
        //set libtransmission settings for initialization
        tr_ctor * ctor = tr_ctorNew(lib);
        tr_ctorSetPaused(ctor, TR_FORCE, YES);
        tr_ctorSetPeerLimit(ctor, TR_FALLBACK, [fDefaults integerForKey: @"PeersTorrent"]);
        
        tr_info info;
        if (hashString)
        {
            tr_ctorSetMetainfoFromHash(ctor, [hashString UTF8String]);
            if (tr_torrentParse(lib, ctor, &info) == TR_OK)
            {
                NSString * currentDownloadFolder = [self shouldUseIncompleteFolderForName: [NSString stringWithUTF8String: info.name]]
                                                    ? fIncompleteFolder : fDownloadFolder;
                tr_ctorSetDownloadDir(ctor, TR_FORCE, [currentDownloadFolder UTF8String]);
                
                fHandle = tr_torrentNew(lib, ctor, NULL);
            }
            tr_metainfoFree(&info);
        }
        if (!fHandle && path)
        {
            tr_ctorSetMetainfoFromFile(ctor, [path UTF8String]);
            if (tr_torrentParse(lib, ctor, &info) == TR_OK)
            {
                NSString * currentDownloadFolder = [self shouldUseIncompleteFolderForName: [NSString stringWithUTF8String: info.name]]
                                                    ? fIncompleteFolder : fDownloadFolder;
                tr_ctorSetDownloadDir(ctor, TR_FORCE, [currentDownloadFolder UTF8String]);
                
                fHandle = tr_torrentNew(lib, ctor, NULL);
            }
            tr_metainfoFree(&info);
        }
        
        tr_ctorFree(ctor);
        
        if (!fHandle)
        {
            [self release];
            return nil;
        }
        
        fInfo = tr_torrentInfo(fHandle);
    }
    
    tr_torrentSetCompletenessCallback(fHandle, completenessChangeCallback, self);
    
    fNameString = [[NSString alloc] initWithUTF8String: fInfo->name];
    fHashString = [[NSString alloc] initWithUTF8String: fInfo->hashString];
	
    fRatioSetting = ratioSetting ? [ratioSetting intValue] : NSMixedState;
    fRatioLimit = ratioLimit ? [ratioLimit floatValue] : [fDefaults floatForKey: @"RatioLimit"];
    fFinishedSeeding = NO;
    
    fWaitToStart = waitToStart && [waitToStart boolValue];
    fResumeOnWake = NO;
    
    fOrderValue = orderValue ? [orderValue intValue] : tr_sessionCountTorrents(lib) - 1;
    fGroupValue = groupValue ? [groupValue intValue] : -1;
    
    fAddedTrackers = addedTrackers ? [addedTrackers boolValue] : NO; 
    
    [self createFileList];
    
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(checkGroupValueForRemoval:)
        name: @"GroupValueRemoved" object: nil];
    
    [self update];
    
    //mark incomplete files to be ignored by Time Machine
    [self setTimeMachineExclude: ![self allDownloaded] forPath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]];
    
    return self;
}

- (void) createFileList
{
    if ([self isFolder])
    {
        NSInteger count = [self fileCount];
        NSMutableArray * fileList = [[NSMutableArray alloc] initWithCapacity: count];
        
        for (NSInteger i = 0; i < count; i++)
        {
            tr_file * file = &fInfo->files[i];
            
            NSMutableArray * pathComponents = [[[NSString stringWithUTF8String: file->name] pathComponents] mutableCopy];
            NSString * path = [pathComponents objectAtIndex: 0];
            NSString * name = [pathComponents objectAtIndex: 1];
            [pathComponents removeObjectsAtIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, 2)]];
            
            if ([pathComponents count] > 0)
            {
                //determine if folder node already exists
                NSEnumerator * enumerator = [fileList objectEnumerator];
                FileListNode * node;
                while ((node = [enumerator nextObject]))
                    if ([[node name] isEqualToString: name] && [node isFolder])
                        break;
                
                if (!node)
                {
                    node = [[FileListNode alloc] initWithFolderName: name path: path];
                    [fileList addObject: node];
                    [node release];
                }
                
                [node insertIndex: i withSize: file->length];
                [self insertPath: pathComponents forParent: node fileSize: file->length index: i];
            }
            else
            {
                FileListNode * node = [[FileListNode alloc] initWithFileName: name path: path size: file->length index: i];
                [fileList addObject: node];
                [node release];
            }
            
            [pathComponents release];
        }
        
        fFileList = [[NSArray alloc] initWithArray: fileList];
        [fileList release];
    }
    else
    {
        FileListNode * node = [[FileListNode alloc] initWithFileName: [self name] path: @"" size: [self size] index: 0];
        fFileList = [[NSArray arrayWithObject: node] retain];
        [node release];
    }
}

- (void) insertPath: (NSMutableArray *) components forParent: (FileListNode *) parent fileSize: (uint64_t) size index: (NSInteger) index
{
    NSString * name = [components objectAtIndex: 0];
    BOOL isFolder = [components count] > 1;
    
    FileListNode * node = nil;
    if (isFolder)
    {
        NSEnumerator * enumerator = [[parent children] objectEnumerator];
        while ((node = [enumerator nextObject]))
            if ([[node name] isEqualToString: name] && [node isFolder])
                break;
    }
    
    //create new folder or file if it doesn't already exist
    if (!node)
    {
        if (isFolder)
            node = [[FileListNode alloc] initWithFolderName: name path: [parent fullPath]];
        else
            node = [[FileListNode alloc] initWithFileName: name path: [parent fullPath] size: size index: index];
        
        [parent insertChild: node];
    }
    
    if (isFolder)
    {
        [node insertIndex: index withSize: size];
        
        [components removeObjectAtIndex: 0];
        [self insertPath: components forParent: node fileSize: size index: index];
    }
}

- (BOOL) shouldUseIncompleteFolderForName: (NSString *) name
{
    return fUseIncompleteFolder &&
        ![[NSFileManager defaultManager] fileExistsAtPath: [fDownloadFolder stringByAppendingPathComponent: name]];
}

- (void) updateDownloadFolder
{
    //remove old Time Machine location
    [self setTimeMachineExclude: NO forPath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]];
    
    NSString * folder = [self shouldUseIncompleteFolderForName: [self name]] ? fIncompleteFolder : fDownloadFolder;
    tr_torrentSetDownloadDir(fHandle, [folder UTF8String]);
    
    [self setTimeMachineExclude: ![self allDownloaded] forPath: [folder stringByAppendingPathComponent: [self name]]];
}

//status has been retained
- (void) completenessChange: (NSNumber *) status
{
    fStat = tr_torrentStat(fHandle); //don't call update yet to avoid auto-stop
    
    BOOL canMove;
    switch ([status intValue])
    {
        case TR_CP_DONE:
        case TR_CP_COMPLETE:
            canMove = YES;
            
            //move file from incomplete folder to download folder
            if (fUseIncompleteFolder && ![[self downloadFolder] isEqualToString: fDownloadFolder]
                && (canMove = [self alertForMoveFolderAvailable]))
            {
                [self quickPause];
                
                if ([[NSFileManager defaultManager] movePath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]
                                        toPath: [fDownloadFolder stringByAppendingPathComponent: [self name]] handler: nil])
                    [self updateDownloadFolder];
                else
                    canMove = NO;
                
                [self endQuickPause];
            }
            
            if (!canMove)
            {
                fUseIncompleteFolder = NO;
                
                [fDownloadFolder release];
                fDownloadFolder = fIncompleteFolder;
                fIncompleteFolder = nil;
            }
            
            //allow to be backed up by Time Machine
            [self setTimeMachineExclude: NO forPath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]];
            
            [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentFinishedDownloading" object: self];
            break;
        
        case TR_CP_INCOMPLETE:
            //do not allow to be backed up by Time Machine
            [self setTimeMachineExclude: YES forPath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]];
            
            [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentRestartedDownloading" object: self];
            break;
    }
    [status release];
    
    [self update];
} 

- (void) quickPause
{
    if (fQuickPauseDict)
        return;

    fQuickPauseDict = [[NSDictionary alloc] initWithObjectsAndKeys:
                    [NSNumber numberWithInt: [self speedMode: YES]], @"UploadSpeedMode",
                    [NSNumber numberWithInt: [self speedLimit: YES]], @"UploadSpeedLimit",
                    [NSNumber numberWithInt: [self speedMode: NO]], @"DownloadSpeedMode",
                    [NSNumber numberWithInt: [self speedLimit: NO]], @"DownloadSpeedLimit", nil];
    
    [self setSpeedMode: TR_SPEEDLIMIT_SINGLE upload: YES];
    [self setSpeedLimit: 0 upload: YES];
    [self setSpeedMode: TR_SPEEDLIMIT_SINGLE upload: NO];
    [self setSpeedLimit: 0 upload: NO];
}

- (void) endQuickPause
{
    if (!fQuickPauseDict)
        return;
    
    [self setSpeedMode: [[fQuickPauseDict objectForKey: @"UploadSpeedMode"] intValue] upload: YES];
    [self setSpeedLimit: [[fQuickPauseDict objectForKey: @"UploadSpeedLimit"] intValue] upload: YES];
    [self setSpeedMode: [[fQuickPauseDict objectForKey: @"DownloadSpeedMode"] intValue] upload: NO];
    [self setSpeedLimit: [[fQuickPauseDict objectForKey: @"DownloadSpeedLimit"] intValue] upload: NO];
    
    [fQuickPauseDict release];
    fQuickPauseDict = nil;
}

- (NSString *) etaString: (NSInteger) eta
{
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

- (void) updateAllTrackers: (NSMutableArray *) trackers
{
    //get count
    NSInteger count = 0;
    NSEnumerator * enumerator = [trackers objectEnumerator];
    id object;
    while ((object = [enumerator nextObject]))
        if (![object isKindOfClass: [NSNumber class]])
            count++;
    
    //recreate the tracker structure
    tr_tracker_info * trackerStructs = tr_new(tr_tracker_info, count);
    NSInteger tier = 0, i = 0;
    enumerator = [trackers objectEnumerator];
    while ((object = [enumerator nextObject]))
    {
        if (![object isKindOfClass: [NSNumber class]])
        {
            trackerStructs[i].tier = tier;
            trackerStructs[i].announce = (char *)[object UTF8String];
            i++;
        }
        else
            tier++;
    }
    
    tr_torrentSetAnnounceList(fHandle, trackerStructs, count);
    tr_free(trackerStructs);
}

- (void) trashFile: (NSString *) path
{
    //attempt to move to trash
    if (![[NSWorkspace sharedWorkspace] performFileOperation: NSWorkspaceRecycleOperation
        source: [path stringByDeletingLastPathComponent] destination: @""
        files: [NSArray arrayWithObject: [path lastPathComponent]] tag: nil])
    {
        //if cannot trash, just delete it (will work if it's on a remote volume)
        if ([NSApp isOnLeopardOrBetter])
        {
            NSError * error;
            if (![[NSFileManager defaultManager] removeItemAtPath: path error: &error])
                NSLog(@"Could not trash %@: %@", path, [error localizedDescription]);
        }
        else
        {
            if (![[NSFileManager defaultManager] removeFileAtPath: path handler: nil])
                NSLog(@"Could not trash %@", path);
        }
    }
}

- (void) setTimeMachineExclude: (BOOL) exclude forPath: (NSString *) path
{
    if ([NSApp isOnLeopardOrBetter])
        CSBackupSetItemExcluded((CFURLRef)[NSURL fileURLWithPath: path], exclude, true);
}

@end
