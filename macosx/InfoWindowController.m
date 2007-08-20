/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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

#define FILE_ROW_SMALL_HEIGHT 18.0

#define TAB_INFO_IDENT @"Info"
#define TAB_ACTIVITY_IDENT @"Activity"
#define TAB_PEERS_IDENT @"Peers"
#define TAB_FILES_IDENT @"Files"
#define TAB_OPTIONS_IDENT @"Options"

//15 spacing at the bottom of each tab
#define TAB_INFO_HEIGHT 268.0
#define TAB_ACTIVITY_HEIGHT 274.0
#define TAB_PEERS_HEIGHT 279.0
#define TAB_FILES_HEIGHT 279.0
#define TAB_OPTIONS_HEIGHT 158.0

#define PIECES_CONTROL_PROGRESS 0
#define PIECES_CONTROL_AVAILABLE 1

#define OPTION_POPUP_GLOBAL 0
#define OPTION_POPUP_NO_LIMIT 1
#define OPTION_POPUP_LIMIT 2

#define INVALID -99

@interface InfoWindowController (Private)

- (void) updateInfoGeneral;
- (void) updateInfoActivity;
- (void) updateInfoPeers;
- (void) updateInfoFiles;
- (void) updateInfoSettings;

- (void) setWindowForTab: (NSString *) identifier animate: (BOOL) animate;
- (NSArray *) peerSortDescriptors;

@end

@implementation InfoWindowController

- (id) initWithWindowNibName: (NSString *) name
{
    if ((self = [super initWithWindowNibName: name]))
    {
        fAppIcon = [NSImage imageNamed: @"NSApplicationIcon"];
        fDotGreen = [NSImage imageNamed: @"GreenDot.tiff"];
        fDotRed = [NSImage imageNamed: @"RedDot.tiff"];
        
        fCanResizeVertical = YES;
    }
    return self;
}

