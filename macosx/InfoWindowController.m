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

#import "InfoWindowController.h"
#import "StringAdditions.h"

#define RATIO_NO_CHECK_TAG 0
#define RATIO_GLOBAL_TAG 1
#define RATIO_CHECK_TAG 2

#define MIN_WINDOW_WIDTH 270
#define MAX_WINDOW_WIDTH 2000

#define TAB_INFO_IDENT @"Info"
#define TAB_ACTIVITY_IDENT @"Activity"
#define TAB_OPTIONS_IDENT @"Options"
#define TAB_FILES_IDENT @"Files"

//15 spacing at the bottom of each tab
#define TAB_INFO_HEIGHT 182.0
#define TAB_ACTIVITY_HEIGHT 198.0
#define TAB_OPTIONS_HEIGHT 116.0
#define TAB_FILES_HEIGHT 250.0

@interface InfoWindowController (Private)

- (void) setWindowForTab: (NSString *) identifier animate: (BOOL) animate;

@end

@implementation InfoWindowController

- (void) awakeFromNib
{
    fAppIcon = [[NSApp applicationIconImage] copy];
    
    fTorrents = [[NSArray alloc] init];
    fFiles = [[NSMutableArray alloc] initWithCapacity: 6];
    [fFileTable setDoubleAction: @selector(revealFile:)];
    
    //window location and size
    NSPanel * window = (NSPanel *)[self window];
    
    [window setBecomesKeyOnlyIfNeeded: YES];
    
    [window setFrameAutosaveName: @"InspectorWindowFrame"];
    [window setFrameUsingName: @"InspectorWindowFrame"];
    
    NSString * identifier = [[NSUserDefaults standardUserDefaults] stringForKey: @"InfoTab"];
    [fTabView selectTabViewItemWithIdentifier: identifier];
    [self setWindowForTab: identifier animate: NO];
}

- (void) dealloc
{
    [fTorrents release];
    [fFiles release];

    [fAppIcon release];
    [super dealloc];
}

- (void) updateInfoForTorrents: (NSArray *) torrents
{
    [fTorrents release];
    fTorrents = [torrents retain];

    int numberSelected = [fTorrents count];
    if (numberSelected != 1)
    {
        if (numberSelected > 0)
        {
            [fNameField setStringValue: [NSString stringWithFormat:
                            @"%d Torrents Selected", numberSelected]];
        
            uint64_t size = 0;
            NSEnumerator * enumerator = [torrents objectEnumerator];
            Torrent * torrent;
            while ((torrent = [enumerator nextObject]))
                size += [torrent size];
            
            [fSizeField setStringValue: [[NSString stringForFileSize: size]
                                stringByAppendingString: @" Total"]];
        }
        else
        {
            [fNameField setStringValue: @"No Torrents Selected"];
            [fSizeField setStringValue: @""];
            
/*
            [fDownloadRateField setStringValue: @""];
            [fUploadRateField setStringValue: @""];
*/
            [fDownloadedValidField setStringValue: @""];
            [fDownloadedTotalField setStringValue: @""];
            [fUploadedTotalField setStringValue: @""];
        }
        
        [fImageView setImage: fAppIcon];
        
        [fNameField setToolTip: nil];

        [fTrackerField setStringValue: @""];
        [fTrackerField setToolTip: nil];
        [fAnnounceField setStringValue: @""];
        [fAnnounceField setToolTip: nil];
        [fPieceSizeField setStringValue: @""];
        [fPiecesField setStringValue: @""];
        [fHashField setStringValue: @""];
        [fHashField setToolTip: nil];
        
        [fTorrentLocationField setStringValue: @""];
        [fTorrentLocationField setToolTip: nil];
        [fDataLocationField setStringValue: @""];
        [fDataLocationField setToolTip: nil];
        [fDateStartedField setStringValue: @""];
        
/*
        [fStateField setStringValue: @""];
        [fPercentField setStringValue: @""];
*/
        [fRatioField setStringValue: @""];
        
        [fSeedersField setStringValue: @""];
        [fLeechersField setStringValue: @""];
        [fConnectedPeersField setStringValue: @""];
        [fDownloadingFromField setStringValue: @""];
        [fUploadingToField setStringValue: @""];
    }
    else
    {    
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
        [fImageView setImage: [torrent icon]];
        
        NSString * name = [torrent name];
        [fNameField setStringValue: name];
        [fNameField setToolTip: name];
        [fSizeField setStringValue: [NSString stringForFileSize: [torrent size]]];
        
        NSString * tracker = [torrent tracker],
                * announce = [torrent announce],
                * hashString = [torrent hashString];
        [fTrackerField setStringValue: tracker];
        [fTrackerField setToolTip: tracker];
        [fAnnounceField setStringValue: announce];
        [fAnnounceField setToolTip: announce];
        [fPieceSizeField setStringValue: [NSString stringForFileSize: [torrent pieceSize]]];
        [fPiecesField setIntValue: [torrent pieceCount]];
        [fHashField setStringValue: hashString];
        [fHashField setToolTip: hashString];
        
        [fTorrentLocationField setStringValue: [torrent torrentLocationString]];
        [fTorrentLocationField setToolTip: [torrent torrentLocation]];
        [fDataLocationField setStringValue: [[torrent dataLocation] stringByAbbreviatingWithTildeInPath]];
        [fDataLocationField setToolTip: [torrent dataLocation]];
        [fDateStartedField setObjectValue: [torrent date]];
    }
    [self updateInfoStats];

    //set file table
    [fFiles removeAllObjects];
    
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [fFiles addObjectsFromArray: [torrent fileList]];
    
    [fFileTable deselectAll: nil];
    [fFileTable reloadData];
    
    //set wait to start
    if (numberSelected == 1)
    {
        #warning make work for multiple torrents
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        [fWaitToStartButton setState: [torrent waitingToStart]];
        
        #warning disable if actively downloading or finished
        [fWaitToStartButton setEnabled:
            [[[NSUserDefaults standardUserDefaults] stringForKey: @"StartSetting"] isEqualToString: @"Wait"]];
    }
    else
    {
        [fWaitToStartButton setState: NSOffState];
        [fWaitToStartButton setEnabled: NO];
    }
    
    //set ratio settings
    if (numberSelected > 0)
    {
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        Torrent * torrent = [enumerator nextObject]; //first torrent
        const int INVALID = -99;
        int ratioSetting = [torrent stopRatioSetting];
        float ratioLimit = [torrent ratioLimit];
        
        while ((ratioSetting != INVALID || ratioLimit != INVALID)
                && (torrent = [enumerator nextObject]))
        {
            if (ratioSetting != INVALID && ratioSetting != [torrent stopRatioSetting])
                ratioSetting = INVALID;
            
            if (ratioLimit != INVALID && ratioLimit != [torrent ratioLimit])
                ratioLimit = INVALID;
        }
        
        [fRatioMatrix setEnabled: YES];
        
        if (ratioSetting == RATIO_CHECK)
        {
            [fRatioMatrix selectCellWithTag: RATIO_CHECK_TAG];
            [fRatioLimitField setEnabled: YES];
        }
        else
        {
            if (ratioSetting == RATIO_NO_CHECK)
                [fRatioMatrix selectCellWithTag: RATIO_NO_CHECK_TAG];
            else if (ratioSetting == RATIO_GLOBAL)
                [fRatioMatrix selectCellWithTag: RATIO_GLOBAL_TAG];
            else
                [fRatioMatrix deselectAllCells];
            
            [fRatioLimitField setEnabled: NO];
        }
        
        if (ratioLimit != INVALID)
            [fRatioLimitField setFloatValue: ratioLimit];
        else
            [fRatioLimitField setStringValue: @""];
    }
    else
    {
        [fRatioMatrix deselectAllCells];
        [fRatioMatrix setEnabled: NO];
        
        [fRatioLimitField setEnabled: NO];
        [fRatioLimitField setStringValue: @""];
    }
}

