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
#import "GroupsWindowController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

static int static_lastid = 0;

@interface Torrent (Private)

- (id) initWithHash: (NSString *) hashString path: (NSString *) path data: (NSData *) data lib: (tr_handle *) lib
        publicTorrent: (NSNumber *) publicTorrent
        downloadFolder: (NSString *) downloadFolder
        useIncompleteFolder: (NSNumber *) useIncompleteFolder incompleteFolder: (NSString *) incompleteFolder
        dateAdded: (NSDate *) dateAdded dateCompleted: (NSDate *) dateCompleted
        dateActivity: (NSDate *) dateActivity
        ratioSetting: (NSNumber *) ratioSetting ratioLimit: (NSNumber *) ratioLimit
        waitToStart: (NSNumber *) waitToStart
        orderValue: (NSNumber *) orderValue groupValue: (NSNumber *) groupValue;

- (BOOL) shouldUseIncompleteFolderForName: (NSString *) name;
- (void) updateDownloadFolder;

- (void) createFileList;
- (void) insertPath: (NSMutableArray *) components forSiblings: (NSMutableArray *) siblings previousPath: (NSString *) previousPath
            fileSize: (uint64_t) size index: (int) index;

- (void) completenessChange: (NSNumber *) status;

- (void) quickPause;
- (void) endQuickPause;

- (void) trashFile: (NSString *) path;

- (void) setTimeMachineExclude: (BOOL) exclude forPath: (NSString *) path;

@end

void completenessChangeCallback(tr_torrent * torrent, cp_status_t status, void * torrentData)
{
    [(Torrent *)torrentData performSelectorOnMainThread: @selector(completenessChange:)
                withObject: [[NSNumber alloc] initWithInt: status] waitUntilDone: NO];
}

@implementation Torrent


- (id) initWithPath: (NSString *) path location: (NSString *) location deleteTorrentFile: (torrentFileState) torrentDelete
        lib: (tr_handle *) lib
{
    self = [self initWithHash: nil path: path data: nil lib: lib
            publicTorrent: torrentDelete != TORRENT_FILE_DEFAULT
                            ? [NSNumber numberWithBool: torrentDelete == TORRENT_FILE_SAVE] : nil
            downloadFolder: location
            useIncompleteFolder: nil incompleteFolder: nil
            dateAdded: nil dateCompleted: nil
            dateActivity: nil
            ratioSetting: nil ratioLimit: nil
            waitToStart: nil orderValue: nil groupValue: nil];
    
    if (self)
    {
        if (!fPublicTorrent)
            [self trashFile: path];
    }
    return self;
}

- (id) initWithData: (NSData *) data location: (NSString *) location lib: (tr_handle *) lib
{
    self = [self initWithHash: nil path: nil data: data lib: lib
            publicTorrent: nil
            downloadFolder: location
            useIncompleteFolder: nil incompleteFolder: nil
            dateAdded: nil dateCompleted: nil
            dateActivity: nil
            ratioSetting: nil ratioLimit: nil
            waitToStart: nil orderValue: nil groupValue: nil];
    
    return self;
}

- (id) initWithHistory: (NSDictionary *) history lib: (tr_handle *) lib
{
    self = [self initWithHash: [history objectForKey: @"TorrentHash"]
                path: [history objectForKey: @"TorrentPath"] data: nil lib: lib
                publicTorrent: [history objectForKey: @"PublicCopy"]
                downloadFolder: [history objectForKey: @"DownloadFolder"]
                useIncompleteFolder: [history objectForKey: @"UseIncompleteFolder"]
                incompleteFolder: [history objectForKey: @"IncompleteFolder"]
                dateAdded: [history objectForKey: @"Date"]
				dateCompleted: [history objectForKey: @"DateCompleted"]
                dateActivity: [history objectForKey: @"DateActivity"]
                ratioSetting: [history objectForKey: @"RatioSetting"]
                ratioLimit: [history objectForKey: @"RatioLimit"]
                waitToStart: [history objectForKey: @"WaitToStart"]
                orderValue: [history objectForKey: @"OrderValue"]
                groupValue: [history objectForKey: @"GroupValue"]];
    
    if (self)
    {
        //start transfer
        NSNumber * active;
        if ((active = [history objectForKey: @"Active"]) && [active boolValue])
        {
            fStat = tr_torrentStat(fHandle);
            [self startTransfer];
        }
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
                    fDateAdded, @"Date",
                    [NSNumber numberWithInt: fRatioSetting], @"RatioSetting",
                    [NSNumber numberWithFloat: fRatioLimit], @"RatioLimit",
                    [NSNumber numberWithBool: fWaitToStart], @"WaitToStart",
                    [NSNumber numberWithInt: fOrderValue], @"OrderValue",
                    [NSNumber numberWithInt: fGroupValue], @"GroupValue", nil];
    
    if (fIncompleteFolder)
        [history setObject: fIncompleteFolder forKey: @"IncompleteFolder"];

    if (fPublicTorrent)
        [history setObject: [self publicTorrentLocation] forKey: @"TorrentPath"];
	
    if (fDateCompleted)
		[history setObject: fDateCompleted forKey: @"DateCompleted"];
    
    NSDate * dateActivity = [self dateActivity];
    if (dateActivity)
		[history setObject: dateActivity forKey: @"DateActivity"];
	
    return history;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    if (fFileStat)
        tr_torrentFilesFree(fFileStat, [self fileCount]);
    
    if (fPreviousFinishedPieces != NULL)
        free(fPreviousFinishedPieces);
    [fFinishedPiecesDate release];
    
    [fNameString release];
    [fHashString release];
    
    [fDownloadFolder release];
    [fIncompleteFolder release];
    
    [fPublicTorrentLocation release];
    
    [fDateAdded release];
    [fDateCompleted release];
    [fDateActivity release];
    
    [fIcon release];
    
    [fFileList release];
    
    [fQuickPauseDict release];
    
    [super dealloc];
}