- (void) awakeFromNib
{
    //window location and size
    NSPanel * window = (NSPanel *)[self window];
    
    [window setFrameAutosaveName: @"InspectorWindowFrame"];
    [window setFrameUsingName: @"InspectorWindowFrame"];
    
    //select tab
    NSString * identifier = [[NSUserDefaults standardUserDefaults] stringForKey: @"InspectorSelected"];
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
    
    //allow for update notifications
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
    [nc addObserver: self selector: @selector(updateInfoStats)
            name: @"UpdateStats" object: nil];
    
    [nc addObserver: self selector: @selector(updateInfoSettings)
            name: @"UpdateSettings" object: nil];
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fTorrents release];
    [fPeers release];
    [fFiles release];
    
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
        [fCommentView setSelectable: NO];
        
        [fTorrentLocationField setStringValue: @""];
        [fTorrentLocationField setToolTip: nil];
        [fDataLocationField setStringValue: @""];
        [fDataLocationField setToolTip: nil];
        
        [fRevealDataButton setHidden: YES];
        [fRevealTorrentButton setHidden: YES];
        
        //don't allow empty fields to be selected
        [fTrackerField setSelectable: NO];
        [fHashField setSelectable: NO];
        [fCreatorField setSelectable: NO];
        [fTorrentLocationField setSelectable: NO];
        [fDataLocationField setSelectable: NO];
        
        [fStateField setStringValue: @""];
        [fProgressField setStringValue: @""];
        [fRatioField setStringValue: @""];
        
        [fSeedersField setStringValue: @""];
        [fLeechersField setStringValue: @""];
        [fCompletedFromTrackerField setStringValue: @""];
        [fConnectedPeersField setStringValue: NSLocalizedString(@"info not available", "Inspector -> Peers tab -> peers")];
        [fDownloadingFromField setStringValue: @""];
        [fUploadingToField setStringValue: @""];
        [fSwarmSpeedField setStringValue: @""];
        [fErrorMessageView setString: @""];
        [fErrorMessageView setSelectable: NO];
        
        [fDateAddedField setStringValue: @""];
        [fDateCompletedField setStringValue: @""];
        [fDateActivityField setStringValue: @""];
        
        [fPiecesControl setSelected: NO forSegment: PIECES_CONTROL_AVAILABLE];
        [fPiecesControl setSelected: NO forSegment: PIECES_CONTROL_PROGRESS];
        [fPiecesControl setEnabled: NO];
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
        
        NSImage * icon = [[torrent icon] copy];
        [icon setFlipped: NO];
        [fImageView setImage: icon];
        [icon release];
        
        NSString * name = [torrent name];
        [fNameField setStringValue: name];
        [fNameField setToolTip: name];
        [fSizeField setStringValue: [NSString stringForFileSize: [torrent size]]];
        
        NSString * hashString = [torrent hashString];
        [fPiecesField setStringValue: [NSString stringWithFormat: @"%d, %@", [torrent pieceCount],
                                        [NSString stringForFileSize: [torrent pieceSize]]]];
        [fHashField setStringValue: hashString];
        [fHashField setToolTip: hashString];
        [fSecureField setStringValue: [torrent privateTorrent]
                        ? NSLocalizedString(@"Private Torrent, PEX disabled", "Inspector -> is private torrent")
                        : NSLocalizedString(@"Public Torrent", "Inspector -> is not private torrent")];
        
        NSString * commentString = [torrent comment];
        [fCommentView setString: commentString];
        
        NSString * creatorString = [torrent creator];
        [fCreatorField setStringValue: creatorString];
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
        
        [fDateAddedField setObjectValue: [torrent dateAdded]];
        
        [fRevealDataButton setHidden: NO];
        [fRevealTorrentButton setHidden: ![torrent publicTorrent]];
        
        //allow these fields to be selected
        [fTrackerField setSelectable: YES];
        [fHashField setSelectable: YES];
        [fCommentView setSelectable: ![commentString isEqualToString: @""]];
        [fCreatorField setSelectable: ![creatorString isEqualToString: @""]];
        [fTorrentLocationField setSelectable: YES];
        [fDataLocationField setSelectable: YES];
        
        //set pieces view
        BOOL piecesAvailableSegment = [[NSUserDefaults standardUserDefaults] boolForKey: @"PiecesViewShowAvailability"];
        [fPiecesControl setSelected: piecesAvailableSegment forSegment: PIECES_CONTROL_AVAILABLE];
        [fPiecesControl setSelected: !piecesAvailableSegment forSegment: PIECES_CONTROL_PROGRESS];
        [fPiecesControl setEnabled: YES];
        [fPiecesView setTorrent: torrent];
        
        //set file table
        [fFileOutline deselectAll: nil];
        [fFiles release];
        fFiles = [[torrent fileList] retain];
        
        [self updateInfoFiles];
        
        int fileCount = [torrent fileCount];
        if (fileCount != 1)
            [fFileTableStatusField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d files total",
                                "Inspector -> Files tab -> bottom text (number of files)"), fileCount]];
        else
            [fFileTableStatusField setStringValue: NSLocalizedString(@"1 file total",
                                "Inspector -> Files tab -> bottom text (number of files)")];
    }
    
    //update stats and settings
    [self updateInfoSettings];
    
    [fPeerTable reloadData];
    [fFileOutline deselectAll: nil];
    [fFileOutline reloadData];
}

- (Torrent *) selectedTorrent
{
    return fTorrents && [fTorrents count] > 0 ? [fTorrents objectAtIndex: 0] : nil;
}

- (void) updateInfoStats
{
    NSString * ident = [[fTabView selectedTabViewItem] identifier];
    if ([ident isEqualToString: TAB_ACTIVITY_IDENT])
        [self updateInfoActivity];
    else if ([ident isEqualToString: TAB_PEERS_IDENT])
        [self updateInfoPeers];
    else if ([ident isEqualToString: TAB_INFO_IDENT])
        [self updateInfoGeneral];
    else if ([ident isEqualToString: TAB_FILES_IDENT])
        [self updateInfoFiles];
    else;
}

- (void) updateInfoGeneral
{   
    if ([fTorrents count] != 1)
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
    
    uint64_t  downloadedValid = 0, downloadedTotal = 0, uploadedTotal = 0;
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
        
        [fStateField setStringValue: [torrent stateString]];
        [fProgressField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%.2f%% (%.2f%% selected)",
                    "Inspector -> Activity tab -> progress"), 100.0 * [torrent progress], 100.0 * [torrent progressDone]]];
        [fRatioField setStringValue: [NSString stringForRatio: [torrent ratio]]];
        [fSwarmSpeedField setStringValue: [torrent isActive] ? [NSString stringForSpeed: [torrent swarmSpeed]] : @""];
        
        NSString * errorMessage = [torrent errorMessage];
        if (![errorMessage isEqualToString: [fErrorMessageView string]])
        {
            [fErrorMessageView setString: errorMessage];
            [fErrorMessageView setSelectable: ![errorMessage isEqualToString: @""]];
        }
        
        [fDateCompletedField setObjectValue: [torrent dateCompleted]];
        [fDateActivityField setObjectValue: [torrent dateActivity]];
        
        [fPiecesView updateView: NO];
    }
}

