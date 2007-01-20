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

#define MIN_WINDOW_WIDTH 300
#define MAX_WINDOW_WIDTH 5000

#define FILE_ROW_SMALL_HEIGHT 18.0

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
#define TAB_OPTIONS_HEIGHT 142.0

#define INVALID -99

@interface InfoWindowController (Private)

- (void) updateInfoGeneral;
- (void) updateInfoActivity;
- (void) updateInfoPeers;
- (void) updateInfoSettings;

- (void) setWindowForTab: (NSString *) identifier animate: (BOOL) animate;
- (NSArray *) peerSortDescriptors;

- (void) setFileCheckState: (int) state forItem: (NSMutableDictionary *) item;
- (NSMutableDictionary *) resetFileCheckStateForItemParent: (NSMutableDictionary *) originalChild;

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
    
    //set file table
    [fFileOutline setDoubleAction: @selector(revealFile:)];
    
    //set blank inspector
    [self updateInfoForTorrents: [NSArray array]];
}

- (void) dealloc
{
    [fTorrents release];
    if (fPeers)
        [fPeers release];
    if (fFiles)
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
        
        [fPiecesView setTorrent: nil];
        
        if (fPeers)
        {
            [fPeers release];
            fPeers = nil;
        }
        
        if (fFiles)
        {
            [fFiles release];
            fFiles = nil;
        }
        [fFileTableStatusField setStringValue: NSLocalizedString(@"info not available",
                                        "Inspector -> Files tab -> bottom text (number of files)")];
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
        
        //set file table
        [fFileOutline deselectAll: nil];
        if (fFiles)
            [fFiles release];
        fFiles = [[torrent fileList] retain];
        
        int fileCount = [torrent fileCount];
        if (fileCount != 1)
            [fFileTableStatusField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d files total",
                                "Inspector -> Files tab -> bottom text (number of files)"), fileCount]];
        else
            [fFileTableStatusField setStringValue: NSLocalizedString(@"1 file total",
                                "Inspector -> Files tab -> bottom text (number of files)")];
    }
    
    //update stats and settings
    [self updateInfoStats];
    [self updateInfoSettings];
    
    [fPeerTable reloadData];
    [fFileOutline reloadData];
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
    
    NSString * location = [torrent dataLocation];
    [fDataLocationField setStringValue: [location stringByAbbreviatingWithTildeInPath]];
    [fDataLocationField setToolTip: location];
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
        [fRatioField setStringValue: [NSString stringForRatio: [torrent ratio]]];
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
    
    if (fPeers)
        [fPeers release];
    fPeers = [[[torrent peers] sortedArrayUsingDescriptors: [self peerSortDescriptors]] retain];
    
    [fPeerTable reloadData];
}