- (void) updateInfoStats
{
    int numberSelected = [fTorrents count];
    if (numberSelected > 0)
    {
        //float downloadRate = 0, uploadRate = 0;
        float downloadedValid = 0;
        uint64_t downloadedTotal = 0, uploadedTotal = 0;
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
        {
            /*downloadRate += [torrent downloadRate];
            uploadRate += [torrent uploadRate];
            */
            downloadedValid += [torrent downloadedValid];
            downloadedTotal += [torrent downloadedTotal];
            uploadedTotal += [torrent uploadedTotal];
        }
/*
        [fDownloadRateField setStringValue: [NSString stringForSpeed: downloadRate]];
        [fUploadRateField setStringValue: [NSString stringForSpeed: uploadRate]];
*/
        [fDownloadedValidField setStringValue: [NSString stringForFileSize: downloadedValid]];
        [fDownloadedTotalField setStringValue: [NSString stringForFileSize: downloadedTotal]];
        [fUploadedTotalField setStringValue: [NSString stringForFileSize: uploadedTotal]];
    
        if (numberSelected == 1)
        {
            torrent = [fTorrents objectAtIndex: 0];
/*
            [fStateField setStringValue: [torrent state]];
            [fPercentField setStringValue: [NSString stringWithFormat:
                                            @"%.2f%%", 100.0 * [torrent progress]]];
*/
            int seeders = [torrent seeders], leechers = [torrent leechers];
            [fSeedersField setStringValue: seeders < 0 ?
                @"N/A" : [NSString stringWithInt: seeders]];
            [fLeechersField setStringValue: leechers < 0 ?
                @"N/A" : [NSString stringWithInt: leechers]];
            
            BOOL active = [torrent isActive];
            
            [fConnectedPeersField setStringValue: active ? [NSString
                    stringWithInt: [torrent totalPeers]] : @"N/A"];
            [fDownloadingFromField setStringValue: active ? [NSString
                    stringWithInt: [torrent peersUploading]] : @"N/A"];
            [fUploadingToField setStringValue: active ? [NSString
                    stringWithInt: [torrent peersDownloading]] : @"N/A"];
            
            [fRatioField setStringValue: [NSString stringForRatioWithDownload:
                                        downloadedTotal upload: uploadedTotal]];
        }
    }
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];
    
    if (action == @selector(revealFile:))
        return [fFileTable numberOfSelectedRows] > 0 &&
            [[[fTabView selectedTabViewItem] identifier] isEqualToString: TAB_FILES_IDENT];
        
    return YES;
}

