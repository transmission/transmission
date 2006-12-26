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

#define RATIO_GLOBAL_TAG 0
#define RATIO_NO_CHECK_TAG 1
#define RATIO_CHECK_TAG 2

#define MIN_WINDOW_WIDTH 300
#define MAX_WINDOW_WIDTH 5000

#define TAB_INFO_IDENT @"Info"
#define TAB_ACTIVITY_IDENT @"Activity"
#define TAB_PEERS_IDENT @"Peers"
#define TAB_FILES_IDENT @"Files"
#define TAB_OPTIONS_IDENT @"Options"

//15 spacing at the bottom of each tab
#define TAB_INFO_HEIGHT 284.0
#define TAB_ACTIVITY_HEIGHT 170.0
#define TAB_PEERS_HEIGHT 268.0
#define TAB_FILES_HEIGHT 268.0
#define TAB_OPTIONS_HEIGHT 83.0

#define INVALID -99

@interface InfoWindowController (Private)

- (void) updateInfoGeneral;
- (void) updateInfoActivity;
- (void) updateInfoPeers;

- (void) setWindowForTab: (NSString *) identifier animate: (BOOL) animate;
- (NSArray *) peerSortDescriptors;

@end

@implementation InfoWindowController

- (id) initWithWindowNibName: (NSString *) name
{
    if ((self = [super initWithWindowNibName: name]))
    {
        fAppIcon = [[NSApp applicationIconImage] copy];
        fDotGreen = [NSImage imageNamed: @"GreenDot.tiff"];
        fDotRed = [NSImage imageNamed: @"RedDot.tiff"];
    }
    return self;
}