- (void) closeRemoveTorrent
{
    //allow the file to be index by Time Machine
    [self setTimeMachineExclude: NO forPath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]];
    
    tr_torrentDelete(fHandle);
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
    return [NSString stringWithUTF8String: tr_torrentGetFolder(fHandle)];
}

- (void) getAvailability: (int8_t *) tab size: (int) size
{
    tr_torrentAvailability(fHandle, tab, size);
}

- (void) getAmountFinished: (float *) tab size: (int) size
{
    tr_torrentAmountFinished(fHandle, tab, size);
}

- (float *) getPreviousAmountFinished
{
    if (fFinishedPiecesDate && [fFinishedPiecesDate timeIntervalSinceNow] > -2.0)
        return fPreviousFinishedPieces;
    else
        return NULL;
}

-(void) setPreviousAmountFinished: (float *) tab
{
    if (fPreviousFinishedPieces != NULL)
        free(fPreviousFinishedPieces);
    fPreviousFinishedPieces = tab;
    
    [fFinishedPiecesDate release];
    fFinishedPiecesDate = tab != NULL ? [[NSDate alloc] init] : nil;
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
    float stopRatio;
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
    tr_manualUpdate(fHandle);
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

- (float) ratio
{
    return fStat->ratio;
}

- (int) ratioSetting
{
    return fRatioSetting;
}

- (void) setRatioSetting: (int) setting
{
    fRatioSetting = setting;
}

- (float) ratioLimit
{
    return fRatioLimit;
}

- (void) setRatioLimit: (float) limit
{
    if (limit >= 0)
        fRatioLimit = limit;
}

- (float) actualStopRatio
{
    if (fRatioSetting == NSOnState)
        return fRatioLimit;
    else if (fRatioSetting == NSMixedState && [fDefaults boolForKey: @"RatioCheck"])
        return [fDefaults floatForKey: @"RatioLimit"];
    else
        return INVALID;
}

- (float) progressStopRatio
{
    float stopRatio, ratio;
    if ((stopRatio = [self actualStopRatio]) == INVALID || (ratio = [self ratio]) >= stopRatio)
        return 1.0;
    else if (ratio > 0 && stopRatio > 0)
        return ratio / stopRatio;
    else
        return 0;
}

- (tr_speedlimit) speedMode: (BOOL) upload
{
    return tr_torrentGetSpeedMode(fHandle, upload ? TR_UP : TR_DOWN);
}

- (void) setSpeedMode: (tr_speedlimit) mode upload: (BOOL) upload
{
    tr_torrentSetSpeedMode(fHandle, upload ? TR_UP : TR_DOWN, mode);
}

- (int) speedLimit: (BOOL) upload
{
    return tr_torrentGetSpeedLimit(fHandle, upload ? TR_UP : TR_DOWN);
}

- (void) setSpeedLimit: (int) limit upload: (BOOL) upload
{
    tr_torrentSetSpeedLimit(fHandle, upload ? TR_UP : TR_DOWN, limit);
}

- (void) setMaxPeerConnect: (uint16_t) count
{
    if (count > 0)
        tr_torrentSetMaxConnectedPeers(fHandle, count);
}

- (uint16_t) maxPeerConnect
{
    return tr_torrentGetMaxConnectedPeers(fHandle);
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
        int count;
        
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
            int i;
            for (i = 0; i < [self fileCount]; i++)
            {
                if (tr_torrentGetFileDL(fHandle, i))
                {
                    tr_file * file = &fInfo->files[i];
                    
                    neededSpace += file->length;
                    
                    NSString * path = [downloadFolder stringByAppendingPathComponent: [NSString stringWithUTF8String: file->name]];
                    NSDictionary * fileAttributes = onLeopard ? [fileManager attributesOfItemAtPath: path error: NULL]
                                                        : [fileManager fileAttributesAtPath: path traverseLink: NO];
                    if (fileAttributes)
                        neededSpace -= [[fileAttributes objectForKey: NSFileSize] unsignedLongLongValue];
                }
            }
            
            if (remainingSpace < neededSpace)
            {
                NSAlert * alert = [[NSAlert alloc] init];
                [alert setMessageText: [NSString stringWithFormat:
                                        NSLocalizedString(@"Not enough remaining disk space to download \"%@\" completely.",
                                            "Torrent file disk space alert -> title"), [self name]]];
                [alert setInformativeText: [NSString stringWithFormat: NSLocalizedString(@"The transfer will be paused."
                                            " Clear up space on %@ or deselect files in the torrent inspector to continue.",
                                            "Torrent file disk space alert -> message"), volumeName]];
                [alert addButtonWithTitle: NSLocalizedString(@"OK", "Torrent file disk space alert -> button")];
                [alert addButtonWithTitle: NSLocalizedString(@"Download Anyway", "Torrent file disk space alert -> button")];
                
                if (onLeopard)
                    [alert setShowsSuppressionButton: YES];
                else
                    [alert addButtonWithTitle: NSLocalizedString(@"Always Download", "Torrent file disk space alert -> button")];

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
    if (access(tr_torrentGetFolder(fHandle), 0))
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

- (void) destinationChoiceClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) context
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
        fIcon = [[[NSWorkspace sharedWorkspace] iconForFileType: [self folder] ? NSFileTypeForHFSTypeCode('fldr')
                                                : [[self name] pathExtension]] retain];
        [fIcon setFlipped: YES];
    }
    return fIcon;
}