- (void) updateInfoSettings
{
    int numberSelected = [fTorrents count];

    if (numberSelected > 0)
    {
        Torrent * torrent;
        
        //set bandwidth limits
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        torrent = [enumerator nextObject]; //first torrent
        
        int limitCustom = [torrent customLimitSetting] ? 1 : 0,
            checkUpload = [torrent checkUpload] ? 1 : 0,
            checkDownload = [torrent checkDownload] ? 1 : 0,
            uploadLimit = [torrent uploadLimit],
            downloadLimit = [torrent downloadLimit];
        
        while ((limitCustom != INVALID
                || checkUpload != INVALID || uploadLimit != INVALID
                || checkDownload != INVALID || downloadLimit != INVALID)
                && (torrent = [enumerator nextObject]))
        {
            if (limitCustom != INVALID && limitCustom != ([torrent customLimitSetting] ? 1 : 0))
                limitCustom = INVALID;
            
            if (checkUpload != INVALID && checkUpload != ([torrent checkUpload] ? 1 : 0))
                checkUpload = INVALID;
            
            if (uploadLimit != INVALID && uploadLimit != [torrent uploadLimit])
                uploadLimit = INVALID;
            
            if (checkDownload != INVALID && checkDownload != ([torrent checkDownload] ? 1 : 0))
                checkDownload = INVALID;
            
            if (downloadLimit != INVALID && downloadLimit != [torrent downloadLimit])
                downloadLimit = INVALID;
        }
        
        [fLimitCustomCheck setEnabled: YES];
        [fLimitCustomCheck setState: limitCustom == INVALID ? NSMixedState
                                : (limitCustom == 1 ? NSOnState : NSOffState)];
        
        [fUploadLimitCheck setEnabled: limitCustom == 1];
        [fUploadLimitLabel setEnabled: limitCustom == 1];
        [fUploadLimitCheck setState: checkUpload == INVALID ? NSMixedState
                                : (checkUpload == 1 ? NSOnState : NSOffState)];
        
        [fDownloadLimitCheck setEnabled: limitCustom == 1];
        [fDownloadLimitLabel setEnabled: limitCustom == 1];
        [fDownloadLimitCheck setState: checkDownload == INVALID ? NSMixedState
                                : (checkDownload == 1 ? NSOnState : NSOffState)];
        
        [fUploadLimitField setEnabled: limitCustom == 1 && checkUpload == 1];
        if (uploadLimit != INVALID)
            [fUploadLimitField setIntValue: uploadLimit];
        else
            [fUploadLimitField setStringValue: @""];

        [fDownloadLimitField setEnabled: limitCustom == 1 && checkDownload == 1];
        if (downloadLimit != INVALID)
            [fDownloadLimitField setIntValue: downloadLimit];
        else
            [fDownloadLimitField setStringValue: @""];
        
        //set ratio settings
        enumerator = [fTorrents objectEnumerator];
        torrent = [enumerator nextObject]; //first torrent
        
        int ratioCustom = [torrent customRatioSetting] ? 1 : 0,
            ratioStop = [torrent shouldStopAtRatio];
        float ratioLimit = [torrent ratioLimit];
        
        while ((ratioCustom != INVALID || ratioStop != INVALID || ratioLimit != INVALID)
                && (torrent = [enumerator nextObject]))
        {
            if (ratioCustom != INVALID && ratioCustom != ([torrent customRatioSetting] ? 1 : 0))
                ratioCustom = INVALID;
            
            if (ratioStop != INVALID && ratioStop != ([torrent shouldStopAtRatio] ? 1 : 0))
                ratioStop = INVALID;
            
            if (ratioLimit != INVALID && ratioLimit != [torrent ratioLimit])
                ratioLimit = INVALID;
        }
        
        [fRatioCustomCheck setEnabled: YES];
        [fRatioCustomCheck setState: ratioCustom == INVALID ? NSMixedState
                                : (ratioCustom == 1 ? NSOnState : NSOffState)];
        
        [fRatioStopCheck setEnabled: ratioCustom == 1];
        [fRatioStopCheck setState: ratioStop == INVALID ? NSMixedState
                                : (ratioStop == 1 ? NSOnState : NSOffState)];
        
        [fRatioLimitField setEnabled: ratioCustom == 1 && ratioStop == 1];
        if (ratioLimit != INVALID)
            [fRatioLimitField setFloatValue: ratioLimit];
        else
            [fRatioLimitField setStringValue: @""];
    }
    else
    {
        [fLimitCustomCheck setEnabled: NO];
        [fLimitCustomCheck setState: NSOffState];
        
        [fUploadLimitCheck setEnabled: NO];
        [fUploadLimitCheck setState: NSOffState];
        [fUploadLimitField setEnabled: NO];
        [fUploadLimitField setStringValue: @""];
        [fUploadLimitLabel setEnabled: NO];
        
        [fDownloadLimitCheck setEnabled: NO];
        [fDownloadLimitCheck setState: NSOffState];
        [fDownloadLimitField setEnabled: NO];
        [fDownloadLimitField setStringValue: @""];
        [fDownloadLimitLabel setEnabled: NO];
        
        [fRatioCustomCheck setEnabled: NO];
        [fRatioCustomCheck setState: NSOffState];
        [fRatioStopCheck setEnabled: NO];
        [fRatioStopCheck setState: NSOffState];
        [fRatioLimitField setEnabled: NO];
        [fRatioLimitField setStringValue: @""];
    }
    
    [self updateInfoStats];
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];
    
    if (action == @selector(revealFile:))
        return [fFileOutline numberOfSelectedRows] > 0 &&
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
        height = TAB_PEERS_HEIGHT;
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
        return fPeers ? [fPeers count] : 0;
    return 0;
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) column row: (int) row
{
    if (tableView == fPeerTable)
    {
        NSString * ident = [column identifier];
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
    return nil;
}

- (void) tableView: (NSTableView *) tableView didClickTableColumn: (NSTableColumn *) tableColumn
{
    if (tableView == fPeerTable)
    {
        if (fPeers)
        {
            NSArray * oldPeers = fPeers;
            fPeers = [[fPeers sortedArrayUsingDescriptors: [self peerSortDescriptors]] retain];
            [oldPeers release];
            [tableView reloadData];
        }
    }
}

- (BOOL) tableView: (NSTableView *) tableView shouldSelectRow:(int) row
{
    return tableView != fPeerTable;
}

- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) column row: (int) row mouseLocation: (NSPoint) mouseLocation
{
    if (tableView == fPeerTable)
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
    return nil;
}