- (void) awakeFromNib
{
    fPeers = [[NSMutableArray alloc] initWithCapacity: 75];
    fFiles = [[NSMutableArray alloc] initWithCapacity: 15];
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
    if (fTorrents)
        [fTorrents release];
    fTorrents = [torrents retain];

    int numberSelected = [fTorrents count];
    if (numberSelected != 1)
    {
        if (numberSelected > 0)
        {
            [fNameField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d Torrents Selected",
                                            "Inspector -> above tabs -> selected torrents"), numberSelected]];
        
            uint64_t size = 0;
            NSEnumerator * enumerator = [torrents objectEnumerator];
            Torrent * torrent;
            while ((torrent = [enumerator nextObject]))
                size += [torrent size];
            
            [fSizeField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ Total",
                "Inspector -> above tabs -> total size (several torrents selected)"), [NSString stringForFileSize: size]]];
        }
        else
        {
            [fNameField setStringValue: NSLocalizedString(@"No Torrents Selected",
                                                            "Inspector -> above tabs -> selected torrents")];
            [fSizeField setStringValue: @""];
    
            [fDownloadedValidField setStringValue: @""];
            [fDownloadedTotalField setStringValue: @""];
            [fUploadedTotalField setStringValue: @""];
        }
        
        [fImageView setImage: fAppIcon];
        
        [fNameField setToolTip: nil];

        [fTrackerField setStringValue: @""];
        [fTrackerField setToolTip: nil];
        [fPiecesField setStringValue: @""];
        [fHashField setStringValue: @""];
        [fHashField setToolTip: nil];
        [fSecureField setStringValue: @""];
        [fCommentView setString: @""];
        
        [fCreatorField setStringValue: @""];
        [fDateCreatedField setStringValue: @""];
        
        [fTorrentLocationField setStringValue: @""];
        [fTorrentLocationField setToolTip: nil];
        [fDataLocationField setStringValue: @""];
        [fDataLocationField setToolTip: nil];
        [fDateStartedField setStringValue: @""];
        [fCommentView setSelectable: NO];
        
        [fRevealDataButton setHidden: YES];
        [fRevealTorrentButton setHidden: YES];
        
        //don't allow empty fields to be selected
        [fTrackerField setSelectable: NO];
        [fHashField setSelectable: NO];
        [fCreatorField setSelectable: NO];
        [fTorrentLocationField setSelectable: NO];
        [fDataLocationField setSelectable: NO];
        
        [fStateField setStringValue: @""];
        [fRatioField setStringValue: @""];
        
        [fSeedersField setStringValue: @""];
        [fLeechersField setStringValue: @""];
        [fCompletedFromTrackerField setStringValue: @""];
        [fConnectedPeersField setStringValue: @""];
        [fDownloadingFromField setStringValue: @""];
        [fUploadingToField setStringValue: @""];
        [fSwarmSpeedField setStringValue: @""];
        [fErrorMessageView setString: @""];
        [fErrorMessageView setSelectable: NO];
        
        [fPeers removeAllObjects];
        [fPeerTable reloadData];
        
        [fPiecesView setTorrent: nil];
    }
    else
    {    
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
        [fImageView setImage: [torrent icon]];
        
        NSString * name = [torrent name];
        [fNameField setStringValue: name];
        [fNameField setToolTip: name];
        [fSizeField setStringValue: [NSString stringForFileSize: [torrent size]]];
        
        NSString * hashString = [torrent hashString],
                * commentString = [torrent comment];
        [fPiecesField setStringValue: [NSString stringWithFormat: @"%d, %@", [torrent pieceCount],
                                        [NSString stringForFileSize: [torrent pieceSize]]]];
        [fHashField setStringValue: hashString];
        [fHashField setToolTip: hashString];
        [fSecureField setStringValue: [torrent privateTorrent]
                        ? NSLocalizedString(@"Private Torrent", "Inspector -> is private torrent")
                        : NSLocalizedString(@"Public Torrent", "Inspector -> is not private torrent")];
        [fCommentView setString: commentString];
        
        [fCreatorField setStringValue: [torrent creator]];
        [fDateCreatedField setObjectValue: [torrent dateCreated]];
        
        BOOL publicTorrent = [torrent publicTorrent];
        [fTorrentLocationField setStringValue: publicTorrent
                    ? [[torrent publicTorrentLocation] stringByAbbreviatingWithTildeInPath]
                    : NSLocalizedString(@"Transmission Support Folder", "Torrent -> location when deleting original")];
        if (publicTorrent)
            [fTorrentLocationField setToolTip: [NSString stringWithFormat: @"%@\n\n%@",
                        [torrent publicTorrentLocation], [torrent torrentLocation]]];
        else
            [fTorrentLocationField setToolTip: [torrent torrentLocation]];
        
        [fDateStartedField setObjectValue: [torrent date]];
        
        [fRevealDataButton setHidden: NO];
        [fRevealTorrentButton setHidden: ![torrent publicTorrent]];
        
        //allow these fields to be selected
        [fTrackerField setSelectable: YES];
        [fHashField setSelectable: YES];
        [fCommentView setSelectable: YES];
        [fCreatorField setSelectable: YES];
        [fTorrentLocationField setSelectable: YES];
        [fDataLocationField setSelectable: YES];
        
        [fPiecesView setTorrent: torrent];
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
        
        if ([fFiles count] > 1)
            [fFileTableStatusField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d files",
                                        "Inspector -> Files tab -> bottom text (number of files)"), [fFiles count]]];
        else
            [fFileTableStatusField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d file",
                                        "Inspector -> Files tab -> bottom text (number of files)"), [fFiles count]]];
    }
    else
        [fFileTableStatusField setStringValue: NSLocalizedString(@"info not available",
                                        "Inspector -> Files tab -> bottom text (number of files)")];
    
    [fFileTable deselectAll: nil];
    [fFileTable reloadData];
}

- (void) updateInfoStats
{
    if ([[[fTabView selectedTabViewItem] identifier] isEqualToString: TAB_ACTIVITY_IDENT])
        [self updateInfoActivity];
    else if ([[[fTabView selectedTabViewItem] identifier] isEqualToString: TAB_PEERS_IDENT])
        [self updateInfoPeers];
    else if ([[[fTabView selectedTabViewItem] identifier] isEqualToString: TAB_INFO_IDENT])
        [self updateInfoGeneral];
    else;
}

- (void) updateInfoGeneral
{   
    int numberSelected = [fTorrents count];
    if (numberSelected != 1)
        return;
    
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSString * tracker = [[torrent trackerAddress] stringByAppendingString: [torrent trackerAddressAnnounce]];
    [fTrackerField setStringValue: tracker];
    [fTrackerField setToolTip: tracker];
}

- (void) updateInfoActivity
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
        [fRatioField setStringValue: [NSString stringForRatioWithDownload: downloadedTotal upload: uploadedTotal]];
        [fSwarmSpeedField setStringValue: [torrent isActive] ? [NSString stringForSpeed: [torrent swarmSpeed]] : @""];
        
        NSString * errorMessage = [torrent errorMessage];
        if (![errorMessage isEqualToString: [fErrorMessageView string]])
        {
            [fErrorMessageView setString: errorMessage];
            [fErrorMessageView setSelectable: ![errorMessage isEqualToString: @""]];
        }
        
        [fPiecesView updateView: NO];
    }
}