- (NSString *) name
{
    return fNameString;
}

- (BOOL) folder
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

- (NSString *) trackerAddress
{
    return [NSString stringWithFormat: @"http://%s:%d", fStat->tracker->address, fStat->tracker->port];
}

- (NSString *) trackerAddressAnnounce
{
    return [NSString stringWithUTF8String: fStat->tracker->announce];
}

- (NSDate *) lastAnnounceTime
{
    int date = fStat->tracker_stat.lastAnnounceTime;
    return date > 0 ? [NSDate dateWithTimeIntervalSince1970: date] : nil;
}

- (int) nextAnnounceTime
{
    int date = fStat->tracker_stat.nextAnnounceTime;
    if (date <= 0)
        return -1;
    
    NSTimeInterval difference = [[NSDate dateWithTimeIntervalSince1970: date] timeIntervalSinceNow];
    return difference > 0 ? (int)difference : -1;
}

- (NSString *) announceResponse
{
    return [NSString stringWithUTF8String: fStat->tracker_stat.announceResponse];
}

- (NSString *) trackerAddressScrape
{
    return [NSString stringWithUTF8String: fStat->tracker->scrape];
}

- (NSDate *) lastScrapeTime
{
    int date = fStat->tracker_stat.lastScrapeTime;
    return date > 0 ? [NSDate dateWithTimeIntervalSince1970: date] : nil;
}

- (int) nextScrapeTime
{
    int date = fStat->tracker_stat.nextScrapeTime;
    if (date <= 0)
        return -1;
    
    NSTimeInterval difference = [[NSDate dateWithTimeIntervalSince1970: date] timeIntervalSinceNow];
    return difference > 0 ? (int)difference : -1;
}
- (NSString *) scrapeResponse
{
    return [NSString stringWithUTF8String: fStat->tracker_stat.scrapeResponse];
}

- (NSArray *) allTrackers
{
    NSMutableArray * allTrackers = [NSMutableArray array];
    
    int i, j;
    for (i = 0; i < fInfo->trackerTiers; i++)
    {
        [allTrackers addObject: [NSNumber numberWithInt: i]];
        for (j = 0; j < fInfo->trackerList[i].count; j++)
            [allTrackers addObject: [NSString stringWithFormat: @"http://%s:%d",
                fInfo->trackerList[i].list[j].address, fInfo->trackerList[i].list[j].port]];
    }
    
    return allTrackers;
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
    int date = fInfo->dateCreated;
    return date > 0 ? [NSDate dateWithTimeIntervalSince1970: date] : nil;
}

- (int) pieceSize
{
    return fInfo->pieceSize;
}

- (int) pieceCount
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

- (float) progress
{
    return fStat->percentComplete;
}

- (float) progressDone
{
    return fStat->percentDone;
}

- (float) progressLeft
{
    return (float)[self sizeLeft] / [self size];
}

