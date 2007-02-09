/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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
#import "StringAdditions.h"

#define BAR_HEIGHT 12.0

#define MAX_PIECES 324
#define BLANK_PIECE -99

@interface Torrent (Private)

- (id) initWithHash: (NSString *) hashString path: (NSString *) path lib: (tr_handle_t *) lib
        publicTorrent: (NSNumber *) publicTorrent
        date: (NSDate *) date
        ratioSetting: (NSNumber *) ratioSetting ratioLimit: (NSNumber *) ratioLimit
        limitSpeedCustom: (NSNumber *) limitCustom
        checkUpload: (NSNumber *) checkUpload uploadLimit: (NSNumber *) uploadLimit
        checkDownload: (NSNumber *) checkDownload downloadLimit: (NSNumber *) downloadLimit
        waitToStart: (NSNumber *) waitToStart orderValue: (NSNumber *) orderValue;

- (NSArray *) createFileList;
- (void) insertPath: (NSMutableArray *) components forSiblings: (NSMutableArray *) siblings
        withParent: (NSMutableDictionary *) parent previousPath: (NSString *) previousPath
        fileSize: (uint64_t) size state: (int) state;
- (NSImage *) advancedBar;
- (void) trashFile: (NSString *) path;

@end

@implementation Torrent

// Used to optimize drawing. They contain packed RGBA pixels for every color needed.
#define BE OSSwapBigToHostConstInt32

static uint32_t kRed   = BE(0xFF6450FF), //255, 100, 80
                kBlue = BE(0x50A0FFFF), //80, 160, 255
                kBlue2 = BE(0x1E46B4FF), //30, 70, 180
                kGray  = BE(0x969696FF), //150, 150, 150
                kGreen1 = BE(0x99FFCCFF), //153, 255, 204
                kGreen2 = BE(0x66FF99FF), //102, 255, 153
                kGreen3 = BE(0x00FF66FF), //0, 255, 102
                kWhite = BE(0xFFFFFFFF); //255, 255, 255

- (id) initWithPath: (NSString *) path forceDeleteTorrent: (BOOL) delete lib: (tr_handle_t *) lib
{
    self = [self initWithHash: nil path: path lib: lib
            publicTorrent: delete ? [NSNumber numberWithBool: NO] : nil
            date: nil
            ratioSetting: nil ratioLimit: nil
            limitSpeedCustom: nil
            checkUpload: nil uploadLimit: nil
            checkDownload: nil downloadLimit: nil
            waitToStart: nil orderValue: nil];
    
    if (self)
    {
        fUseIncompleteFolder = [fDefaults boolForKey: @"UseIncompleteDownloadFolder"];
        fIncompleteFolder = [[fDefaults stringForKey: @"IncompleteDownloadFolder"] copy];
        
        if (!fPublicTorrent)
            [self trashFile: path];
    }
    return self;
}