- (void) updateInfoPeers
{
    if ([fTorrents count] != 1)
        return;
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    
    int seeders = [torrent seeders], leechers = [torrent leechers], downloaded = [torrent completedFromTracker];
    [fSeedersField setStringValue: seeders < 0 ? @"" : [NSString stringWithFormat: @"%d", seeders]];
    [fLeechersField setStringValue: leechers < 0 ? @"" : [NSString stringWithFormat: @"%d", leechers]];
    [fCompletedFromTrackerField setStringValue: downloaded < 0 ? @"" : [NSString stringWithFormat: @"%d", downloaded]];
    
    BOOL active = [torrent isActive];
    
    if (active)
    {
        int total = [torrent totalPeersConnected];
        NSString * connected = [NSString stringWithFormat:
                                NSLocalizedString(@"%d Connected", "Inspector -> Peers tab -> peers"), total];
        
        if (total > 0)
        {
            NSMutableArray * components = [NSMutableArray arrayWithCapacity: 4];
            int count;
            if ((count = [torrent totalPeersTracker]) > 0)
                [components addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d tracker", "Inspector -> Peers tab -> peers"), count]];
            if ((count = [torrent totalPeersIncoming]) > 0)
                [components addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d incoming", "Inspector -> Peers tab -> peers"), count]];
            if ((count = [torrent totalPeersPex]) > 0)
                [components addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d PEX", "Inspector -> Peers tab -> peers"), count]];
            if ((count = [torrent totalPeersCache]) > 0)
                [components addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d cache", "Inspector -> Peers tab -> peers"), count]];
            
            connected = [NSString stringWithFormat: @"%@: %@", connected, [components componentsJoinedByString: @", "]];
        }
        
        [fConnectedPeersField setStringValue: connected];
    }
    else
        [fConnectedPeersField setStringValue: NSLocalizedString(@"info not available", "Inspector -> Peers tab -> peers")];
    
    [fDownloadingFromField setStringValue: active ? [NSString stringWithFormat: @"%d", [torrent peersSendingToUs]] : @""];
    [fUploadingToField setStringValue: active ? [NSString stringWithFormat: @"%d", [torrent peersGettingFromUs]] : @""];
    
    [fPeers release];
    fPeers = [[[torrent peers] sortedArrayUsingDescriptors: [self peerSortDescriptors]] retain];
    
    [fPeerTable reloadData];
}

- (void) updateInfoFiles
{
    if ([fTorrents count] == 1)
    {
        [[fTorrents objectAtIndex: 0] updateFileStat];
        [fFileOutline reloadData];
    }
}

- (void) updateInfoSettings
{
    if ([fTorrents count] > 0)
    {
        Torrent * torrent;
        
        //get bandwidth info
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        torrent = [enumerator nextObject]; //first torrent
        
        int uploadSpeedMode = [torrent speedMode: YES],
            uploadSpeedLimit = [torrent speedLimit: YES],
            downloadSpeedMode = [torrent speedMode: NO],
            downloadSpeedLimit = [torrent speedLimit: NO];
        
        while ((uploadSpeedMode != INVALID || uploadSpeedLimit != INVALID
                || downloadSpeedMode != INVALID || downloadSpeedLimit != INVALID)
                && (torrent = [enumerator nextObject]))
        {
            if (uploadSpeedMode != INVALID && uploadSpeedMode != [torrent speedMode: YES])
                uploadSpeedMode = INVALID;
            
            if (uploadSpeedLimit != INVALID && uploadSpeedLimit != [torrent speedLimit: YES])
                uploadSpeedLimit = INVALID;
            
            if (downloadSpeedMode != INVALID && downloadSpeedMode != [torrent speedMode: NO])
                downloadSpeedMode = INVALID;
            
            if (downloadSpeedLimit != INVALID && downloadSpeedLimit != [torrent speedLimit: NO])
                downloadSpeedLimit = INVALID;
        }
        
        //set upload view
        int index;
        if (uploadSpeedMode == TR_SPEEDLIMIT_SINGLE)
            index = OPTION_POPUP_LIMIT;
        else if (uploadSpeedMode == TR_SPEEDLIMIT_UNLIMITED)
            index = OPTION_POPUP_NO_LIMIT;
        else if (uploadSpeedMode == TR_SPEEDLIMIT_GLOBAL)
            index = OPTION_POPUP_GLOBAL;
        else
            index = -1;
        [fUploadLimitPopUp selectItemAtIndex: index];
        [fUploadLimitPopUp setEnabled: YES];
        
        [fUploadLimitLabel setHidden: uploadSpeedMode != TR_SPEEDLIMIT_SINGLE];
        [fUploadLimitField setHidden: uploadSpeedMode != TR_SPEEDLIMIT_SINGLE];
        if (uploadSpeedLimit != INVALID)
            [fUploadLimitField setIntValue: uploadSpeedLimit];
        else
            [fUploadLimitField setStringValue: @""];
        
        //set download view
        if (downloadSpeedMode == TR_SPEEDLIMIT_SINGLE)
            index = OPTION_POPUP_LIMIT;
        else if (downloadSpeedMode == TR_SPEEDLIMIT_UNLIMITED)
            index = OPTION_POPUP_NO_LIMIT;
        else if (downloadSpeedMode == TR_SPEEDLIMIT_GLOBAL)
            index = OPTION_POPUP_GLOBAL;
        else
            index = -1;
        [fDownloadLimitPopUp selectItemAtIndex: index];
        [fDownloadLimitPopUp setEnabled: YES];
        
        [fDownloadLimitLabel setHidden: downloadSpeedMode != TR_SPEEDLIMIT_SINGLE];
        [fDownloadLimitField setHidden: downloadSpeedMode != TR_SPEEDLIMIT_SINGLE];
        if (downloadSpeedLimit != INVALID)
            [fDownloadLimitField setIntValue: downloadSpeedLimit];
        else
            [fDownloadLimitField setStringValue: @""];
        
        //get ratio info
        enumerator = [fTorrents objectEnumerator];
        torrent = [enumerator nextObject]; //first torrent
        
        int checkRatio = [torrent ratioSetting];
        float ratioLimit = [torrent ratioLimit];
        
        while ((checkRatio != INVALID || checkRatio != INVALID)
                && (torrent = [enumerator nextObject]))
        {
            if (checkRatio != INVALID && checkRatio != [torrent ratioSetting])
                checkRatio = INVALID;
            
            if (ratioLimit != INVALID && ratioLimit != [torrent ratioLimit])
                ratioLimit = INVALID;
        }
        
        //set ratio view
        if (checkRatio == NSOnState)
            index = OPTION_POPUP_LIMIT;
        else if (checkRatio == NSOffState)
            index = OPTION_POPUP_NO_LIMIT;
        else if (checkRatio == NSMixedState)
            index = OPTION_POPUP_GLOBAL;
        else
            index = -1;
        [fRatioPopUp selectItemAtIndex: index];
        [fRatioPopUp setEnabled: YES];
        
        [fRatioLimitField setHidden: checkRatio != NSOnState];
        if (ratioLimit != INVALID)
            [fRatioLimitField setFloatValue: ratioLimit];
        else
            [fRatioLimitField setStringValue: @""];
		
		//set pex check
		enumerator = [fTorrents objectEnumerator];
        torrent = [enumerator nextObject]; //first torrent
		
		BOOL pexEnabled = ![torrent privateTorrent];
		int pexState = [torrent pex] ? NSOnState : NSOffState;
		
		while ((pexEnabled || pexState != NSMixedState)
                && (torrent = [enumerator nextObject]))
        {
            if (pexEnabled)
                pexEnabled = ![torrent privateTorrent];
            
            if (pexState != NSMixedState && pexState != ([torrent pex] ? NSOnState : NSOffState))
                pexState = NSMixedState;
        }
		
		[fPexCheck setEnabled: pexEnabled];
		[fPexCheck setState: pexState];
    }
    else
    {
        [fUploadLimitPopUp setEnabled: NO];
        [fUploadLimitPopUp selectItemAtIndex: -1];
        [fUploadLimitField setHidden: YES];
        [fUploadLimitLabel setHidden: YES];
        [fUploadLimitField setStringValue: @""];
        
        [fDownloadLimitPopUp setEnabled: NO];
        [fDownloadLimitPopUp selectItemAtIndex: -1];
        [fDownloadLimitField setHidden: YES];
        [fDownloadLimitLabel setHidden: YES];
        [fDownloadLimitField setStringValue: @""];
        
        [fRatioPopUp setEnabled: NO];
        [fRatioPopUp selectItemAtIndex: -1];
        [fRatioLimitField setHidden: YES];
        [fRatioLimitField setStringValue: @""];
		
		[fPexCheck setEnabled: NO];
        [fPexCheck setState: NSOffState];
    }
    
    [self updateInfoStats];
}

- (void) updateRatioForTorrent: (Torrent *) torrent
{
    if ([fTorrents containsObject: torrent])
        [self updateInfoSettings];
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];
    
    if (action == @selector(revealFile:))
        return [fFileOutline numberOfSelectedRows] > 0 &&
            [[[fTabView selectedTabViewItem] identifier] isEqualToString: TAB_FILES_IDENT];
    
    if (action == @selector(setCheck:))
    {
        if ([fFileOutline numberOfSelectedRows] <= 0)
            return NO;
        
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        NSIndexSet * indexSet = [fFileOutline selectedRowIndexes];
        NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
        int i, state = (menuItem == fFileCheckItem) ? NSOnState : NSOffState;
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            [itemIndexes addIndexes: [[fFileOutline itemAtRow: i] objectForKey: @"Indexes"]];
        
        return [torrent checkForFiles: itemIndexes] != state && [torrent canChangeDownloadCheckForFiles: itemIndexes];
    }
    
    if (action == @selector(setOnlySelectedCheck:))
    {
        if ([fFileOutline numberOfSelectedRows] <= 0)
            return NO;
        
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        NSIndexSet * indexSet = [fFileOutline selectedRowIndexes];
        NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
        int i;
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            [itemIndexes addIndexes: [[fFileOutline itemAtRow: i] objectForKey: @"Indexes"]];
            
        return [torrent canChangeDownloadCheckForFiles: itemIndexes];
    }
    
    if (action == @selector(setPriority:))
    {
        if ([fFileOutline numberOfSelectedRows] <= 0)
        {
            [menuItem setState: NSOffState];
            return NO;
        }
        
        //determine which priorities are checked
        NSIndexSet * indexSet = [fFileOutline selectedRowIndexes];
        BOOL current = NO, other = NO;
        int i, priority;
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
        if (menuItem == fFilePriorityHigh)
            priority = TR_PRI_HIGH;
        else if (menuItem == fFilePriorityLow)
            priority = TR_PRI_LOW;
        else
            priority = TR_PRI_NORMAL;
        
        NSIndexSet * fileIndexSet;
        for (i = [indexSet firstIndex]; i != NSNotFound && (!current || !other); i = [indexSet indexGreaterThanIndex: i])
        {
            fileIndexSet = [[fFileOutline itemAtRow: i] objectForKey: @"Indexes"];
            if (![torrent canChangeDownloadCheckForFiles: fileIndexSet])
                continue;
            else if ([torrent hasFilePriority: priority forIndexes: fileIndexSet])
                current = YES;
            else
                other = YES;
        }
        
        [menuItem setState: current ? NSOnState : NSOffState];
        return current || other;
    }
    
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
    [[NSUserDefaults standardUserDefaults] setObject: identifier forKey: @"InspectorSelected"];
}

- (void) setWindowForTab: (NSString *) identifier animate: (BOOL) animate
{
    [self updateInfoStats];
    
    BOOL canResizeVertical = NO;
    float height;
    if ([identifier isEqualToString: TAB_ACTIVITY_IDENT])
    {
        height = TAB_ACTIVITY_HEIGHT;
        [fPiecesView updateView: YES];
    }
    else if ([identifier isEqualToString: TAB_PEERS_IDENT])
    {
        height = TAB_PEERS_HEIGHT;
        canResizeVertical = YES;
    }
    else if ([identifier isEqualToString: TAB_FILES_IDENT])
    {
        height = TAB_FILES_HEIGHT;
        canResizeVertical = YES;
    }
    else if ([identifier isEqualToString: TAB_OPTIONS_IDENT])
        height = TAB_OPTIONS_HEIGHT;
    else
        height = TAB_INFO_HEIGHT;
    
    NSWindow * window = [self window];
    NSRect frame = [window frame];
    NSView * view = [[fTabView selectedTabViewItem] view];
    
    float difference = (height - [view frame].size.height) * [window userSpaceScaleFactor];;
    frame.origin.y -= difference;
    frame.size.height += difference;
    
    if (!fCanResizeVertical || !canResizeVertical)
    {
        if (animate)
        {
            [view setHidden: YES];
            [window setFrame: frame display: YES animate: YES];
            [view setHidden: NO];
        }
        else
            [window setFrame: frame display: YES];
    }
    
    [window setMinSize: NSMakeSize([window minSize].width, frame.size.height)];
    [window setMaxSize: NSMakeSize([window maxSize].width, canResizeVertical ? FLT_MAX : frame.size.height)];
    
    fCanResizeVertical = canResizeVertical;
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
            return [peer objectForKey: @"Progress"]; //returning nil is fine
        else if ([ident isEqualToString: @"UL To"])
        {
            NSNumber * rate;
            return (rate = [peer objectForKey: @"UL To Rate"]) ? [NSString stringForSpeedAbbrev: [rate floatValue]] : @"";
        }
        else if ([ident isEqualToString: @"DL From"])
        {
            NSNumber * rate;
            return (rate = [peer objectForKey: @"DL From Rate"]) ? [NSString stringForSpeedAbbrev: [rate floatValue]] : @"";
        }
        else
            return [peer objectForKey: @"IP"];
    }
    return nil;
}

- (void) tableView: (NSTableView *) tableView willDisplayCell: (id) cell
            forTableColumn: (NSTableColumn *) tableColumn row: (int) row
{
    if (tableView == fPeerTable)
    {
        if ([[tableColumn identifier] isEqualToString: @"Progress"])
            [cell setHidden: ![[[fPeers objectAtIndex: row] objectForKey: @"Connected"] boolValue]];
    }
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
        NSDictionary * peer = [fPeers objectAtIndex: row];
        
        NSMutableArray * components = [NSMutableArray arrayWithCapacity: 3];
        
        if ([[peer objectForKey: @"Connected"] boolValue])
            [components addObject: [NSString stringWithFormat:
                                    NSLocalizedString(@"Progress: %.1f%%", "Inspector -> Peers tab -> table row tooltip"),
                                    [[peer objectForKey: @"Progress"] floatValue] * 100.0]];
        
        int port;
        if ((port = [[peer objectForKey: @"Port"] intValue]) > 0)
            [components addObject: [NSString stringWithFormat:
                                    NSLocalizedString(@"Port: %d", "Inspector -> Peers tab -> table row tooltip"), port]];
        else
            [components addObject: NSLocalizedString(@"Port: N/A", "Inspector -> Peers tab -> table row tooltip")];
        
        int from = [[peer objectForKey: @"From"] intValue];
        if (from == TR_PEER_FROM_INCOMING)
            [components addObject: NSLocalizedString(@"From: incoming connection", "Inspector -> Peers tab -> table row tooltip")];
        else if (from == TR_PEER_FROM_CACHE)
            [components addObject: NSLocalizedString(@"From: cache", "Inspector -> Peers tab -> table row tooltip")];
        else if (from == TR_PEER_FROM_PEX)
            [components addObject: NSLocalizedString(@"From: peer exchange", "Inspector -> Peers tab -> table row tooltip")];
        else
            [components addObject: NSLocalizedString(@"From: tracker", "Inspector -> Peers tab -> table row tooltip")];
        
        return [components componentsJoinedByString: @"\n"];
    }
    return nil;
}

- (int) outlineView: (NSOutlineView *) outlineView numberOfChildrenOfItem: (id) item
{
    if (!item)
        return [fFiles count];
    return [[item objectForKey: @"IsFolder"] boolValue] ? [[item objectForKey: @"Children"] count] : 0;
}

- (BOOL) outlineView: (NSOutlineView *) outlineView isItemExpandable: (id) item 
{
    return [[item objectForKey: @"IsFolder"] boolValue];
}

- (id) outlineView: (NSOutlineView *) outlineView child: (int) index ofItem: (id) item
{
    return [(item ? [item objectForKey: @"Children"] : fFiles) objectAtIndex: index];
}

- (id) outlineView: (NSOutlineView *) outlineView objectValueForTableColumn: (NSTableColumn *) tableColumn byItem: (id) item
{
    if ([[tableColumn identifier] isEqualToString: @"Check"])
        return [NSNumber numberWithInt: [[fTorrents objectAtIndex: 0] checkForFiles: [item objectForKey: @"Indexes"]]];
    else
        return item;
}

- (void) outlineView: (NSOutlineView *) outlineView willDisplayCell: (id) cell
            forTableColumn: (NSTableColumn *) tableColumn item: (id) item
{
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Check"])
        [cell setEnabled: [[fTorrents objectAtIndex: 0] canChangeDownloadCheckForFiles: [item objectForKey: @"Indexes"]]];
    else if ([identifier isEqualToString: @"Priority"])
        [cell setRepresentedObject: item];
    else;
}

- (void) outlineView: (NSOutlineView *) outlineView setObjectValue: (id) object
        forTableColumn: (NSTableColumn *) tableColumn byItem: (id) item
{
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Check"])
    {
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        NSIndexSet * indexSet;
        if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask)
            indexSet = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [torrent fileCount])];
        else
            indexSet = [item objectForKey: @"Indexes"];
        
        [torrent setFileCheckState: [object intValue] != NSOffState ? NSOnState : NSOffState forIndexes: indexSet];
        [fFileOutline reloadData];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
    }
}