- (float) checkingProgress
{
    return fStat->recheckProgress;
}

- (int) eta
{
    return fStat->eta;
}

- (int) etaRatio
{
    if (![self isSeeding])
        return -1;
    
    float uploadRate = [self uploadRate];
    if (uploadRate < 0.1)
        return -1;
    
    float stopRatio = [self actualStopRatio], ratio = [self ratio];
    if (stopRatio == INVALID || ratio >= stopRatio)
        return -1;
    
    return (float)MAX([self downloadedTotal], [self haveVerified]) * (stopRatio - ratio) / uploadRate / 1024.0;
}

- (float) notAvailableDesired
{
    return (float)(fStat->desiredSize - fStat->desiredAvailable) / [self size];
}

- (BOOL) isActive
{
    return fStat->status != TR_STATUS_STOPPED;
}

- (BOOL) isSeeding
{
    return fStat->status == TR_STATUS_SEED || fStat->status == TR_STATUS_DONE;
}

- (BOOL) isChecking
{
    return fStat->status == TR_STATUS_CHECK || fStat->status == TR_STATUS_CHECK_WAIT;
}

- (BOOL) isCheckingWaiting
{
    return fStat->status == TR_STATUS_CHECK_WAIT;
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
    return fStat->error != 0;
}

- (NSString *) errorMessage
{
    if (![self isError])
        return @"";
    
    NSString * error;
    if (!(error = [NSString stringWithUTF8String: fStat->errorString])
        && !(error = [NSString stringWithCString: fStat->errorString encoding: NSISOLatin1StringEncoding]))
        error = NSLocalizedString(@"(unreadable error)", "Torrent -> error string unreadable");
    
    return error;
}

- (NSArray *) peers
{
    int totalPeers, i;
    tr_peer_stat * peers = tr_torrentPeers(fHandle, &totalPeers);
    
    NSMutableArray * peerDicts = [NSMutableArray arrayWithCapacity: totalPeers];
    
    for (i = 0; i < totalPeers; i++)
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
            [dict setObject: [NSNumber numberWithFloat: peer->uploadToRate] forKey: @"UL To Rate"];
        if (peer->isDownloadingFrom)
            [dict setObject: [NSNumber numberWithFloat: peer->downloadFromRate] forKey: @"DL From Rate"];
        
        [peerDicts addObject: dict];
    }
    
    tr_torrentPeersFree(peers, totalPeers);
    
    return peerDicts;
}