- (id) initWithHistory: (NSDictionary *) history lib: (tr_handle_t *) lib
{
    self = [self initWithHash: [history objectForKey: @"TorrentHash"]
                path: [history objectForKey: @"TorrentPath"] lib: lib
                publicTorrent: [history objectForKey: @"PublicCopy"]
                date: [history objectForKey: @"Date"]
                ratioSetting: [history objectForKey: @"RatioSetting"]
                ratioLimit: [history objectForKey: @"RatioLimit"]
                limitSpeedCustom: [history objectForKey: @"LimitSpeedCustom"]
                checkUpload: [history objectForKey: @"CheckUpload"]
                uploadLimit: [history objectForKey: @"UploadLimit"]
                checkDownload: [history objectForKey: @"CheckDownload"]
                downloadLimit: [history objectForKey: @"DownloadLimit"]
                waitToStart: [history objectForKey: @"WaitToStart"]
                orderValue: [history objectForKey: @"OrderValue"]];
    
    if (self)
    {
        //download folders
        NSString * downloadFolder;
        if (!(downloadFolder = [history objectForKey: @"DownloadFolder"]))
            downloadFolder = [[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath];
        
        NSNumber * useIncompleteFolder;
        if ((useIncompleteFolder = [history objectForKey: @"UseIncompleteFolder"]))
        {
            if ((fUseIncompleteFolder = [useIncompleteFolder boolValue]))
            {
                NSString * incompleteFolder;
                if (incompleteFolder = [history objectForKey: @"IncompleteFolder"])
                    fIncompleteFolder = [incompleteFolder retain];
                else
                    fIncompleteFolder = [[[fDefaults stringForKey: @"IncompleteDownloadFolder"]
                                            stringByExpandingTildeInPath] retain];
            }
        }
        else
            fUseIncompleteFolder = NO;
        
        [self setDownloadFolder: downloadFolder];

        NSString * paused;
        if (!(paused = [history objectForKey: @"Paused"]) || [paused isEqualToString: @"NO"])
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
                    [self isActive] ? @"NO" : @"YES", @"Paused",
                    [self date], @"Date",
                    [NSNumber numberWithInt: fRatioSetting], @"RatioSetting",
                    [NSNumber numberWithFloat: fRatioLimit], @"RatioLimit",
                    [NSNumber numberWithInt: fCheckUpload], @"CheckUpload",
                    [NSNumber numberWithInt: fUploadLimit], @"UploadLimit",
                    [NSNumber numberWithInt: fCheckDownload], @"CheckDownload",
                    [NSNumber numberWithInt: fDownloadLimit], @"DownloadLimit",
                    [NSNumber numberWithBool: fWaitToStart], @"WaitToStart",
                    [self orderValue], @"OrderValue", nil];
    
    if (fUseIncompleteFolder)
        [history setObject: fIncompleteFolder forKey: @"IncompleteFolder"];

    if (fPublicTorrent)
        [history setObject: [self publicTorrentLocation] forKey: @"TorrentPath"];
    
    return history;
}

- (void) dealloc
{
    if (fHandle)
    {
        tr_torrentClose(fLib, fHandle);
        
        if (fDownloadFolder)
            [fDownloadFolder release];
        if (fIncompleteFolder)
            [fIncompleteFolder release];
        
        if (fPublicTorrentLocation)
            [fPublicTorrentLocation release];
        
        tr_torrentRemoveSaved(fHandle);
        
        [fDate release];
        
        if (fAnnounceDate)
            [fAnnounceDate release];
        
        [fIcon release];
        [fIconFlipped release];
        [fIconSmall release];
        
        [fProgressString release];
        [fStatusString release];
        [fShortStatusString release];
        [fRemainingTimeString release];
        
        [fFileList release];
        
        [fBitmap release];
        free(fPieces);
    }
    [super dealloc];
}

- (void) setDownloadFolder: (NSString *) path
{
    if (path)
        fDownloadFolder = [path copy];
    
    if (!fUseIncompleteFolder || [[NSFileManager defaultManager] fileExistsAtPath:
                                    [path stringByAppendingPathComponent: [self name]]])
        tr_torrentSetFolder(fHandle, [path UTF8String]);
    else
        tr_torrentSetFolder(fHandle, [fIncompleteFolder UTF8String]);
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

- (void) update
{
    fStat = tr_torrentStat(fHandle);
    
    //notification when downloading finished
    if ([self justFinished])
    {
        BOOL canMove = YES;
        
        //move file from incomplete folder to download folder
        if (fUseIncompleteFolder && ![[self downloadFolder] isEqualToString: fDownloadFolder]
            && (canMove = [self alertForMoveFolderAvailable]))
        {
            tr_torrentStop(fHandle);
            if ([[NSFileManager defaultManager] movePath: [[self downloadFolder] stringByAppendingPathComponent: [self name]]
                                    toPath: [fDownloadFolder stringByAppendingPathComponent: [self name]] handler: nil])
                tr_torrentSetFolder(fHandle, [fDownloadFolder UTF8String]);
            tr_torrentStart(fHandle);
        }
        
        if (!canMove)
        {
            fUseIncompleteFolder = NO;
            
            [fDownloadFolder release];
            fDownloadFolder = fIncompleteFolder;
            fIncompleteFolder = nil;
        }
        
        fStat = tr_torrentStat(fHandle);
        [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentFinishedDownloading" object: self];
    }
    
    //check to stop for ratio
    if ([self isSeeding] && ((fRatioSetting == NSOnState && [self ratio] >= fRatioLimit)
            || (fRatioSetting == NSMixedState && [fDefaults boolForKey: @"RatioCheck"]
                && [self ratio] >= [fDefaults floatForKey: @"RatioLimit"])))
    {
        [self stopTransfer];
        fStat = tr_torrentStat(fHandle);
        
        fFinishedSeeding = YES;
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentStoppedForRatio" object: self];
    }

    [fProgressString setString: @""];
    if (![self allDownloaded])
        [fProgressString appendFormat: NSLocalizedString(@"%@ of %@ (%.2f%%)", "Torrent -> progress string"),
                            [NSString stringForFileSize: [self downloadedValid]],
                            [NSString stringForFileSize: [self size]], 100.0 * [self progress]];
    else
        [fProgressString appendFormat: NSLocalizedString(@"%@, uploaded %@ (Ratio: %@)", "Torrent -> progress string"),
                [NSString stringForFileSize: [self size]], [NSString stringForFileSize: [self uploadedTotal]],
                [NSString stringForRatio: [self ratio]]];

    BOOL wasChecking = fChecking;
    fChecking = NO;
    switch (fStat->status)
    {
        NSString * tempString;
        
        case TR_STATUS_PAUSE:
            if (fWaitToStart)
            {
                tempString = ![self allDownloaded]
                        ? [NSLocalizedString(@"Waiting to download", "Torrent -> status string") stringByAppendingEllipsis]
                        : [NSLocalizedString(@"Waiting to seed", "Torrent -> status string") stringByAppendingEllipsis];
            }
            else if (fFinishedSeeding)
                tempString = NSLocalizedString(@"Seeding complete", "Torrent -> status string");
            else
                tempString = NSLocalizedString(@"Paused", "Torrent -> status string");
            
            [fStatusString setString: tempString];
            [fShortStatusString setString: tempString];
            
            break;

        case TR_STATUS_CHECK:
            tempString = [NSLocalizedString(@"Checking existing files", "Torrent -> status string") stringByAppendingEllipsis];
            
            [fStatusString setString: tempString];
            [fShortStatusString setString: tempString];
            [fRemainingTimeString setString: tempString];
            
            fChecking = YES;
            
            break;

        case TR_STATUS_DOWNLOAD:
            [fStatusString setString: @""];
            if ([self totalPeers] != 1)
                [fStatusString appendFormat: NSLocalizedString(@"Downloading from %d of %d peers",
                                                "Torrent -> status string"), [self peersUploading], [self totalPeers]];
            else
                [fStatusString appendFormat: NSLocalizedString(@"Downloading from %d of %d peer",
                                                "Torrent -> status string"), [self peersUploading], [self totalPeers]];
            
            [fRemainingTimeString setString: @""];
            int eta = [self eta];
            if (eta < 0)
            {
                [fRemainingTimeString setString: NSLocalizedString(@"Unknown", "Torrent -> remaining time")];
                [fProgressString appendString: NSLocalizedString(@" - remaining time unknown", "Torrent -> progress string")];
            }
            else
            {
                if (eta < 60)
                    [fRemainingTimeString appendFormat: NSLocalizedString(@"%d sec", "Torrent -> remaining time"), eta];
                else if (eta < 3600) //60 * 60
                    [fRemainingTimeString appendFormat: NSLocalizedString(@"%d min %02d sec", "Torrent -> remaining time"),
                                                            eta / 60, eta % 60];
                else if (eta < 86400) //24 * 60 * 60
                    [fRemainingTimeString appendFormat: NSLocalizedString(@"%d hr %02d min", "Torrent -> remaining time"),
                                                            eta / 3600, (eta / 60) % 60];
                else
                {
                    if (eta / 86400 > 1)
                        [fRemainingTimeString appendFormat: NSLocalizedString(@"%d days %d hr", "Torrent -> remaining time"),
                                                                eta / 86400, (eta / 3600) % 24];
                    else
                        [fRemainingTimeString appendFormat: NSLocalizedString(@"%d day %d hr", "Torrent -> remaining time"),
                                                                eta / 86400, (eta / 3600) % 24];
                }
                
                [fProgressString appendFormat: NSLocalizedString(@" - %@ remaining", "Torrent -> progress string"),
                                                                    fRemainingTimeString];
            }
            
            break;

        case TR_STATUS_SEED:
            [fStatusString setString: @""];
            if ([self totalPeers] != 1)
                [fStatusString appendFormat: NSLocalizedString(@"Seeding to %d of %d peers", "Torrent -> status string"),
                                                [self peersDownloading], [self totalPeers]];
            else
                [fStatusString appendFormat: NSLocalizedString(@"Seeding to %d of 1 peer", "Torrent -> status string"),
                                                [self peersDownloading]];
            
            break;

        case TR_STATUS_STOPPING:
            tempString = [NSLocalizedString(@"Stopping", "Torrent -> status string") stringByAppendingEllipsis];
        
            [fStatusString setString: tempString];
            [fShortStatusString setString: tempString];
            
            break;
    }
    
    if (wasChecking && !fChecking)
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
    
    if (fStat->error)
    {
        [fStatusString setString: [NSLocalizedString(@"Error: ", "Torrent -> status string") stringByAppendingString:
                                    [self errorMessage]]];
    }
    
    BOOL wasError = fError;
    if ((fError = fStat->cannotConnect))
    {
        if (!wasError && [self isActive])
            [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
    }

    if ([self isActive] && fStat->status != TR_STATUS_CHECK )
    {
        NSString * stringToAppend = @"";
        if (![self allDownloaded])
        {
            stringToAppend = [NSString stringWithFormat: NSLocalizedString(@"DL: %@, ", "Torrent -> status string"),
                                [NSString stringForSpeed: [self downloadRate]]];
            [fShortStatusString setString: @""];
        }
        else
        {
            NSString * ratioString = [NSString stringForRatio: [self ratio]];
        
            [fShortStatusString setString: [NSString stringWithFormat: NSLocalizedString(@"Ratio: %@, ",
                                            "Torrent -> status string"), ratioString]];
            [fRemainingTimeString setString: [NSLocalizedString(@"Ratio: ", "Torrent -> status string")
                                                stringByAppendingString: ratioString]];
        }
        
        stringToAppend = [stringToAppend stringByAppendingString: [NSLocalizedString(@"UL: ", "Torrent -> status string")
                                            stringByAppendingString: [NSString stringForSpeed: [self uploadRate]]]];

        [fStatusString appendFormat: @" - %@", stringToAppend];
        [fShortStatusString appendString: stringToAppend];
    }
}

- (NSDictionary *) infoForCurrentView
{
    NSMutableDictionary * info = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                    [self name], @"Name",
                                    [NSNumber numberWithBool: [self isSeeding]], @"Seeding",
                                    [NSNumber numberWithFloat: [self progress]], @"Progress",
                                    [NSNumber numberWithBool: [self isActive]], @"Active",
                                    [NSNumber numberWithBool: [self isError]], @"Error", nil];
    
    if (![fDefaults boolForKey: @"SmallView"])
    {
        [info setObject: fIconFlipped forKey: @"Icon"];
        [info setObject: [self progressString] forKey: @"ProgressString"];
        [info setObject: [self statusString] forKey: @"StatusString"];
    }
    else
    {
        [info setObject: fIconSmall forKey: @"Icon"];
        [info setObject: [self remainingTimeString] forKey: @"RemainingTimeString"];
        [info setObject: [self shortStatusString] forKey: @"ShortStatusString"];
    }
    
    if ([fDefaults boolForKey: @"UseAdvancedBar"])
        [info setObject: [self advancedBar] forKey: @"AdvancedBar"];
    
    return info;
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
    fError = NO;
    fWaitToStart = NO;
    
    if ([self isActive])
    {
        tr_torrentStop(fHandle);
        [self update];

        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
    }
}

- (void) stopTransferForQuit
{
    if ([self isActive])
        tr_torrentStop(fHandle);
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

- (void) announce
{
    if (![self isActive])
        return;
    
    tr_manualUpdate(fHandle);
    
    if (fAnnounceDate)
        [fAnnounceDate release];
    fAnnounceDate = [[NSDate date] retain];
}

- (NSDate *) announceDate
{
    return fAnnounceDate;
}

- (BOOL) allDownloaded
{
    return [self progress] >= 1.0;
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

- (int) checkUpload
{
    return fCheckUpload;
}

- (void) setCheckUpload: (int) setting
{
    fCheckUpload = setting;
    [self updateSpeedSetting];
}

- (int) uploadLimit
{
    return fUploadLimit;
}

- (void) setUploadLimit: (int) limit
{
    fUploadLimit = limit;
    [self updateSpeedSetting];
}

- (int) checkDownload
{
    return fCheckDownload;
}

- (void) setCheckDownload: (int) setting
{
    fCheckDownload = setting;
    [self updateSpeedSetting];
}

- (int) downloadLimit
{
    return fDownloadLimit;
}

- (void) setDownloadLimit: (int) limit
{
    fDownloadLimit = limit;
    [self updateSpeedSetting];
}

- (void) updateSpeedSetting
{
    tr_setUseCustomUpload(fHandle, fCheckUpload != NSMixedState);
    tr_setUploadLimit(fHandle, fCheckUpload == NSOnState ? fUploadLimit : -1);
    
    tr_setUseCustomDownload(fHandle, fCheckDownload != NSMixedState);
    tr_setDownloadLimit(fHandle, fCheckDownload == NSOnState ? fDownloadLimit : -1);
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
        [self trashFile: [self publicTorrentLocation]];
}

- (BOOL) alertForRemainingDiskSpace
{
    if ([self allDownloaded])
        return YES;
    
    NSString * volumeName = [[[NSFileManager defaultManager] componentsToDisplayForPath: [self downloadFolder]]
                                                                                                objectAtIndex: 0];
    NSDictionary * fsAttributes = [[NSFileManager defaultManager] fileSystemAttributesAtPath: [self downloadFolder]];
    uint64_t remainingSpace = [[fsAttributes objectForKey: NSFileSystemFreeSize] unsignedLongLongValue],
            torrentRemaining = [self size] - (uint64_t)[self downloadedValid];
    
    /*NSLog(@"Volume: %@", volumeName);
    NSLog(@"Remaining disk space: %qu (%@)", remainingSpace, [NSString stringForFileSize: remainingSpace]);
    NSLog(@"Torrent remaining size: %qu (%@)", torrentRemaining, [NSString stringForFileSize: torrentRemaining]);*/
    
    if (volumeName && remainingSpace <= torrentRemaining)
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: [NSString stringWithFormat:
                                NSLocalizedString(@"Not enough remaining disk space to download \"%@\" completely.",
                                    "Torrent file disk space alert -> title"), [self name]]];
        [alert setInformativeText: [NSString stringWithFormat:
                        NSLocalizedString(@"The transfer will be paused. Clear up space on \"%@\" to continue.",
                                            "Torrent file disk space alert -> message"), volumeName]];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Torrent file disk space alert -> button")];
        [alert addButtonWithTitle: NSLocalizedString(@"Download Anyway", "Torrent file disk space alert -> button")];
        
        BOOL ret = [alert runModal] != NSAlertFirstButtonReturn;
        
        [alert release];
        
        return ret;
    }
    return YES;
}

- (BOOL) alertForFolderAvailable
{
    if (access(tr_torrentGetFolder(fHandle), 0))
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: [NSString stringWithFormat:
                                NSLocalizedString(@"The folder for downloading \"%@\" cannot be found.",
                                    "Folder cannot be found alert -> title"), [self name]]];
        [alert setInformativeText: [NSString stringWithFormat:
                        NSLocalizedString(@"\"%@\" cannot be found. The transfer will be paused.",
                                            "Folder cannot be found alert -> message"), [self downloadFolder]]];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Folder cannot be found alert -> button")];
        [alert addButtonWithTitle: [NSLocalizedString(@"Choose New Location",
                                    "Folder cannot be found alert -> location button") stringByAppendingEllipsis]];
        
        if ([alert runModal] != NSAlertFirstButtonReturn)
        {
            NSOpenPanel * panel = [NSOpenPanel openPanel];
            
            [panel setPrompt: NSLocalizedString(@"Select", "Folder cannot be found alert -> prompt")];
            [panel setAllowsMultipleSelection: NO];
            [panel setCanChooseFiles: NO];
            [panel setCanChooseDirectories: YES];
            [panel setCanCreateDirectories: YES];

            [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the download folder for \"%@\"",
                                "Folder cannot be found alert -> select destination folder"), [self name]]];
            
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
    
    NSString * folder = [[openPanel filenames] objectAtIndex: 0];
    if (fUseIncompleteFolder)
    {
        [fIncompleteFolder release];
        fIncompleteFolder = [folder retain];
        [self setDownloadFolder: nil];
    }
    else
    {
        [fDownloadFolder release];
        fDownloadFolder = folder;
        [self setDownloadFolder: fDownloadFolder];
    }
    
    [self startTransfer];
    [self update];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateInfoSettings" object: nil];
}

- (BOOL) alertForMoveFolderAvailable
{
    if (access([fDownloadFolder UTF8String], 0))
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: [NSString stringWithFormat:
                                NSLocalizedString(@"The folder for moving the completed \"%@\" cannot be found.",
                                    "Move folder cannot be found alert -> title"), [self name]]];
        [alert setInformativeText: [NSString stringWithFormat:
                                NSLocalizedString(@"\"%@\" cannot be found. The file will remain in its current location.",
                                    "Move folder cannot be found alert -> message"), fDownloadFolder]];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Move folder cannot be found alert -> button")];
        
        [alert runModal];
        [alert release];
        
        return NO;
    }
    
    return YES;
}

