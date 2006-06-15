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

@interface Torrent (Private)

- (id) initWithPath: (NSString *) path lib: (tr_handle_t *) lib date: (NSDate *) date
        stopRatioSetting: (NSNumber *) stopRatioSetting ratioLimit: (NSNumber *) ratioLimit;
- (id) initWithHash: (NSString *) hashString path: (NSString *) path lib: (tr_handle_t *) lib
        date: (NSDate *) date stopRatioSetting: (NSNumber *) stopRatioSetting
        ratioLimit: (NSNumber *) ratioLimit;
- (id) initForSuccessWithPath: (NSString *) path date: (NSDate *) date stopRatioSetting: (NSNumber *)
            stopRatioSetting ratioLimit: (NSNumber *) ratioLimit;

- (void) trashFile: (NSString *) path;

@end


@implementation Torrent

- (id) initWithPath: (NSString *) path lib: (tr_handle_t *) lib
{
    fDefaults = [NSUserDefaults standardUserDefaults];

    fPrivateTorrent = [fDefaults boolForKey: @"SavePrivateTorrent"];
    fPublicTorrent = !fPrivateTorrent || ![fDefaults boolForKey: @"DeleteOriginalTorrent"];

    id torrent = [self initWithPath: path lib: lib date: nil
                    stopRatioSetting: nil ratioLimit: nil];
    
    if (torrent)
    {
        if (!fPublicTorrent)
        {
            [self trashFile: path];
            
            [fPublicTorrentLocation release];
            fPublicTorrentLocation = nil;
        }
    }
    
    return torrent;
}

- (id) initWithHistory: (NSDictionary *) history lib: (tr_handle_t *) lib
{
    fDefaults = [NSUserDefaults standardUserDefaults];

    id torrent;
    NSNumber * privateCopy, * publicCopy;
    fPrivateTorrent = (privateCopy = [history objectForKey: @"PrivateCopy"]) && [privateCopy boolValue];
    fPublicTorrent = !fPrivateTorrent || ((publicCopy = [history objectForKey: @"PublicCopy"])
                                            && [publicCopy boolValue]);
    
    //load from saved torrent file if set to, otherwise try to load from where torrent file should be
    if (fPrivateTorrent)
        torrent = [self initWithHash: [history objectForKey: @"TorrentHash"]
            path: [history objectForKey: @"TorrentPath"]
            lib: lib date: [history objectForKey: @"Date"]
            stopRatioSetting: [history objectForKey: @"StopRatioSetting"]
            ratioLimit: [history objectForKey: @"RatioLimit"]];
    else
        torrent = [self initWithPath: [history objectForKey: @"TorrentPath"]
            lib: lib date: [history objectForKey: @"Date"]
            stopRatioSetting: [history objectForKey: @"StopRatioSetting"]
            ratioLimit: [history objectForKey: @"RatioLimit"]];
            
    if (torrent)
    {
        NSString * downloadFolder;
        if (!(downloadFolder = [history objectForKey: @"DownloadFolder"]))
            downloadFolder = [[fDefaults stringForKey: @"DownloadFolder"]
                                stringByExpandingTildeInPath];
        [self setDownloadFolder: downloadFolder];

        NSString * paused;
        if (!(paused = [history objectForKey: @"Paused"]) || [paused isEqualToString: @"NO"])
            [self start];
    }
    return torrent;
}

- (NSDictionary *) history
{
    NSMutableDictionary * history = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                    [NSNumber numberWithBool: fPrivateTorrent], @"PrivateCopy",
                    [NSNumber numberWithBool: fPublicTorrent], @"PublicCopy",
                    [self downloadFolder], @"DownloadFolder",
                    [self isActive] ? @"NO" : @"YES", @"Paused",
                    [self date], @"Date",
                    [NSNumber numberWithInt: fStopRatioSetting], @"StopRatioSetting",
                    [NSNumber numberWithFloat: fRatioLimit], @"RatioLimit", nil];
            
    if (fPrivateTorrent)
        [history setObject: [self hashString] forKey: @"TorrentHash"];

    if (fPublicTorrent)
        [history setObject: [self publicTorrentLocation] forKey: @"TorrentPath"];
    
    return history;
}