- (NSString *) progressString
{
    NSString * string;
    
    if (![self allDownloaded])
    {
        if ([fDefaults boolForKey: @"DisplayStatusProgressSelected"])
        {
            string = [NSString stringWithFormat: NSLocalizedString(@"%@ of %@ selected (%.2f%%)", "Torrent -> progress string"),
                            [NSString stringForFileSize: [self haveTotal]], [NSString stringForFileSize: [self totalSizeSelected]],
                            100.0 * [self progressDone]];
        }
        else
            string = [NSString stringWithFormat: NSLocalizedString(@"%@ of %@ (%.2f%%)", "Torrent -> progress string"),
                            [NSString stringForFileSize: [self haveTotal]],
                            [NSString stringForFileSize: [self size]], 100.0 * [self progress]];
    }
    else if (![self isComplete])
    {
        if ([fDefaults boolForKey: @"DisplayStatusProgressSelected"])
            string = [NSString stringWithFormat: NSLocalizedString(@"%@ selected, uploaded %@ (Ratio: %@)",
                "Torrent -> progress string"), [NSString stringForFileSize: [self haveTotal]],
                [NSString stringForFileSize: [self uploadedTotal]], [NSString stringForRatio: [self ratio]]];
        else
            string = [NSString stringWithFormat: NSLocalizedString(@"%@ of %@ (%.2f%%), uploaded %@ (Ratio: %@)",
                "Torrent -> progress string"), [NSString stringForFileSize: [self haveTotal]],
                [NSString stringForFileSize: [self size]], 100.0 * [self progress],
                [NSString stringForFileSize: [self uploadedTotal]], [NSString stringForRatio: [self ratio]]];
    }
    else
        string = [NSString stringWithFormat: NSLocalizedString(@"%@, uploaded %@ (Ratio: %@)", "Torrent -> progress string"),
                [NSString stringForFileSize: [self size]], [NSString stringForFileSize: [self uploadedTotal]],
                [NSString stringForRatio: [self ratio]]];
    
    //add time when downloading
    if (fStat->status == TR_STATUS_DOWNLOAD || ([self isSeeding]
        && (fRatioSetting == NSOnState || (fRatioSetting == NSMixedState && [fDefaults boolForKey: @"RatioCheck"]))))
    {
        int eta = fStat->status == TR_STATUS_DOWNLOAD ? [self eta] : [self etaRatio];
        string = eta >= 0 ? [string stringByAppendingFormat: NSLocalizedString(@" - %@ remaining", "Torrent -> progress string"),
                                [NSString timeString: eta showSeconds: YES maxDigits: 2]]
            : [string stringByAppendingString: NSLocalizedString(@" - remaining time unknown", "Torrent -> progress string")];
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
            string = [NSString stringWithFormat: @"%@: %@", string, errorString];
    }
    else
    {
        switch (fStat->status)
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
                string = [NSString stringWithFormat: NSLocalizedString(@"Checking existing data (%.2f%%)",
                                        "Torrent -> status string"), 100.0 * [self checkingProgress]];
                break;

            case TR_STATUS_DOWNLOAD:
                if ([self totalPeersConnected] != 1)
                    string = [NSString stringWithFormat: NSLocalizedString(@"Downloading from %d of %d peers",
                                                    "Torrent -> status string"), [self peersSendingToUs], [self totalPeersConnected]];
                else
                    string = [NSString stringWithFormat: NSLocalizedString(@"Downloading from %d of 1 peer",
                                                    "Torrent -> status string"), [self peersSendingToUs]];
                break;

            case TR_STATUS_SEED:
            case TR_STATUS_DONE:
                if ([self totalPeersConnected] != 1)
                    string = [NSString stringWithFormat: NSLocalizedString(@"Seeding to %d of %d peers", "Torrent -> status string"),
                                                    [self peersGettingFromUs], [self totalPeersConnected]];
                else
                    string = [NSString stringWithFormat: NSLocalizedString(@"Seeding to %d of 1 peer", "Torrent -> status string"),
                                                    [self peersGettingFromUs]];
                break;
            
            default:
                string = @"";
        }
        
        if (fStalled)
            string = [NSLocalizedString(@"Stalled, ", "Torrent -> status string") stringByAppendingString: string];
    }
    
    //append even if error
    if ([self isActive] && ![self isChecking])
    {
        if (fStat->status == TR_STATUS_DOWNLOAD)
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
    
    switch (fStat->status)
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
            string = [NSString stringWithFormat: NSLocalizedString(@"Checking existing data (%.2f%%)",
                                    "Torrent -> status string"), 100.0 * [self checkingProgress]];
            break;
        
        case TR_STATUS_DOWNLOAD:
            string = [NSString stringWithFormat: @"%@: %@, %@: %@",
                            NSLocalizedString(@"DL", "Torrent -> status string"), [NSString stringForSpeed: [self downloadRate]],
                            NSLocalizedString(@"UL", "Torrent -> status string"), [NSString stringForSpeed: [self uploadRate]]];
            break;
        
        case TR_STATUS_SEED:
        case TR_STATUS_DONE:
            string = [NSString stringWithFormat: @"%@: %@, %@: %@",
                            NSLocalizedString(@"Ratio", "Torrent -> status string"), [NSString stringForRatio: [self ratio]],
                            NSLocalizedString(@"UL", "Torrent -> status string"), [NSString stringForSpeed: [self uploadRate]]];
            break;
        
        default:
            string = @"";
    }
    
    return string;
}

- (NSString *) remainingTimeString
{
    if (![self isActive] || ([self isSeeding]
        && !(fRatioSetting == NSOnState || (fRatioSetting == NSMixedState && [fDefaults boolForKey: @"RatioCheck"]))))
        return [self shortStatusString];
    
    int eta = [self isSeeding] ? [self etaRatio] : [self eta];
    return eta >= 0 ? [NSString timeString: eta showSeconds: YES maxDigits: 2]
                    : NSLocalizedString(@"Unknown", "Torrent -> remaining time");
}

- (NSString *) stateString
{
    switch (fStat->status)
    {
        case TR_STATUS_STOPPED:
            return NSLocalizedString(@"Paused", "Torrent -> status string");

        case TR_STATUS_CHECK:
            return [NSString stringWithFormat: NSLocalizedString(@"Checking existing data (%.2f%%)",
                                    "Torrent -> status string"), 100.0 * [self checkingProgress]];
        
        case TR_STATUS_CHECK_WAIT:
            return [NSLocalizedString(@"Waiting to check existing data", "Torrent -> status string") stringByAppendingEllipsis];

        case TR_STATUS_DOWNLOAD:
            return NSLocalizedString(@"Downloading", "Torrent -> status string");

        case TR_STATUS_SEED:
        case TR_STATUS_DONE:
            return NSLocalizedString(@"Seeding", "Torrent -> status string");
        
        default:
            return NSLocalizedString(@"N/A", "Torrent -> status string");
    }
}

- (int) seeders
{
    return fStat->seeders;
}

- (int) leechers
{
    return fStat->leechers;
}