- (NSImage *) icon
{
    return fIcon;
}

- (NSImage *) iconFlipped
{
    return fIconFlipped;
}

- (NSImage *) iconSmall
{
    return fIconSmall;
}

- (NSString *) name
{
    return [NSString stringWithUTF8String: fInfo->name];
}

- (uint64_t) size
{
    return fInfo->totalSize;
}

- (NSString *) trackerAddress
{
    return [NSString stringWithFormat: @"http://%s:%d", fStat->trackerAddress, fStat->trackerPort];
}

- (NSString *) trackerAddressAnnounce
{
    return [NSString stringWithUTF8String: fStat->trackerAnnounce];
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
    return [NSString stringWithUTF8String: fInfo->hashString];
}

- (BOOL) privateTorrent
{
    return TR_FLAG_PRIVATE & fInfo->flags;
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

- (NSString *) stateString
{
    switch( fStat->status )
    {
        case TR_STATUS_PAUSE:
            return NSLocalizedString(@"Paused", "Torrent -> status string");
            break;

        case TR_STATUS_CHECK:
            return [NSLocalizedString(@"Checking existing files", "Torrent -> status string") stringByAppendingEllipsis];
            break;

        case TR_STATUS_DOWNLOAD:
            return NSLocalizedString(@"Downloading", "Torrent -> status string");
            break;

        case TR_STATUS_SEED:
            return NSLocalizedString(@"Seeding", "Torrent -> status string");
            break;

        case TR_STATUS_STOPPING:
            return [NSLocalizedString(@"Stopping", "Torrent -> status string") stringByAppendingEllipsis];
            break;
        
        default:
            return NSLocalizedString(@"N/A", "Torrent -> status string");
    }
}

- (float) progress
{
    return fStat->progress;
}

- (int) eta
{
    return fStat->eta;
}

- (BOOL) isActive
{
    return fStat->status & TR_STATUS_ACTIVE;
}

- (BOOL) isSeeding
{
    return fStat->status == TR_STATUS_SEED;
}

- (BOOL) isPaused
{
    return fStat->status == TR_STATUS_PAUSE;
}

- (BOOL) isError
{
    return fStat->error;
}

- (NSString *) errorMessage
{
    [NSString stringWithUTF8String: fStat->errorString];
}

- (BOOL) justFinished
{
    return tr_getFinished(fHandle);
}

- (NSArray *) peers
{
    int totalPeers, i;
    tr_peer_stat_t * peers = tr_torrentPeers(fHandle, & totalPeers);
    
    NSMutableArray * peerDics = [NSMutableArray arrayWithCapacity: totalPeers];
    NSMutableDictionary * dic;
    
    tr_peer_stat_t * peer;
    NSString * client;
    for (i = 0; i < totalPeers; i++)
    {
        peer = &peers[i];
        
        dic = [NSMutableDictionary dictionaryWithObjectsAndKeys:
            [NSNumber numberWithBool: peer->isConnected], @"Connected",
            [NSNumber numberWithBool: peer->isIncoming], @"Incoming",
            [NSString stringWithCString: (char *) peer->addr encoding: NSUTF8StringEncoding], @"IP",
            [NSString stringWithCString: (char *) peer->client encoding: NSUTF8StringEncoding], @"Client",
            [NSNumber numberWithFloat: peer->progress], @"Progress",
            [NSNumber numberWithBool: peer->isDownloading], @"UL To",
            [NSNumber numberWithBool: peer->isUploading], @"DL From",
            [NSNumber numberWithInt: peer->port], @"Port", nil];
        
        if (peer->isDownloading)
            [dic setObject: [NSNumber numberWithFloat: peer->uploadToRate] forKey: @"UL To Rate"];
        if (peer->isUploading)
            [dic setObject: [NSNumber numberWithFloat: peer->downloadFromRate] forKey: @"DL From Rate"];
        
        [peerDics addObject: dic];
    }
    
    tr_torrentPeersFree(peers, totalPeers);
    
    return peerDics;
}

- (NSString *) progressString
{
    return fProgressString;
}

- (NSString *) statusString
{
    return fStatusString;
}

- (NSString *) shortStatusString
{
    return fShortStatusString;
}

- (NSString *) remainingTimeString
{
    return fRemainingTimeString;
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

- (int) totalPeers
{
    return fStat->peersTotal;
}

- (int) totalPeersIncoming
{
    return fStat->peersIncoming;
}

- (int) totalPeersOutgoing
{
    return [self totalPeers] - [self totalPeersIncoming];
}

//peers uploading to you
- (int) peersUploading
{
    return fStat->peersUploading;
}

//peers downloading from you
- (int) peersDownloading
{
    return fStat->peersDownloading;
}

- (float) downloadRate
{
    return fStat->rateDownload;
}

- (float) uploadRate
{
    return fStat->rateUpload;
}

- (float) downloadedValid
{
    return [self progress] * [self size];
}

- (uint64_t) downloadedTotal
{
    return fStat->downloaded;
}

- (uint64_t) uploadedTotal
{
    return fStat->uploaded;
}

- (float) swarmSpeed
{
    return fStat->swarmspeed;
}

- (NSNumber *) orderValue
{
    return [NSNumber numberWithInt: fOrderValue];
}

- (void) setOrderValue: (int) orderValue
{
    fOrderValue = orderValue;
}

- (NSArray *) fileList
{
    return fFileList;
}

- (int) fileCount
{
    return fInfo->fileCount;
}

- (NSDate *) date
{
    return fDate;
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

- (NSNumber *) progressSortKey
{
    return [NSNumber numberWithFloat: [self progress]];
}

- (NSNumber *) ratioSortKey
{
    float ratio = [self ratio];
    if (ratio == TR_RATIO_INF)
        return [NSNumber numberWithInt: 999999999]; //this should hopefully be big enough
    else
        return [NSNumber numberWithFloat: [self ratio]];
}

@end

@implementation Torrent (Private)

//if a hash is given, attempt to load that; otherwise, attempt to open file at path
- (id) initWithHash: (NSString *) hashString path: (NSString *) path lib: (tr_handle_t *) lib
        publicTorrent: (NSNumber *) publicTorrent
        date: (NSDate *) date
        ratioSetting: (NSNumber *) ratioSetting ratioLimit: (NSNumber *) ratioLimit
        limitSpeedCustom: (NSNumber *) limitCustom
        checkUpload: (NSNumber *) checkUpload uploadLimit: (NSNumber *) uploadLimit
        checkDownload: (NSNumber *) checkDownload downloadLimit: (NSNumber *) downloadLimit
        waitToStart: (NSNumber *) waitToStart orderValue: (NSNumber *) orderValue
{
    if (!(self = [super init]))
        return nil;

    fLib = lib;
    fDefaults = [NSUserDefaults standardUserDefaults];

    fPublicTorrent = path && (publicTorrent ? [publicTorrent boolValue] : ![fDefaults boolForKey: @"DeleteOriginalTorrent"]);
    if (fPublicTorrent)
        fPublicTorrentLocation = [path retain];

    int error;
    if (hashString)
        fHandle = tr_torrentInitSaved(fLib, [hashString UTF8String], TR_FLAG_SAVE, & error);
    
    if (!fHandle && path)
        fHandle = tr_torrentInit(fLib, [path UTF8String], TR_FLAG_SAVE, & error);

    if (!fHandle)
    {
        [self release];
        return nil;
    }
    
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
    [nc addObserver: self selector: @selector(updateSpeedSetting:)
                name: @"UpdateSpeedSetting" object: nil];
    
    fInfo = tr_torrentInfo( fHandle );

    fDate = date ? [date retain] : [[NSDate alloc] init];
    
    fRatioSetting = ratioSetting ? [ratioSetting intValue] : NSMixedState;
    fRatioLimit = ratioLimit ? [ratioLimit floatValue] : [fDefaults floatForKey: @"RatioLimit"];
    fFinishedSeeding = NO;
    
    fCheckUpload = checkUpload ? [checkUpload intValue] : NSMixedState;
    fUploadLimit = uploadLimit ? [uploadLimit intValue] : [fDefaults integerForKey: @"UploadLimit"];
    fCheckDownload = checkDownload ? [checkDownload intValue] : NSMixedState;
    fDownloadLimit = downloadLimit ? [downloadLimit intValue] : [fDefaults integerForKey: @"DownloadLimit"];
    [self updateSpeedSetting];
    
    fWaitToStart = waitToStart ? [waitToStart boolValue] : [fDefaults boolForKey: @"AutoStartDownload"];
    fOrderValue = orderValue ? [orderValue intValue] : tr_torrentCount(fLib) - 1;
    fError = NO;
    
    NSString * fileType = fInfo->multifile ? NSFileTypeForHFSTypeCode('fldr') : [[self name] pathExtension];
    fIcon = [[NSWorkspace sharedWorkspace] iconForFileType: fileType];
    [fIcon retain];
    
    fIconFlipped = [fIcon copy];
    [fIconFlipped setFlipped: YES];
    
    fIconSmall = [fIconFlipped copy];
    [fIconSmall setScalesWhenResized: YES];
    [fIconSmall setSize: NSMakeSize(16.0, 16.0)];

    fProgressString = [[NSMutableString alloc] initWithCapacity: 50];
    fStatusString = [[NSMutableString alloc] initWithCapacity: 75];
    fShortStatusString = [[NSMutableString alloc] initWithCapacity: 30];
    fRemainingTimeString = [[NSMutableString alloc] initWithCapacity: 30];
    
    fFileList = [self createFileList];
    
    //set up advanced bar
    fBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: nil
        pixelsWide: MAX_PIECES pixelsHigh: BAR_HEIGHT bitsPerSample: 8 samplesPerPixel: 4 hasAlpha: YES
        isPlanar: NO colorSpaceName: NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];
    
    fPieces = malloc(MAX_PIECES);
    int i;
    for (i = 0; i < MAX_PIECES; i++)
        fPieces[i] = BLANK_PIECE;

    [self update];
    return self;
}

- (NSArray *) createFileList
{
    int count = [self fileCount], i;
    tr_file_t * file;
    NSMutableArray * files = [[NSMutableArray alloc] init], * pathComponents;
    NSString * path;
    
    for (i = 0; i < count; i++)
    {
        file = &fInfo->files[i];
        
        pathComponents = [[[NSString stringWithUTF8String: file->name] pathComponents] mutableCopy];
        if (fInfo->multifile)
        {
            path = [pathComponents objectAtIndex: 0];
            [pathComponents removeObjectAtIndex: 0];
        }
        else
            path = @"";
        
        [self insertPath: pathComponents forSiblings: files withParent: nil previousPath: path
                fileSize: file->length state: NSOnState];
        [pathComponents autorelease];
    }
    return files;
}

- (void) insertPath: (NSMutableArray *) components forSiblings: (NSMutableArray *) siblings
        withParent: (NSMutableDictionary *) parent previousPath: (NSString *) previousPath
        fileSize: (uint64_t) size state: (int) state
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
                                [NSNumber numberWithBool: isFolder], @"IsFolder",
                                currentPath, @"Path", nil];
        if (isFolder)
            [dict setObject: [NSMutableArray array] forKey: @"Children"];
        else
            [dict setObject: [NSNumber numberWithUnsignedLongLong: size] forKey: @"Size"];
        
        if (parent)
            [dict setObject: parent forKey: @"Parent"];
        [dict setObject: [NSNumber numberWithInt: state] forKey: @"Check"];
        
        [siblings addObject: dict];
    }
    else
    {
        int dictState = [[dict objectForKey: @"Check"] intValue];
        if (dictState != NSMixedState && dictState != state)
            [dict setObject: [NSNumber numberWithInt: NSMixedState] forKey: @"Check"];
    }
    
    if (isFolder)
    {
        [components removeObjectAtIndex: 0];
        [self insertPath: components forSiblings: [dict objectForKey: @"Children"]
                withParent: dict previousPath: currentPath fileSize: size state: state];
    }
}