- (int) outlineView: (NSOutlineView *) outlineView numberOfChildrenOfItem: (id) item
{
    if (!item)
        return fFiles ? [fFiles count] : 1;
    return [[item objectForKey: @"IsFolder"] boolValue] ? [[item objectForKey: @"Children"] count] : 0;
}

- (BOOL) outlineView: (NSOutlineView *) outlineView isItemExpandable: (id) item 
{
    return item && [[item objectForKey: @"IsFolder"] boolValue];
}

- (id) outlineView: (NSOutlineView *) outlineView child: (int) index ofItem: (id) item
{
    if (!fFiles)
        return nil;
    
    return [(item ? [item objectForKey: @"Children"] : fFiles) objectAtIndex: index];
}

- (id) outlineView: (NSOutlineView *) outlineView objectValueForTableColumn: (NSTableColumn *) tableColumn
            byItem: (id) item
{
    if (!item)
        return nil;
    
    if ([[tableColumn identifier] isEqualToString: @"Check"])
        return [item objectForKey: @"Check"];
    else
        return item;
}

- (void) outlineView: (NSOutlineView *) outlineView willDisplayCell: (id) cell
            forTableColumn: (NSTableColumn *) tableColumn item:(id) item
{
    if ([[tableColumn identifier] isEqualToString: @"Name"])
    {
        if (!item)
            return;
        
        NSImage * icon = [[NSWorkspace sharedWorkspace] iconForFileType: ![[item objectForKey: @"IsFolder"] boolValue]
                            ? [[item objectForKey: @"Name"] pathExtension] : NSFileTypeForHFSTypeCode('fldr')];
        [cell setImage: icon];
    }
    else if ([[tableColumn identifier] isEqualToString: @"Check"])
    {
        [(NSButtonCell *)cell setImagePosition: item ? NSImageOnly : NSNoImage];
        [cell setEnabled: NO];
    }
    else;
}

- (void) outlineView: (NSOutlineView *) outlineView setObjectValue: (id) object
        forTableColumn: (NSTableColumn *) tableColumn byItem: (id) item
{
    int state = [object intValue] != NSOffState;
    
    [self setFileCheckState: state forItem: item];
    NSMutableDictionary * topItem = [self resetFileCheckStateForItemParent: item];
    
    [fFileOutline reloadItem: topItem reloadChildren: YES];
}

- (void) setFileCheckState: (int) state forItem: (NSMutableDictionary *) item
{
    [item setObject: [NSNumber numberWithInt: state] forKey: @"Check"];
    
    if (![[item objectForKey: @"IsFolder"] boolValue])
        return;
    
    NSMutableDictionary * child;
    NSEnumerator * enumerator = [[item objectForKey: @"Children"] objectEnumerator];
    while ((child = [enumerator nextObject]))
        if ([[child objectForKey: @"Check"] intValue] != state)
            [self setFileCheckState: state forItem: child];
}

- (NSMutableDictionary *) resetFileCheckStateForItemParent: (NSMutableDictionary *) originalChild
{
    NSMutableDictionary * item;
    if (!(item = [originalChild objectForKey: @"Parent"]))
        return originalChild;
    
    int state = INVALID;
    
    NSMutableDictionary * child;
    NSEnumerator * enumerator = [[item objectForKey: @"Children"] objectEnumerator];
    while ((child = [enumerator nextObject]))
    {
        if (state == INVALID)
        {
            state = [[child objectForKey: @"Check"] intValue];
            if (state == NSMixedState)
                break;
        }
        else if (state != [[child objectForKey: @"Check"] intValue])
        {
            state = NSMixedState;
            break;
        }
        else;
    }
    
    if ([[item objectForKey: @"Check"] intValue] != state)
    {
        [item setObject: [NSNumber numberWithInt: state] forKey: @"Check"];
        return [self resetFileCheckStateForItemParent: item];
    }
    else
        return originalChild;
}

