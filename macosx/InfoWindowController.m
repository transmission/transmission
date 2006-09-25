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
#define TAB_PEERS_IDENT @"Peers"
#define TAB_FILES_IDENT @"Files"
#define TAB_OPTIONS_IDENT @"Options"

//15 spacing at the bottom of each tab
#define TAB_INFO_HEIGHT 182.0
#define TAB_ACTIVITY_HEIGHT 109.0
#define TAB_PEERS_HEIGHT 236.0
#define TAB_FILES_HEIGHT 255.0
#define TAB_OPTIONS_HEIGHT 83.0

@interface InfoWindowController (Private)

- (void) reloadPeerTable;
- (void) setWindowForTab: (NSString *) identifier animate: (BOOL) animate;
- (NSArray *) peerSortDescriptors;

@end

@implementation InfoWindowController

- (void) awakeFromNib
{
    fAppIcon = [[NSApp applicationIconImage] copy];
    fDotGreen = [NSImage imageNamed: @"GreenDot.tiff"];
    fDotRed = [NSImage imageNamed: @"RedDot.tiff"];
    fCheckImage = [NSImage imageNamed: @"NSMenuCheckmark"];
    
    fTorrents = [[NSArray alloc] init];
    fPeers = [[NSMutableArray alloc] initWithCapacity: 30];
    fFiles = [[NSMutableArray alloc] initWithCapacity: 6];
    [fFileTable setDoubleAction: @selector(revealFile:)];
    
    //window location and size
    NSPanel * window = (NSPanel *)[self window];
    
    [window setBecomesKeyOnlyIfNeeded: YES];
    
    [window setFrameAutosaveName: @"InspectorWindowFrame"];
    [window setFrameUsingName: @"InspectorWindowFrame"];
    
    //select tab
    NSString * identifier = [[NSUserDefaults standardUserDefaults] stringForKey: @"InfoTab"];
    if ([fTabView indexOfTabViewItemWithIdentifier: identifier] == NSNotFound)
        identifier = TAB_INFO_IDENT;
    
    [fTabView selectTabViewItemWithIdentifier: identifier];
    [self setWindowForTab: identifier animate: NO];
    
    //initially sort peer table by IP
    if ([[fPeerTable sortDescriptors] count] == 0)
        [fPeerTable setSortDescriptors: [NSArray arrayWithObject: [[fPeerTable tableColumnWithIdentifier: @"IP"]
                                            sortDescriptorPrototype]]];
    
    [self updateInfoForTorrents: [NSArray array]];
}

- (void) dealloc
{
    [fTorrents release];
    [fPeers release];
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
            [fNameField setStringValue: [NSString stringWithFormat: @"%d Torrents Selected", numberSelected]];
        
            uint64_t size = 0;
            NSEnumerator * enumerator = [torrents objectEnumerator];
            Torrent * torrent;
            while ((torrent = [enumerator nextObject]))
                size += [torrent size];
            
            [fSizeField setStringValue: [[NSString stringForFileSize: size] stringByAppendingString: @" Total"]];
        }
        else
        {
            [fNameField setStringValue: @"No Torrents Selected"];
            [fSizeField setStringValue: @""];

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
        
        //don't allow empty fields to be selected
        [fTrackerField setSelectable: NO];
        [fAnnounceField setSelectable: NO];
        [fHashField setSelectable: NO];
        [fTorrentLocationField setSelectable: NO];
        [fDataLocationField setSelectable: NO];
        
        [fStateField setStringValue: @""];
        [fRatioField setStringValue: @""];
        
        [fSeedersField setStringValue: @""];
        [fLeechersField setStringValue: @""];
        [fConnectedPeersField setStringValue: @""];
        [fDownloadingFromField setStringValue: @""];
        [fUploadingToField setStringValue: @""];
        [fSwarmSpeedField setStringValue: @""];
        
        [fPeers removeAllObjects];
        [fPeerTable reloadData];
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
        
        //allow these fields to be selected
        [fTrackerField setSelectable: YES];
        [fAnnounceField setSelectable: YES];
        [fHashField setSelectable: YES];
        [fTorrentLocationField setSelectable: YES];
        [fDataLocationField setSelectable: YES];
    }
    
    //update stats and settings
    [self updateInfoStats];
    [self updateInfoSettings];

    //set file table
    [fFiles removeAllObjects];
    
    if (numberSelected > 0)
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            [fFiles addObjectsFromArray: [torrent fileList]];
    
        [fFileTableStatusField setStringValue: [NSString stringWithFormat: @"%d file%s", [fFiles count],
                                                [fFiles count] == 1 ? "" : "s"]];
    }
    else
        [fFileTableStatusField setStringValue: @"info not available"];
    
    [fFileTable deselectAll: nil];
    [fFileTable reloadData];
}