- (void) dealloc
{
    if( fHandle )
    {
        tr_torrentClose( fLib, fHandle );
        
        if (fPublicTorrentLocation)
            [fPublicTorrentLocation release];
        
        [fDate release];
        [fIcon release];
        [fIconFlipped release];
        [fProgressString release];
        [fStatusString release];
    }
    [super dealloc];
}

- (void) setDownloadFolder: (NSString *) path
{
    tr_torrentSetFolder( fHandle, [path UTF8String] );
}

- (NSString *) downloadFolder
{
    return [NSString stringWithUTF8String: tr_torrentGetFolder( fHandle )];
}

- (void) getAvailability: (int8_t *) tab size: (int) size
{
    tr_torrentAvailability( fHandle, tab, size );
}

- (void) update
{
    fStat = tr_torrentStat( fHandle );
    
    if ([self isSeeding]
        && (fStopRatioSetting == RATIO_CHECK && [self ratio] >= fRatioLimit)
        || (fStopRatioSetting == RATIO_GLOBAL && [fDefaults boolForKey: @"RatioCheck"]
            && [self ratio] >= [fDefaults floatForKey: @"RatioLimit"]))
    {
        [self stop];
        [self setStopRatioSetting: RATIO_NO_CHECK];
        fFinishedSeeding = YES;
        
        fStat = tr_torrentStat( fHandle );
        
        [[NSNotificationCenter defaultCenter] postNotificationName:
            @"TorrentRatioChanged" object: self];
    }

    [fProgressString setString: @""];
    if ([self progress] < 1.0)
        [fProgressString appendFormat: @"%@ of %@ (%.2f%%)", [NSString stringForFileSize:
                [self downloadedValid]], [NSString stringForFileSize: [self size]], 100 * [self progress]];
    else
        [fProgressString appendFormat: @"%@, uploaded %@ (ratio: %@)", [NSString stringForFileSize:
                [self size]], [NSString stringForFileSize: [self uploadedTotal]],
                [NSString stringForRatioWithDownload: [self downloadedTotal] upload: [self uploadedTotal]]];

    switch( fStat->status )
    {
        case TR_STATUS_PAUSE:
            [fStatusString setString: fFinishedSeeding ? @"Seeding Complete" : @"Paused"];
            break;

        case TR_STATUS_CHECK:
            [fStatusString setString: [@"Checking existing files" stringByAppendingEllipsis]];
            break;

        case TR_STATUS_DOWNLOAD:
            [fStatusString setString: @""];
            [fStatusString appendFormat:
                @"Downloading from %d of %d peer%s",
                [self peersUploading], [self totalPeers],
                ([self totalPeers] == 1) ? "" : "s"];
            
            int eta = fStat->eta;
            if (eta < 0)
                [fProgressString appendString: @" - remaining time unknown"];
            else
            {
                if (eta < 60)
                    [fProgressString appendFormat: @" - %d sec remaining", eta];
                else if (eta < 3600)
                    [fProgressString appendFormat: @" - %d min %02d sec remaining",
                                                    eta / 60, eta % 60];
                else
                    [fProgressString appendFormat: @" - %d hr %02d min remaining",
                                                    eta / 3600, (eta / 60) % 60];
            }
            break;

        case TR_STATUS_SEED:
            [fStatusString setString: @""];
            [fStatusString appendFormat:
                @"Seeding to %d of %d peer%s",
                [self peersDownloading], [self totalPeers],
                ([self totalPeers] == 1) ? "" : "s"];
            break;

        case TR_STATUS_STOPPING:
            [fStatusString setString: [@"Stopping" stringByAppendingEllipsis]];
            break;
    }
    
    if( fStat->error & TR_ETRACKER )
    {
        [fStatusString setString: [@"Error: " stringByAppendingString:
            [NSString stringWithUTF8String: fStat->trackerError]]];
    }

    if ([self isActive])
    {
        [fStatusString appendString: @" - "];
        if ([self progress] < 1.0)
            [fStatusString appendFormat: @"DL: %@, ", [NSString stringForSpeed: [self downloadRate]]];
        [fStatusString appendString: [@"UL: " stringByAppendingString:
                                                [NSString stringForSpeed: [self uploadRate]]]];
    }
}