- (void) updateInfoPeers
{
    if ([fTorrents count] != 1)
        return;
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    
    int seeders = [torrent seeders], leechers = [torrent leechers], downloaded = [torrent completedFromTracker];
    [fSeedersField setStringValue: seeders < 0 ? @"" : [NSString stringWithInt: seeders]];
    [fLeechersField setStringValue: leechers < 0 ? @"" : [NSString stringWithInt: leechers]];
    [fCompletedFromTrackerField setStringValue: downloaded < 0 ? @"" : [NSString stringWithInt: downloaded]];
    
    BOOL active = [torrent isActive];
    [fConnectedPeersField setStringValue: active ? [NSString stringWithFormat: NSLocalizedString(@"%d (%d incoming)",
                                                                                "Inspector -> Peers tab -> connected"),
                                                    [torrent totalPeers], [torrent totalPeersIncoming]]: @""];
    [fDownloadingFromField setStringValue: active ? [NSString stringWithInt: [torrent peersUploading]] : @""];
    [fUploadingToField setStringValue: active ? [NSString stringWithInt: [torrent peersDownloading]] : @""];
    
    [fPeers setArray: [torrent peers]];
    [fPeers sortUsingDescriptors: [self peerSortDescriptors]];
    
    [fPeerTable reloadData];
}

- (void) updateInfoSettings
{
    int numberSelected = [fTorrents count];

    if (numberSelected > 0)
    {
        Torrent * torrent;
        
        if (numberSelected == 1)
        {
            torrent = [fTorrents objectAtIndex: 0];
            [fDataLocationField setStringValue: [[torrent dataLocation] stringByAbbreviatingWithTildeInPath]];
            [fDataLocationField setToolTip: [torrent dataLocation]];
        }
        
        //set ratio settings
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        torrent = [enumerator nextObject]; //first torrent
        
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
    [self updateInfoStats];
    
    float height;
    if ([identifier isEqualToString: TAB_ACTIVITY_IDENT])
    {
        height = TAB_ACTIVITY_HEIGHT;
        [fPiecesView updateView: YES];
    }
    else if ([identifier isEqualToString: TAB_PEERS_IDENT])
    {
        height = TAB_PEERS_HEIGHT;
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
        else if ([ident isEqualToString: @"Client"])
            return [peer objectForKey: @"Client"];
        else if  ([ident isEqualToString: @"Progress"])
            return [peer objectForKey: @"Progress"];
        else if ([ident isEqualToString: @"UL To"])
            return [[peer objectForKey: @"UL To"] boolValue]
                    ? [NSString stringForSpeedAbbrev: [[peer objectForKey: @"UL To Rate"] floatValue]] : @"";
        else if ([ident isEqualToString: @"DL From"])
            return [[peer objectForKey: @"DL From"] boolValue]
                    ? [NSString stringForSpeedAbbrev: [[peer objectForKey: @"DL From Rate"] floatValue]] : @"";
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

- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell
        rect: (NSRectPointer) rect tableColumn: (NSTableColumn *) column
        row: (int) row mouseLocation: (NSPoint) mouseLocation
{
    if (tableView == fFileTable)
    {
        NSDictionary * file = [fFiles objectAtIndex: row];
        if ([[column identifier] isEqualToString: @"Size"])
            return [[[file objectForKey: @"Size"] stringValue] stringByAppendingString: NSLocalizedString(@" bytes",
                                                                        "Inspector -> Files tab -> table row tooltip")];
        else
            return [file objectForKey: @"Name"];
    }
    else if (tableView == fPeerTable)
    {
        NSDictionary * peerDic = [fPeers objectAtIndex: row];
        return [NSString stringWithFormat: NSLocalizedString(@"Progress: %.1f%%"
                    "\nPort: %@"
                    "\nFrom %@ connection", "Inspector -> Peers tab -> table row tooltip"),
                    [[peerDic objectForKey: @"Progress"] floatValue] * 100.0,
                    [peerDic objectForKey: @"Port"],
                    [[peerDic objectForKey: @"Incoming"] boolValue]
                        ? NSLocalizedString(@"incoming", "Inspector -> Peers tab -> table row tooltip")
                        : NSLocalizedString(@"outgoing", "Inspector -> Peers tab -> table row tooltip")];
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

- (void) revealTorrentFile: (id) sender
{
    if ([fTorrents count] > 0)
        [[fTorrents objectAtIndex: 0] revealPublicTorrent];
}

- (void) revealDataFile: (id) sender
{
    if ([fTorrents count] > 0)
        [[fTorrents objectAtIndex: 0] revealData];
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