- (NSImage *) advancedBar
{
    int h;
    uint32_t * p;
    uint8_t * bitmapData = [fBitmap bitmapData];
    int bytesPerRow = [fBitmap bytesPerRow];
    
    int pieceCount = [self pieceCount];
    int8_t * piecesAvailablity = malloc(pieceCount);
    [self getAvailability: piecesAvailablity size: pieceCount];
    
    //lines 2 to 14: blue, green, or gray depending on whether we have the piece or not
    int i, index = 0;
    float increment = (float)pieceCount / (float)MAX_PIECES, indexValue = 0;
    uint32_t color;
    BOOL change;
    for (i = 0; i < MAX_PIECES; i++)
    {
        change = NO;
        if (piecesAvailablity[index] < 0)
        {
            if (fPieces[i] != -1)
            {
                color = kBlue;
                fPieces[i] = -1;
                change = YES;
            }
        }
        else if (piecesAvailablity[index] == 0)
        {
            if (fPieces[i] != 0)
            {
                color = kGray;
                fPieces[i] = 0;
                change = YES;
            }
        }
        else
        {
            if (piecesAvailablity[index] == 1)
            {
                if (fPieces[i] != 1)
                {
                    color = kGreen1;
                    fPieces[i] = 1;
                    change = YES;
                }
            }
            else if (piecesAvailablity[index] == 2)
            {
                if (fPieces[i] != 2)
                {
                    color = kGreen2;
                    fPieces[i] = 2;
                    change = YES;
                }
            }
            else
            {
                if (fPieces[i] != 3)
                {
                    color = kGreen3;
                    fPieces[i] = 3;
                    change = YES;
                }
            }
        }
        
        if (change)
        {
            //point to pixel (i, 2) and draw "vertically"
            p = (uint32_t *)(bitmapData + 2 * bytesPerRow) + i;
            for (h = 2; h < BAR_HEIGHT; h++)
            {
                p[0] = color;
                p = (uint32_t *)((uint8_t *)p + bytesPerRow);
            }
        }
        
        indexValue += increment;
        index = (int)indexValue;
    }
    
    //determine percentage finished and available
    float * piecesFinished = malloc(pieceCount * sizeof(float));
    [self getAmountFinished: piecesFinished size: pieceCount];
    
    float finished = 0, available = 0;
    for (i = 0; i < pieceCount; i++)
    {
        finished += piecesFinished[i];
        if (piecesAvailablity[i] > 0)
            available += 1.0 - piecesFinished[i];
    }
    
    int have = rintf((float)MAX_PIECES * finished / (float)pieceCount),
        avail = rintf((float)MAX_PIECES * available / (float)pieceCount);
    if (have + avail > MAX_PIECES) //case if both end in .5 and all pieces are available
        avail--;
    
    //first two lines: dark blue to show progression, green to show available
    p = (uint32_t *)bitmapData;
    for (i = 0; i < have; i++)
    {
        p[i] = kBlue2;
        p[i + bytesPerRow / 4] = kBlue2;
    }
    for (; i < avail + have; i++)
    {
        p[i] = kGreen3;
        p[i + bytesPerRow / 4] = kGreen3;
    }
    for (; i < MAX_PIECES; i++)
    {
        p[i] = kWhite;
        p[i + bytesPerRow / 4] = kWhite;
    }
    
    free(piecesAvailablity);
    free(piecesFinished);
    
    //actually draw image
    NSImage * bar = [[NSImage alloc] initWithSize: [fBitmap size]];
    [bar addRepresentation: fBitmap];
    [bar setScalesWhenResized: YES];
    
    return [bar autorelease];
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

@end