- (void) start
{
    if( fStat->status & TR_STATUS_INACTIVE )
    {
        tr_torrentStart( fHandle );
        fFinishedSeeding = NO;
    }
}

- (void) stop
{
    if( fStat->status & TR_STATUS_ACTIVE )
    {
        tr_torrentStop( fHandle );
    }
}

- (void) removeForever
{
    if (fInfo->flags & TR_FSAVEPRIVATE)
        tr_torrentRemoveSaved(fHandle);
}

- (void) sleep
{
    if( ( fResumeOnWake = ( fStat->status & TR_STATUS_ACTIVE ) ) )
    {
        [self stop];
    }
}

- (void) wakeUp
{
    if( fResumeOnWake )
    {
        [self start];
    }
}

- (float) ratio
{
    float downloaded = [self downloadedTotal];
    return downloaded > 0 ? (float)[self uploadedTotal] / downloaded : -1;
}

- (int) stopRatioSetting
{
	return fStopRatioSetting;
}

- (void) setStopRatioSetting: (int) setting
{
    fStopRatioSetting = setting;
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

- (void) reveal
{
    [[NSWorkspace sharedWorkspace] selectFile: [self dataLocation]
        inFileViewerRootedAtPath: nil];
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

- (NSImage *) icon
{
    return fIcon;
}

- (NSImage *) iconFlipped
{
    return fIconFlipped;
}

- (NSString *) name
{
    return [NSString stringWithUTF8String: fInfo->name];
}

- (uint64_t) size
{
    return fInfo->totalSize;
}

- (NSString *) tracker
{
    return [NSString stringWithFormat: @"%s:%d",
            fInfo->trackerAddress, fInfo->trackerPort];
}

- (NSString *) announce
{
    return [NSString stringWithUTF8String: fInfo->trackerAnnounce];
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

- (NSString *) torrentLocation
{
    return [NSString stringWithUTF8String: fInfo->torrent];
}

- (NSString *) publicTorrentLocation
{
    return fPublicTorrentLocation;
}

- (BOOL) publicTorrent
{
    return fPublicTorrent;
}

- (BOOL) privateTorrent
{
    return fPrivateTorrent;
}

- (NSString *) dataLocation
{
    return [[self downloadFolder] stringByAppendingPathComponent: [self name]];
}

- (NSString *) state
{
    switch( fStat->status )
    {
        case TR_STATUS_PAUSE:
            return @"Paused";
            break;

        case TR_STATUS_CHECK:
            return [@"Checking existing files" stringByAppendingEllipsis];
            break;

        case TR_STATUS_DOWNLOAD:
            return @"Downloading";
            break;

        case TR_STATUS_SEED:
            return @"Seeding";
            break;

        case TR_STATUS_STOPPING:
            return [@"Stopping" stringByAppendingEllipsis];
            break;
        
        default:
            return @"N/A";
    }
}

- (float) progress
{
    return fStat->progress;
}

- (BOOL) isActive
{
    return ( fStat->status & TR_STATUS_ACTIVE );
}

- (BOOL) isSeeding
{
    return ( fStat->status == TR_STATUS_SEED );
}

- (BOOL) isPaused
{
    return ( fStat->status == TR_STATUS_PAUSE );
}

- (BOOL) justFinished
{
    return tr_getFinished( fHandle );
}

- (NSString *) progressString
{
    return fProgressString;
}

- (NSString *) statusString
{
    return fStatusString;
}

- (int) seeders
{
    return fStat->seeders;
}

- (int) leechers
{
    return fStat->leechers;
}

- (int) totalPeers
{
    return fStat->peersTotal;
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

- (NSArray *) fileList
{
    int count = fInfo->fileCount, i;
    NSMutableArray * files = [NSMutableArray arrayWithCapacity: count];
    for (i = 0; i < count; i++)
        [files addObject: [[self downloadFolder] stringByAppendingPathComponent:
            [NSString stringWithUTF8String: fInfo->files[i].name]]];
    return files;
}

- (NSDate *) date
{
    return fDate;
}

- (NSNumber *) stateSortKey
{
    if (fStat->status & TR_STATUS_INACTIVE)
        return [NSNumber numberWithInt: 0];
    else if (fStat->status == TR_STATUS_SEED)
        return [NSNumber numberWithInt: 1];
    else
        return [NSNumber numberWithInt: 2];
}

- (NSNumber *) progressSortKey
{
    return [NSNumber numberWithFloat: [self progress]];
}

@end


@implementation Torrent (Private)

- (id) initWithPath: (NSString *) path lib: (tr_handle_t *) lib date: (NSDate *) date
        stopRatioSetting: (NSNumber *) stopRatioSetting ratioLimit: (NSNumber *) ratioLimit
{
    if (!(self = [super init]))
        return nil;

    fLib = lib;

    int error;
    if (!path || !(fHandle = tr_torrentInit(fLib, [path UTF8String],
                    fPrivateTorrent ? TR_FSAVEPRIVATE : 0, & error)))
    {
        [self release];
        return nil;
    }

    return [self initForSuccessWithPath: path date: date stopRatioSetting: stopRatioSetting ratioLimit: ratioLimit];
}

- (id) initWithHash: (NSString *) hashString path: (NSString *) path lib: (tr_handle_t *) lib
            date: (NSDate *) date stopRatioSetting: (NSNumber *) stopRatioSetting
            ratioLimit: (NSNumber *) ratioLimit
{
    if (!(self = [super init]))
        return nil;

    fLib = lib;

    int error;
    if (!hashString || !(fHandle = tr_torrentInitSaved(fLib, [hashString UTF8String], TR_FSAVEPRIVATE, & error)))
    {
        [self release];
        return nil;
    }
    
    return [self initForSuccessWithPath: path date: date stopRatioSetting: stopRatioSetting ratioLimit: ratioLimit];
}

- (id) initForSuccessWithPath: (NSString *) path date: (NSDate *) date stopRatioSetting: (NSNumber *)
            stopRatioSetting ratioLimit: (NSNumber *) ratioLimit
{
    fInfo = tr_torrentInfo( fHandle );

    if (path)
        fPublicTorrentLocation = [path retain];

    fDate = date ? [date retain] : [[NSDate alloc] init];
    
    fStopRatioSetting = stopRatioSetting ? [stopRatioSetting intValue] : -1;
    fRatioLimit = ratioLimit ? [ratioLimit floatValue] : [fDefaults floatForKey: @"RatioLimit"];
    fFinishedSeeding = NO;
    
    NSString * fileType = ( fInfo->fileCount > 1 ) ?
        NSFileTypeForHFSTypeCode('fldr') : [[self name] pathExtension];
    fIcon = [[NSWorkspace sharedWorkspace] iconForFileType: fileType];
    [fIcon retain];
    
    fIconFlipped = [fIcon copy];
    [fIconFlipped setFlipped: YES];

    fProgressString = [[NSMutableString alloc] initWithCapacity: 50];
    fStatusString = [[NSMutableString alloc] initWithCapacity: 75];

    [self update];
    return self;
}


- (void) trashFile: (NSString *) path
{
    if( ![[NSWorkspace sharedWorkspace] performFileOperation:
           NSWorkspaceRecycleOperation source:
           [path stringByDeletingLastPathComponent]
           destination: @""
           files: [NSArray arrayWithObject: [path lastPathComponent]]
           tag: nil] )
    {
        /* We can't move it to the trash, let's try just to delete it
           (will work if it is on a remote volume) */
        if( ![[NSFileManager defaultManager]
                removeFileAtPath: path handler: nil] )
        {
            NSLog( [@"Could not trash " stringByAppendingString: path] );
        }
    }
}

@end