- (int) completedFromTracker
{
    return fStat->completedFromTracker;
}

- (int) totalPeersConnected
{
    return fStat->peersConnected;
}

- (int) totalPeersTracker
{
    return fStat->peersFrom[TR_PEER_FROM_TRACKER];
}

- (int) totalPeersIncoming
{
    return fStat->peersFrom[TR_PEER_FROM_INCOMING];
}

- (int) totalPeersCache
{
    return fStat->peersFrom[TR_PEER_FROM_CACHE];
}

- (int) totalPeersPex
{
    return fStat->peersFrom[TR_PEER_FROM_PEX];
}

- (int) totalPeersKnown
{
    return fStat->peersKnown;
}

- (int) peersSendingToUs
{
    return fStat->peersSendingToUs;
}

- (int) peersGettingFromUs
{
    return fStat->peersGettingFromUs;
}

- (float) downloadRate
{
    return fStat->rateDownload;
}

- (float) uploadRate
{
    return fStat->rateUpload;
}

- (float) totalRate
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
    return [self haveTotal] + [self sizeLeft];
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

- (float) swarmSpeed
{
    return fStat->swarmspeed;
}

- (int) orderValue
{
    return fOrderValue;
}

- (void) setOrderValue: (int) orderValue
{
    fOrderValue = orderValue;
}

- (int) groupValue
{
    return fGroupValue;
}

- (void) setGroupValue: (int) goupValue
{
    fGroupValue = goupValue;
}

- (int) groupOrderValue
{
    return [[GroupsWindowController groups] orderValueForIndex: fGroupValue];
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

- (int) fileCount
{
    return fInfo->fileCount;
}

- (void) updateFileStat
{
    if (fFileStat)
        tr_torrentFilesFree(fFileStat, [self fileCount]);
    
    int count;
    fFileStat = tr_torrentFiles(fHandle, &count);
}

- (float) fileProgress: (int) index
{
    if (!fFileStat)
        [self updateFileStat];
        
    return fFileStat[index].progress;
}

- (BOOL) canChangeDownloadCheckForFile: (int) index
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
    
    int index;
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
        if (fFileStat[index].progress < 1.0)
            return YES;
    return NO;
}