- (void) updateInfoStats
{
    int numberSelected = [fTorrents count];
    if (numberSelected == 0)
        return;
    
    float downloadedValid = 0;
    uint64_t downloadedTotal = 0, uploadedTotal = 0;
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        downloadedValid += [torrent downloadedValid];
        downloadedTotal += [torrent downloadedTotal];
        uploadedTotal += [torrent uploadedTotal];
    }

    [fDownloadedValidField setStringValue: [NSString stringForFileSize: downloadedValid]];
    [fDownloadedTotalField setStringValue: [NSString stringForFileSize: downloadedTotal]];
    [fUploadedTotalField setStringValue: [NSString stringForFileSize: uploadedTotal]];

    if (numberSelected == 1)
    {
        torrent = [fTorrents objectAtIndex: 0];
        
        //append percentage to amount downloaded if 1 torrent
        [fDownloadedValidField setStringValue: [[fDownloadedValidField stringValue]
                                        stringByAppendingFormat: @" (%.2f%%)", 100.0 * [torrent progress]]];
        
        [fStateField setStringValue: [torrent stateString]];
        
        int seeders = [torrent seeders], leechers = [torrent leechers];
        [fSeedersField setStringValue: seeders < 0 ? @"" : [NSString stringWithInt: seeders]];
        [fLeechersField setStringValue: leechers < 0 ? @"" : [NSString stringWithInt: leechers]];
        
        BOOL active = [torrent isActive];
        
        [fConnectedPeersField setStringValue: active ? [NSString stringWithInt: [torrent totalPeers]] : @""];
        [fDownloadingFromField setStringValue: active ? [NSString stringWithInt: [torrent peersUploading]] : @""];
        [fUploadingToField setStringValue: active ? [NSString stringWithInt: [torrent peersDownloading]] : @""];
        
        [fRatioField setStringValue: [NSString stringForRatioWithDownload: downloadedTotal upload: uploadedTotal]];
        
        [fSwarmSpeedField setStringValue: [torrent isActive] ? [NSString stringForSpeed: [torrent swarmSpeed]] : @""];
        
        //set peers table if visible
        if ([[[fTabView selectedTabViewItem] identifier] isEqualToString: TAB_PEERS_IDENT])
            [self reloadPeerTable];
    }
}

- (void) updateInfoSettings
{
    int numberSelected = [fTorrents count];

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
    
    [self updateInfoStats];
}

//requires a non-empty torrent array
- (void) reloadPeerTable
{
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    
    [fPeers setArray: [torrent peers]];
    [fPeers sortUsingDescriptors: [self peerSortDescriptors]];
    
    [fPeerTable reloadData];
    #warning use [fpeers count]
    //[fPeerTableStatusField setStringValue: [NSString stringWithFormat: @"%d of %d connected",
    //                                        [torrent totalPeers], [fPeers count]]];
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
    else if ([identifier isEqualToString: TAB_PEERS_IDENT])
    {
        height = TAB_PEERS_HEIGHT;
        
        if ([fTorrents count] == 1)
            [self reloadPeerTable]; //initial update of peer table
    }
    else if ([identifier isEqualToString: TAB_FILES_IDENT])
        height = TAB_FILES_HEIGHT;
    else if ([identifier isEqualToString: TAB_OPTIONS_IDENT])
        height = TAB_OPTIONS_HEIGHT;
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
    if ([fTabView indexOfTabViewItem: [fTabView selectedTabViewItem]] == [fTabView numberOfTabViewItems] - 1)
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
    if (tableView == fPeerTable)
        return [fPeers count];
    else
        return [fFiles count];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) column row: (int) row
{
    NSString * ident = [column identifier];
    if (tableView == fPeerTable)
    {
        NSDictionary * peer = [fPeers objectAtIndex: row];
        
        if ([ident isEqualToString: @"Connected"])
            return [[peer objectForKey: @"Connected"] boolValue] ? fDotGreen : fDotRed;
        else if ([ident isEqualToString: @"UL To"])
            return [[peer objectForKey: @"UL To"] boolValue] ? fCheckImage : nil;
        else if ([ident isEqualToString: @"DL From"])
            return [[peer objectForKey: @"DL From"] boolValue] ? fCheckImage : nil;
        else if ([ident isEqualToString: @"Client"])
            return [peer objectForKey: @"Client"];
        else
            return [peer objectForKey: @"IP"];
    }
    else
    {
        NSDictionary * file = [fFiles objectAtIndex: row];
        if ([ident isEqualToString: @"Icon"])
            return [[NSWorkspace sharedWorkspace] iconForFileType: [[file objectForKey: @"Name"] pathExtension]];
        else if ([ident isEqualToString: @"Size"])
            return [NSString stringForFileSize: [[file objectForKey: @"Size"] unsignedLongLongValue]];
        else
            return [[file objectForKey: @"Name"] lastPathComponent];
    }
}

- (void) tableView: (NSTableView *) tableView didClickTableColumn: (NSTableColumn *) tableColumn
{
    if (tableView == fPeerTable)
    {
        [fPeers sortUsingDescriptors: [self peerSortDescriptors]];
        [tableView reloadData];
    }
}

- (BOOL) tableView: (NSTableView *) tableView shouldSelectRow:(int) row
{
    return tableView != fPeerTable;
}

//only called on >= 10.4
- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell
        rect: (NSRectPointer) rect tableColumn: (NSTableColumn *) column
        row: (int) row mouseLocation: (NSPoint) mouseLocation
{
    if (tableView == fFileTable)
    {
        NSDictionary * file = [fFiles objectAtIndex: row];
        if ([[column identifier] isEqualToString: @"Size"])
            return [[[file objectForKey: @"Size"] stringValue] stringByAppendingString: @" bytes"];
        else
            return [file objectForKey: @"Name"];
    }
    else
        return nil;
}

- (NSArray *) peerSortDescriptors
{
    NSMutableArray * descriptors = [NSMutableArray array];
    
    NSArray * oldDescriptors = [fPeerTable sortDescriptors];
    if ([oldDescriptors count] > 0)
        [descriptors addObject: [oldDescriptors objectAtIndex: 0]];
    
    [descriptors addObject: [[fPeerTable tableColumnWithIdentifier: @"IP"] sortDescriptorPrototype]];
    
    return descriptors;
}

- (void) revealFile: (id) sender
{
    NSIndexSet * indexSet = [fFileTable selectedRowIndexes];
    unsigned int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [[NSWorkspace sharedWorkspace] selectFile: [[fFiles objectAtIndex: i] objectForKey: @"Name"]
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
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%.2f", ratioLimit]] || ratioLimit < 0)
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

@end