- (NSString *) outlineView: (NSOutlineView *) outlineView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) tableColumn item: (id) item mouseLocation: (NSPoint) mouseLocation
{
    if (!item)
        return nil;
    
    NSString * ident = [tableColumn identifier];
    if ([ident isEqualToString: @"Name"])
        return [[[fTorrents objectAtIndex: 0] downloadFolder] stringByAppendingPathComponent: [item objectForKey: @"Path"]];
    else
        return nil;
}

- (float) outlineView: (NSOutlineView *) outlineView heightOfRowByItem: (id) item
{
    if ([[item objectForKey: @"IsFolder"] boolValue])
        return FILE_ROW_SMALL_HEIGHT;
    else
        return [outlineView rowHeight];
}

- (BOOL) outlineView: (NSOutlineView *) outlineView shouldSelectItem: (id) item
{
    return item != nil;
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
    if (!fFiles)
        return;
    
    NSString * folder = [[fTorrents objectAtIndex: 0] downloadFolder];
    NSIndexSet * indexes = [fFileOutline selectedRowIndexes];
    int i;
    for (i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
        [[NSWorkspace sharedWorkspace] selectFile: [folder stringByAppendingPathComponent:
                [[fFileOutline itemAtRow: i] objectForKey: @"Path"]] inFileViewerRootedAtPath: nil];
}

- (void) setLimitCustom: (id) sender
{
    BOOL custom = [sender state] != NSOffState;
    if (custom)
        [sender setState: NSOnState];

    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [torrent setCustomLimitSetting: custom];
    
    [fUploadLimitCheck setEnabled: custom];
    [fUploadLimitLabel setEnabled: custom];
    [fUploadLimitField setEnabled: custom && [fUploadLimitCheck state] == NSOnState];
    
    [fDownloadLimitCheck setEnabled: custom];
    [fDownloadLimitLabel setEnabled: custom];
    [fDownloadLimitField setEnabled: custom && [fDownloadLimitCheck state] == NSOnState];
}

- (void) setLimitCheck: (id) sender
{
    BOOL upload = sender == fUploadLimitCheck,
        limit = [sender state] != NSOffState;
    
    if (limit)
        [sender setState: NSOnState];
    
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        upload ? [torrent setLimitUpload: limit] : [torrent setLimitDownload: limit];
    
    NSTextField * field = upload ? fUploadLimitField : fDownloadLimitField;
    
    [field setEnabled: limit];
}

- (void) setSpeedLimit: (id) sender
{
    BOOL upload = sender == fUploadLimitField;
    
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];

    int limit = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%i", limit]] || limit < 0)
    {
        NSBeep();
        
        torrent = [enumerator nextObject]; //use first torrent
        
        limit = upload ? [torrent uploadLimit] : [torrent downloadLimit];
        while ((torrent = [enumerator nextObject]))
            if (limit != upload ? [torrent uploadLimit] : [torrent downloadLimit])
            {
                [sender setStringValue: @""];
                return;
            }
        
        [sender setIntValue: limit];
    }
    else
    {
        while ((torrent = [enumerator nextObject]))
            upload ? [torrent setUploadLimit: limit] : [torrent setDownloadLimit: limit];
    }
}

- (void) setRatioCustom: (id) sender
{
    BOOL custom = [sender state] != NSOffState;
    if (custom)
        [sender setState: NSOnState];

    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [torrent setCustomRatioSetting: custom];
    
    [fRatioStopCheck setEnabled: custom];
    [fRatioLimitField setEnabled: custom && [fRatioStopCheck state] == NSOnState];
}

- (void) setRatioSetting: (id) sender
{
    BOOL enabled = [sender state] != NSOffState;
    if (enabled)
        [sender setState: NSOnState];

    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [torrent setShouldStopAtRatio: enabled];
    
    [fRatioLimitField setEnabled: enabled && [fRatioCustomCheck state] == NSOnState];
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