- (NSString *) outlineView: (NSOutlineView *) outlineView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
        tableColumn: (NSTableColumn *) tableColumn item: (id) item mouseLocation: (NSPoint) mouseLocation
{
    NSString * ident = [tableColumn identifier];
    if ([ident isEqualToString: @"Name"])
        return [[[fTorrents objectAtIndex: 0] downloadFolder] stringByAppendingPathComponent: [item objectForKey: @"Path"]];
    else if ([ident isEqualToString: @"Check"])
    {
        int check = [cell state];
        if (check == NSOffState)
            return NSLocalizedString(@"Don't Download", "Inspector -> files tab -> tooltip");
        else if (check == NSMixedState)
            return NSLocalizedString(@"Download Some", "Inspector -> files tab -> tooltip");
        else
            return NSLocalizedString(@"Download", "Inspector -> files tab -> tooltip");
    }
    else if ([ident isEqualToString: @"Priority"])
    {
        NSSet * priorities = [[fTorrents objectAtIndex: 0] filePrioritiesForIndexes: [item objectForKey: @"Indexes"]];
        
        int count = [priorities count];
        if (count == 0)
            return NSLocalizedString(@"Priority Not Available", "Inspector -> files tab -> tooltip");
        else if (count > 1)
            return NSLocalizedString(@"Multiple Priorities", "Inspector -> files tab -> tooltip");
        else
        {
            int priority = [[priorities anyObject] intValue];
            if (priority == TR_PRI_LOW)
                return NSLocalizedString(@"Low Priority", "Inspector -> files tab -> tooltip");
            else if (priority == TR_PRI_HIGH)
                return NSLocalizedString(@"High Priority", "Inspector -> files tab -> tooltip");
            else
                return NSLocalizedString(@"Normal Priority", "Inspector -> files tab -> tooltip");
        }
    }
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

- (void) setFileOutlineHoverRowForEvent: (NSEvent *) event
{
    [fFileOutline setHoverRowForEvent: [[[fTabView selectedTabViewItem] identifier] isEqualToString: TAB_FILES_IDENT]
                                        ? event : nil];
}

- (NSArray *) peerSortDescriptors
{
    NSMutableArray * descriptors = [NSMutableArray array];
    
    NSArray * oldDescriptors = [fPeerTable sortDescriptors];
    BOOL useSecond = YES, asc = YES;
    if ([oldDescriptors count] > 0)
    {
        NSSortDescriptor * descriptor = [oldDescriptors objectAtIndex: 0];
        [descriptors addObject: descriptor];
        
        if ((useSecond = ![[descriptor key] isEqualToString: @"IP"]))
            asc = [descriptor ascending];
    }
    
    //sort by IP after primary sort
    if (useSecond)
    {
        NSSortDescriptor * secondDescriptor = [[NSSortDescriptor alloc] initWithKey: @"IP" ascending: asc
                                                                        selector: @selector(compareIP:)];
        [descriptors addObject: secondDescriptor];
        [secondDescriptor release];
    }
    
    return descriptors;
}

- (void) setPiecesView: (id) sender
{
    [self setPiecesViewForAvailable: [sender selectedSegment] == PIECES_CONTROL_AVAILABLE];
}

- (void) setPiecesViewForAvailable: (BOOL) available
{
    [fPiecesControl setSelected: available forSegment: PIECES_CONTROL_AVAILABLE];
    [fPiecesControl setSelected: !available forSegment: PIECES_CONTROL_PROGRESS];
    
    [[NSUserDefaults standardUserDefaults] setBool: available forKey: @"PiecesViewShowAvailability"];
    [fPiecesView updateView: YES];
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

- (void) setCheck: (id) sender
{
    int state = sender == fFileCheckItem ? NSOnState : NSOffState;
    
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSIndexSet * indexSet = [fFileOutline selectedRowIndexes];
    NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
    int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [itemIndexes addIndexes: [[fFileOutline itemAtRow: i] objectForKey: @"Indexes"]];
    
    [torrent setFileCheckState: state forIndexes: itemIndexes];
    [fFileOutline reloadData];
}

- (void) setOnlySelectedCheck: (id) sender
{
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSIndexSet * indexSet = [fFileOutline selectedRowIndexes];
    NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
    int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [itemIndexes addIndexes: [[fFileOutline itemAtRow: i] objectForKey: @"Indexes"]];
    
    [torrent setFileCheckState: NSOnState forIndexes: itemIndexes];
    
    NSMutableIndexSet * remainingItemIndexes = [NSMutableIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [torrent fileCount])];
    [remainingItemIndexes removeIndexes: itemIndexes];
    [torrent setFileCheckState: NSOffState forIndexes: remainingItemIndexes];
    
    [fFileOutline reloadData];
}

- (void) setPriority: (id) sender
{
    int priority;
    if (sender == fFilePriorityHigh)
        priority = TR_PRI_HIGH;
    else if (sender == fFilePriorityLow)
        priority = TR_PRI_LOW;
    else
        priority = TR_PRI_NORMAL;
    
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSIndexSet * indexSet = [fFileOutline selectedRowIndexes];
    NSMutableIndexSet * itemIndexes = [NSMutableIndexSet indexSet];
    int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [itemIndexes addIndexes: [[fFileOutline itemAtRow: i] objectForKey: @"Indexes"]];
    
    [torrent setFilePriority: priority forIndexes: itemIndexes];
    [fFileOutline reloadData];
}

- (void) setSpeedMode: (id) sender
{
    BOOL upload = sender == fUploadLimitPopUp;
    int index = [sender indexOfSelectedItem], mode;
    if (index == OPTION_POPUP_LIMIT)
        mode = TR_SPEEDLIMIT_SINGLE;
    else if (index == OPTION_POPUP_NO_LIMIT)
        mode = TR_SPEEDLIMIT_UNLIMITED;
    else
        mode = TR_SPEEDLIMIT_GLOBAL;
    
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [torrent setSpeedMode: mode upload: upload];
    
    NSTextField * field = upload ? fUploadLimitField : fDownloadLimitField;
    
    BOOL single = mode == TR_SPEEDLIMIT_SINGLE;
    [field setHidden: !single];
    if (single)
    {
        [field selectText: self];
        [[self window] makeKeyAndOrderFront:self];
    }
    
    NSTextField * label = upload ? fUploadLimitLabel : fDownloadLimitLabel;
    [label setHidden: !single];
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
        limit = [torrent speedLimit: upload];
        while ((torrent = [enumerator nextObject]))
            if (limit != [torrent speedLimit: upload])
            {
                [sender setStringValue: @""];
                return;
            }
        
        [sender setIntValue: limit];
    }
    else
    {
        while ((torrent = [enumerator nextObject]))
            [torrent setSpeedLimit: limit upload: upload];
    }
}

- (void) setRatioSetting: (id) sender
{
    int index = [sender indexOfSelectedItem], setting;
    if (index == OPTION_POPUP_LIMIT)
        setting = NSOnState;
    else if (index == OPTION_POPUP_NO_LIMIT)
        setting = NSOffState;
    else
        setting = NSMixedState;
    
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [torrent setRatioSetting: setting];
    
    BOOL single = setting == NSOnState;
    [fRatioLimitField setHidden: !single];
    if (single)
    {
        [fRatioLimitField selectText: self];
        [[self window] makeKeyAndOrderFront:self];
    }
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
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
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
}

- (void) setPex: (id) sender
{
	int state = [sender state];
	if (state == NSMixedState)
	{
		state = NSOnState;
		[sender setState: state];
	}
	
	Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
	
	while ((torrent = [enumerator nextObject]))
		[torrent setPex: state == NSOnState];
}

@end