- (NSRect) windowWillUseStandardFrame: (NSWindow *) window defaultFrame: (NSRect) defaultFrame
{
    NSRect windowRect = [window frame];
    windowRect.size.width = [window minSize].width;    
    return windowRect;
}

- (void) tabView: (NSTabView *) tabView didSelectTabViewItem: (NSTabViewItem *) tabViewItem
{
    NSString * identifier = [tabViewItem identifier];
    [self setWindowForTab: identifier animate: YES];
    [[NSUserDefaults standardUserDefaults] setObject: identifier forKey: @"InfoTab"];
}

- (void) setWindowForTab: (NSString *) identifier animate: (BOOL) animate
{
    float height;
    if ([identifier isEqualToString: TAB_ACTIVITY_IDENT])
        height = TAB_ACTIVITY_HEIGHT;
    else if ([identifier isEqualToString: TAB_OPTIONS_IDENT])
        height = TAB_OPTIONS_HEIGHT;
    else if ([identifier isEqualToString: TAB_FILES_IDENT])
        height = TAB_FILES_HEIGHT;
    else
        height = TAB_INFO_HEIGHT;
    
    NSWindow * window = [self window];
    NSRect frame = [window frame];
    NSView * view = [[fTabView selectedTabViewItem] view];
    
    float difference = height - [view frame].size.height;
    frame.origin.y -= difference;
    frame.size.height += difference;
    
    if (animate)
    {
        [view setHidden: YES];
        [window setFrame: frame display: YES animate: YES];
        [view setHidden: NO];
    }
    else
        [window setFrame: frame display: YES];
    
    [window setMinSize: NSMakeSize(MIN_WINDOW_WIDTH, frame.size.height)];
    [window setMaxSize: NSMakeSize(MAX_WINDOW_WIDTH, frame.size.height)];
}

- (void) setNextTab
{
    if ([fTabView indexOfTabViewItem: [fTabView selectedTabViewItem]]
                                    == [fTabView numberOfTabViewItems] - 1)
        [fTabView selectFirstTabViewItem: nil];
    else
        [fTabView selectNextTabViewItem: nil];
}

- (void) setPreviousTab
{
    if ([fTabView indexOfTabViewItem: [fTabView selectedTabViewItem]] == 0)
        [fTabView selectLastTabViewItem: nil];
    else
        [fTabView selectPreviousTabViewItem: nil];
}

- (int) numberOfRowsInTableView: (NSTableView *) tableView
{
    return [fFiles count];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn:
                    (NSTableColumn *) column row: (int) row
{
    NSString * file = [fFiles objectAtIndex: row];
    if ([[column identifier] isEqualToString: @"Icon"])
        return [[NSWorkspace sharedWorkspace] iconForFileType: [file pathExtension]];
    else
        return [file lastPathComponent];
}

//only called on >= 10.4
- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell
        rect: (NSRectPointer) rect tableColumn: (NSTableColumn *) column
        row: (int) row mouseLocation: (NSPoint) mouseLocation
{
    return [fFiles objectAtIndex: row];
}

- (void) revealFile: (id) sender
{
    NSIndexSet * indexSet = [fFileTable selectedRowIndexes];
    unsigned int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [[NSWorkspace sharedWorkspace] selectFile: [fFiles objectAtIndex: i]
            inFileViewerRootedAtPath: nil];
}

- (void) setRatioCheck: (id) sender
{
    int ratioSetting, tag = [[fRatioMatrix selectedCell] tag];
    if (tag == RATIO_CHECK_TAG)
        ratioSetting = RATIO_CHECK;
    else if (tag == RATIO_NO_CHECK_TAG)
        ratioSetting = RATIO_NO_CHECK;
    else
        ratioSetting = RATIO_GLOBAL;

    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [torrent setStopRatioSetting: ratioSetting];
    
    [self setRatioLimit: fRatioLimitField];
    [fRatioLimitField setEnabled: tag == RATIO_CHECK_TAG];
}

- (void) setRatioLimit: (id) sender
{
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];

    float ratioLimit = [sender floatValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%.2f", ratioLimit]]
            || ratioLimit < 0)
    {
        NSBeep();
        float ratioLimit = [[enumerator nextObject] ratioLimit]; //use first torrent
        while ((torrent = [enumerator nextObject]))
            if (ratioLimit != [torrent ratioLimit])
            {
                [sender setStringValue: @""];
                return;
            }
        
        [sender setFloatValue: ratioLimit];
    }
    else
    {
        while ((torrent = [enumerator nextObject]))
            [torrent setRatioLimit: ratioLimit];
    }
}

- (void) setWaitToStart: (id) sender
{
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [torrent setWaitToStart: [sender state]];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"TorrentStartSettingChange" object: torrent];
}

@end