- (int) checkForFiles: (NSIndexSet *) indexSet
{
    BOOL onState = NO, offState = NO;
    int index;
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
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

- (void) setFileCheckState: (int) state forIndexes: (NSIndexSet *) indexSet
{
    int count = [indexSet count], i = 0, index;
    int * files = malloc(count * sizeof(int));
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
    {
        files[i] = index;
        i++;
    }
    
    tr_torrentSetFileDLs(fHandle, files, count, state != NSOffState);
    free(files);
    
    [self update];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentFileCheckChange" object: self];
}

- (void) setFilePriority: (int) priority forIndexes: (NSIndexSet *) indexSet
{
    int count = [indexSet count], i = 0, index;
    int * files = malloc(count * sizeof(int));
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
    {
        files[i] = index;
        i++;
    }
    
    tr_torrentSetFilePriorities(fHandle, files, count, priority);
    free(files);
}

- (BOOL) hasFilePriority: (int) priority forIndexes: (NSIndexSet *) indexSet
{
    int index;
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
        if (priority == tr_torrentGetFilePriority(fHandle, index) && [self canChangeDownloadCheckForFile: index])
            return YES;
    return NO;
}

- (NSSet *) filePrioritiesForIndexes: (NSIndexSet *) indexSet
{
    BOOL low = NO, normal = NO, high = NO;
    NSMutableSet * priorities = [NSMutableSet setWithCapacity: 3];
    
    int index, priority;
    for (index = [indexSet firstIndex]; index != NSNotFound; index = [indexSet indexGreaterThanIndex: index])
    {
        if (![self canChangeDownloadCheckForFile: index])
            continue;
        
        priority = tr_torrentGetFilePriority(fHandle, index);
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
    return fDateAdded;
}

- (NSDate *) dateCompleted
{
    return fDateCompleted;
}

- (NSDate *) dateActivity
{
    uint64_t date = fStat->activityDate;
    return date != 0 ? [NSDate dateWithTimeIntervalSince1970: date / 1000] : fDateActivity;
}

- (NSDate *) dateActivityOrAdd
{
    NSDate * date = [self dateActivity];
    return date ? date : [self dateAdded];
}

- (int) stalledMinutes
{
    uint64_t start;
    if ((start = fStat->startDate) == 0)
        return -1;
    
    NSDate * started = [NSDate dateWithTimeIntervalSince1970: start / 1000],
            * activity = [self dateActivity];
    
    NSDate * laterDate = activity ? [started laterDate: activity] : started;
    return -1 * [laterDate timeIntervalSinceNow] / 60;
}

- (BOOL) isStalled
{
    return fStalled;
}

- (NSNumber *) stateSortKey
{
    if (![self isActive])
        return [NSNumber numberWithInt: 0];
    else if ([self isSeeding])
        return [NSNumber numberWithInt: 1];
    else
        return [NSNumber numberWithInt: 2];
}

- (int) torrentID
{
    return fID;
}

- (const tr_info *) torrentInfo
{
    return fInfo;
}

- (const tr_stat *) torrentStat
{
    return fStat;
}

@end

@implementation Torrent (Private)

//if a hash is given, attempt to load that; otherwise, attempt to open file at path
- (id) initWithHash: (NSString *) hashString path: (NSString *) path data: (NSData *) data lib: (tr_handle *) lib
        publicTorrent: (NSNumber *) publicTorrent
        downloadFolder: (NSString *) downloadFolder
        useIncompleteFolder: (NSNumber *) useIncompleteFolder incompleteFolder: (NSString *) incompleteFolder
        dateAdded: (NSDate *) dateAdded dateCompleted: (NSDate *) dateCompleted
        dateActivity: (NSDate *) dateActivity
        ratioSetting: (NSNumber *) ratioSetting ratioLimit: (NSNumber *) ratioLimit
        waitToStart: (NSNumber *) waitToStart
        orderValue: (NSNumber *) orderValue groupValue: (NSNumber *) groupValue;
{
    if (!(self = [super init]))
        return nil;
    
    static_lastid++;
    fID = static_lastid;
    
    fLib = lib;
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
    
    //set libtransmission settings for initialization
    tr_ctor * ctor = tr_ctorNew(fLib);
    tr_ctorSetPaused(ctor, TR_FORCE, YES);
    tr_ctorSetMaxConnectedPeers(ctor, TR_FALLBACK, [fDefaults integerForKey: @"PeersTorrent"]);
    
    tr_info info;
    int error;
    if (hashString)
    {
        tr_ctorSetMetainfoFromHash(ctor, [hashString UTF8String]);
        if (tr_torrentParse(fLib, ctor, &info) == TR_OK)
        {
            NSString * currentDownloadFolder = [self shouldUseIncompleteFolderForName: [NSString stringWithUTF8String: info.name]]
                                                ? fIncompleteFolder : fDownloadFolder;
            tr_ctorSetDestination(ctor, TR_FORCE, [currentDownloadFolder UTF8String]);
            
            fHandle = tr_torrentNew(fLib, ctor, &error);
        }
        tr_metainfoFree(&info);
    }
    if (!fHandle && path)
    {
        tr_ctorSetMetainfoFromFile(ctor, [path UTF8String]);
        if (tr_torrentParse(fLib, ctor, &info) == TR_OK)
        {
            NSString * currentDownloadFolder = [self shouldUseIncompleteFolderForName: [NSString stringWithUTF8String: info.name]]
                                                ? fIncompleteFolder : fDownloadFolder;
            tr_ctorSetDestination(ctor, TR_FORCE, [currentDownloadFolder UTF8String]);
            
            fHandle = tr_torrentNew(fLib, ctor, &error);
        }
        tr_metainfoFree(&info);
    }
    if (!fHandle && data)
    {
        tr_ctorSetMetainfo(ctor, [data bytes], [data length]);
        if (tr_torrentParse(fLib, ctor, &info) == TR_OK)
        {
            NSString * currentDownloadFolder = [self shouldUseIncompleteFolderForName: [NSString stringWithUTF8String: info.name]]
                                                ? fIncompleteFolder : fDownloadFolder;
            tr_ctorSetDestination(ctor, TR_FORCE, [currentDownloadFolder UTF8String]);
            
            fHandle = tr_torrentNew(fLib, ctor, &error);
        }
        tr_metainfoFree(&info);
    }
    
    tr_ctorFree(ctor);
    
    if (!fHandle)
    {
        [self release];
        return nil;
    }
    
    tr_torrentSetStatusCallback(fHandle, completenessChangeCallback, self);
    
    fInfo = tr_torrentInfo(fHandle);
    
    fNameString = [[NSString alloc] initWithUTF8String: fInfo->name];
    fHashString = [[NSString alloc] initWithUTF8String: fInfo->hashString];
    
    fDateAdded = dateAdded ? [dateAdded retain] : [[NSDate alloc] init];
	if (dateCompleted)
		fDateCompleted = [dateCompleted retain];
    if (dateActivity)
		fDateActivity = [dateActivity retain];
	
    fRatioSetting = ratioSetting ? [ratioSetting intValue] : NSMixedState;
    fRatioLimit = ratioLimit ? [ratioLimit floatValue] : [fDefaults floatForKey: @"RatioLimit"];
    fFinishedSeeding = NO;
    
    fWaitToStart = waitToStart && [waitToStart boolValue];
    
    fOrderValue = orderValue ? [orderValue intValue] : tr_torrentCount(fLib) - 1;
    fGroupValue = groupValue ? [groupValue intValue] : -1;
    
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
    int count = [self fileCount], i;
    tr_file * file;
    NSMutableArray * pathComponents;
    NSString * path;
    
    NSMutableArray * fileList = [[NSMutableArray alloc] initWithCapacity: count];
    
    for (i = 0; i < count; i++)
    {
        file = &fInfo->files[i];
        
        pathComponents = [[[NSString stringWithUTF8String: file->name] pathComponents] mutableCopy];
        if ([self folder])
        {
            path = [pathComponents objectAtIndex: 0];
            [pathComponents removeObjectAtIndex: 0];
        }
        else
            path = @"";
        
        [self insertPath: pathComponents forSiblings: fileList previousPath: path fileSize: file->length index: i];
        [pathComponents release];
    }
    
    fFileList = [[NSArray alloc] initWithArray: fileList];
    [fileList release];
}

- (void) insertPath: (NSMutableArray *) components forSiblings: (NSMutableArray *) siblings previousPath: (NSString *) previousPath
            fileSize: (uint64_t) size index: (int) index
{
    NSString * name = [components objectAtIndex: 0];
    BOOL isFolder = [components count] > 1;
    
    NSMutableDictionary * dict = nil;
    if (isFolder)
    {
        NSEnumerator * enumerator = [siblings objectEnumerator];
        while ((dict = [enumerator nextObject]))
            if ([[dict objectForKey: @"Name"] isEqualToString: name] && [[dict objectForKey: @"IsFolder"] boolValue])
                break;
    }
    
    NSString * currentPath = [previousPath stringByAppendingPathComponent: name];
    
    //create new folder or item if it doesn't already exist
    if (!dict)
    {
        dict = [NSMutableDictionary dictionaryWithObjectsAndKeys: name, @"Name",
                [NSNumber numberWithBool: isFolder], @"IsFolder", currentPath, @"Path", nil];
        [siblings addObject: dict];
        
        if (isFolder)
        {
            [dict setObject: [NSMutableArray array] forKey: @"Children"];
            [dict setObject: [NSMutableIndexSet indexSetWithIndex: index] forKey: @"Indexes"];
        }
        else
        {
            [dict setObject: [NSIndexSet indexSetWithIndex: index] forKey: @"Indexes"];
            [dict setObject: [NSNumber numberWithUnsignedLongLong: size] forKey: @"Size"];
            
            NSImage * icon = [[NSWorkspace sharedWorkspace] iconForFileType: [name pathExtension]];
            [icon setFlipped: YES];
            [dict setObject: icon forKey: @"Icon"];
        }
    }
    else
        [[dict objectForKey: @"Indexes"] addIndex: index];
    
    if (isFolder)
    {
        [components removeObjectAtIndex: 0];
        [self insertPath: components forSiblings: [dict objectForKey: @"Children"] previousPath: currentPath fileSize: size
                index: index];
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
    tr_torrentSetFolder(fHandle, [folder UTF8String]);
    
    [self setTimeMachineExclude: ![self allDownloaded] forPath: [folder stringByAppendingPathComponent: [self name]]];
}

//status has been retained
- (void) completenessChange: (NSNumber *) status
{
    [self update];
    
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
            
            [fDateCompleted release];
            fDateCompleted = [[NSDate alloc] init];
            
            //allow to be backed up by Time Machine
            [self setTimeMachineExclude: NO forPath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]];
            
            fStat = tr_torrentStat(fHandle);
            [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentFinishedDownloading" object: self];
            break;
        
        case TR_CP_INCOMPLETE:
            //do not allow to be backed up by Time Machine
            [self setTimeMachineExclude: YES forPath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]];
            
            [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentRestartedDownloading" object: self];
            break;
    }
    [status release];
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

- (void) trashFile: (NSString *) path
{
    //attempt to move to trash
    if (![[NSWorkspace sharedWorkspace] performFileOperation: NSWorkspaceRecycleOperation
            source: [path stringByDeletingLastPathComponent] destination: @""
            files: [NSArray arrayWithObject: [path lastPathComponent]] tag: nil])
    {
        //if cannot trash, just delete it (will work if it is on a remote volume)
        if (![[NSFileManager defaultManager] removeFileAtPath: path handler: nil])
            NSLog(@"Could not trash %@", path);
    }
}

- (void) setTimeMachineExclude: (BOOL) exclude forPath: (NSString *) path
{
    if ([NSApp isOnLeopardOrBetter])
        CSBackupSetItemExcluded((CFURLRef)[NSURL fileURLWithPath: path], exclude, true);
}

@end
