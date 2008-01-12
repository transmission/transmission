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

#import "InfoWindowController.h"
#import "InfoTabButtonCell.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

#define TAB_INFO_IDENT @"Info"
#define TAB_ACTIVITY_IDENT @"Activity"
#define TAB_PEERS_IDENT @"Peers"
#define TAB_FILES_IDENT @"Files"
#define TAB_OPTIONS_IDENT @"Options"

#define TAB_MIN_HEIGHT 250

#define PIECES_CONTROL_PROGRESS 0
#define PIECES_CONTROL_AVAILABLE 1

#define OPTION_POPUP_GLOBAL 0
#define OPTION_POPUP_NO_LIMIT 1
#define OPTION_POPUP_LIMIT 2

#define INVALID -99

typedef enum
{
    TAB_INFO_TAG = 0,
    TAB_ACTIVITY_TAG = 1,
    TAB_PEERS_TAG = 2,
    TAB_FILES_TAG = 3,
    TAB_OPTIONS_TAG = 4
} tabTag;

@interface InfoWindowController (Private)

- (void) updateInfoGeneral;
- (void) updateInfoActivity;
- (void) updateInfoPeers;
- (void) updateInfoFiles;

- (NSView *) tabViewForTag: (int) tag;
- (NSArray *) peerSortDescriptors;

@end

@implementation InfoWindowController

- (id) init
{
    return [super initWithWindowNibName: @"InfoWindow"];
}

- (void) awakeFromNib
{
    //window location and size
    NSPanel * window = (NSPanel *)[self window];
    
    float windowHeight = [window frame].size.height;
    
    [window setFrameAutosaveName: @"InspectorWindow"];
    [window setFrameUsingName: @"InspectorWindow"];
    
    NSRect windowRect = [window frame];
    windowRect.origin.y -= windowHeight - windowRect.size.height;
    windowRect.size.height = windowHeight;
    [window setFrame: windowRect display: NO];
    
    [window setBecomesKeyOnlyIfNeeded: YES];
    
    //set tab images and tooltips
    [[fTabMatrix cellWithTag: TAB_INFO_TAG] setIcon: [NSImage imageNamed: @"InfoGeneral.png"]];
    [[fTabMatrix cellWithTag: TAB_ACTIVITY_TAG] setIcon: [NSImage imageNamed: @"InfoActivity.png"]];
    [[fTabMatrix cellWithTag: TAB_PEERS_TAG] setIcon: [NSImage imageNamed: @"InfoPeers.png"]];
    [[fTabMatrix cellWithTag: TAB_FILES_TAG] setIcon: [NSImage imageNamed: @"InfoFiles.png"]];
    [[fTabMatrix cellWithTag: TAB_OPTIONS_TAG] setIcon: [NSImage imageNamed: @"InfoOptions.png"]];
    
    //set selected tab
    fCurrentTabTag = INVALID;
    NSString * identifier = [[NSUserDefaults standardUserDefaults] stringForKey: @"InspectorSelected"];
    int tag;
    if ([identifier isEqualToString: TAB_INFO_IDENT])
        tag = TAB_INFO_TAG;
    else if ([identifier isEqualToString: TAB_ACTIVITY_IDENT])
        tag = TAB_ACTIVITY_TAG;
    else if ([identifier isEqualToString: TAB_PEERS_IDENT])
        tag = TAB_PEERS_TAG;
    else if ([identifier isEqualToString: TAB_FILES_IDENT])
        tag = TAB_FILES_TAG;
    else if ([identifier isEqualToString: TAB_OPTIONS_IDENT])
        tag = TAB_OPTIONS_TAG;
    else //safety
    {
        [[NSUserDefaults standardUserDefaults] setObject: TAB_INFO_IDENT forKey: @"InspectorSelected"];
        tag = TAB_INFO_TAG;
    }
    [fTabMatrix selectCellWithTag: tag];
    [self setTab: nil];
    
    //initially sort peer table by IP
    if ([[fPeerTable sortDescriptors] count] == 0)
        [fPeerTable setSortDescriptors: [NSArray arrayWithObject: [[fPeerTable tableColumnWithIdentifier: @"IP"]
                                            sortDescriptorPrototype]]];
    
    //set table header tool tips
    if ([NSApp isOnLeopardOrBetter])
    {
        [[fPeerTable tableColumnWithIdentifier: @"Encryption"] setHeaderToolTip: NSLocalizedString(@"Encrypted Connection",
                                                                            "inspector -> peer table -> header tool tip")];
        [[fPeerTable tableColumnWithIdentifier: @"Progress"] setHeaderToolTip: NSLocalizedString(@"Available",
                                                                            "inspector -> peer table -> header tool tip")];
        [[fPeerTable tableColumnWithIdentifier: @"UL To"] setHeaderToolTip: NSLocalizedString(@"Uploading To Peer",
                                                                            "inspector -> peer table -> header tool tip")];
        [[fPeerTable tableColumnWithIdentifier: @"DL From"] setHeaderToolTip: NSLocalizedString(@"Downloading From Peer",
                                                                            "inspector -> peer table -> header tool tip")];                                                             
    }
    
    //set blank inspector
    [self setInfoForTorrents: [NSArray array]];
    
    //allow for update notifications
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
    [nc addObserver: self selector: @selector(updateInfoStats) name: @"UpdateStats" object: nil];
    [nc addObserver: self selector: @selector(updateOptions) name: @"UpdateOptions" object: nil];
}

- (void) dealloc
{
    //save resizeable view height
    if (fCurrentTabTag == TAB_PEERS_TAG || fCurrentTabTag == TAB_FILES_TAG)
        [[NSUserDefaults standardUserDefaults] setFloat: [[self tabViewForTag: fCurrentTabTag] frame].size.height
            forKey: @"InspectorContentHeight"];
    
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fTorrents release];
    [fPeers release];
    
    [super dealloc];
}

- (void) setInfoForTorrents: (NSArray *) torrents
{
    [fTorrents release];
    fTorrents = [torrents retain];

    int numberSelected = [fTorrents count];
    if (numberSelected != 1)
    {
        if (numberSelected > 0)
        {
            [fImageView setImage: [NSImage imageNamed: [NSApp isOnLeopardOrBetter]
                                    ? NSImageNameMultipleDocuments : @"NSApplicationIcon"]];
            
            [fNameField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d Torrents Selected",
                                            "Inspector -> above tabs -> selected torrents"), numberSelected]];
        
            uint64_t size = 0;
            int fileCount = 0;
            NSEnumerator * enumerator = [torrents objectEnumerator];
            Torrent * torrent;
            while ((torrent = [enumerator nextObject]))
            {
                size += [torrent size];
                fileCount += [torrent fileCount];
            }
            
            [fBasicInfoField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d Files, %@ Total",
                                    "Inspector -> above tabs -> selected torrents"), fileCount, [NSString stringForFileSize: size]]];
            [fBasicInfoField setToolTip: [NSString stringWithFormat: NSLocalizedString(@"%u bytes",
                                            "Inspector -> above tabs -> selected torrents"), size]];
        }
        else
        {
            [fImageView setImage: [NSImage imageNamed: @"NSApplicationIcon"]];
            
            [fNameField setStringValue: NSLocalizedString(@"No Torrents Selected", "Inspector -> above tabs -> selected torrents")];
            [fBasicInfoField setStringValue: @""];
            [fBasicInfoField setToolTip: @""];
    
            [fHaveField setStringValue: @""];
            [fDownloadedTotalField setStringValue: @""];
            [fUploadedTotalField setStringValue: @""];
            [fFailedHashField setStringValue: @""];
            
            //options fields
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
            
            [fPeersConnectField setEnabled: NO];
            [fPeersConnectField setStringValue: @""];
        }
        
        [fFileController setTorrent: nil];
        
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
        
        [fSwarmSpeedField setStringValue: @""];
        [fErrorMessageView setString: @""];
        [fErrorMessageView setSelectable: NO];
        
        [fConnectedPeersField setStringValue: NSLocalizedString(@"info not available", "Inspector -> Peers tab -> peers")];
        [fDownloadingFromField setStringValue: @""];
        [fUploadingToField setStringValue: @""];
        [fKnownField setStringValue: @""];
        [fSeedersField setStringValue: @""];
        [fLeechersField setStringValue: @""];
        [fCompletedFromTrackerField setStringValue: @""];
        
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
    }
    else
    {    
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
        [fFileController setTorrent: torrent];
        
        NSImage * icon = [[torrent icon] copy];
        [icon setFlipped: NO];
        [fImageView setImage: icon];
        [icon release];
        
        NSString * name = [torrent name];
        [fNameField setStringValue: name];
        [fNameField setToolTip: name];
        
        NSString * basicString = [NSString stringForFileSize: [torrent size]];
        if ([torrent folder])
        {
            NSString * fileString;
            int fileCount = [torrent fileCount];
            if (fileCount == 1)
                fileString = NSLocalizedString(@"1 File, ", "Inspector -> above tabs -> selected torrents");
            else
                fileString= [NSString stringWithFormat: NSLocalizedString(@"%d Files, ",
                                "Inspector -> above tabs -> selected torrents"), fileCount];
            basicString = [fileString stringByAppendingString: basicString];
        }
        [fBasicInfoField setStringValue: basicString];
        [fBasicInfoField setToolTip: [NSString stringWithFormat: NSLocalizedString(@"%u bytes",
                                        "Inspector -> above tabs -> selected torrents"), [torrent size]]];
        
        NSArray * allTrackers = [torrent allTrackers], * subTrackers;
        NSMutableArray * trackerStrings = [NSMutableArray arrayWithCapacity: [allTrackers count]];
        NSEnumerator * enumerator = [allTrackers objectEnumerator];
        while ((subTrackers = [enumerator nextObject]))
            [trackerStrings addObject: [subTrackers componentsJoinedByString: @", "]];
        [fTrackerField setToolTip: [trackerStrings componentsJoinedByString: @"\n"]];
        
        NSString * hashString = [torrent hashString];
        [fPiecesField setStringValue: [NSString stringWithFormat: @"%d, %@", [torrent pieceCount],
                                        [NSString stringForFileSize: [torrent pieceSize]]]];
        [fHashField setStringValue: hashString];
        [fHashField setToolTip: hashString];
        [fSecureField setStringValue: [torrent privateTorrent]
                        ? NSLocalizedString(@"Private Torrent, PEX automatically disabled", "Inspector -> private torrent")
                        : NSLocalizedString(@"Public Torrent", "Inspector -> private torrent")];
        
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
    }
    
    //update stats and settings
    [self updateInfoStats];
    [self updateOptions];
    
    [fPeerTable reloadData];
}

- (void) updateInfoStats
{
    switch ([fTabMatrix selectedTag])
    {
        case TAB_INFO_TAG:
            [self updateInfoGeneral];
            break;
        case TAB_ACTIVITY_TAG:
            [self updateInfoActivity];
            break;
        case TAB_PEERS_TAG:
            [self updateInfoPeers];
            break;
        case TAB_FILES_TAG:
            [self updateInfoFiles];
            break;
    }
}

- (void) updateOptions
{
    if ([fTorrents count] == 0)
        return;
    
    //get bandwidth info
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent = [enumerator nextObject]; //first torrent
    
    int uploadSpeedMode = [torrent speedMode: YES],
        uploadSpeedLimit = [torrent speedLimit: YES],
        downloadSpeedMode = [torrent speedMode: NO],
        downloadSpeedLimit = [torrent speedLimit: NO];
    
    while ((torrent = [enumerator nextObject])
            && (uploadSpeedMode != INVALID || uploadSpeedLimit != INVALID
                || downloadSpeedMode != INVALID || downloadSpeedLimit != INVALID))
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
    
    while ((torrent = [enumerator nextObject]) && (checkRatio != INVALID || checkRatio != INVALID))
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
    
    //get peer info
    enumerator = [fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent
    
    int maxPeers = [torrent maxPeerConnect];
    
    while ((torrent = [enumerator nextObject]))
    {
        if (maxPeers != [torrent maxPeerConnect])
        {
            maxPeers = INVALID;
            break;
        }
    }
    
    //set peer view
    [fPeersConnectField setEnabled: YES];
    if (maxPeers != INVALID)
        [fPeersConnectField setIntValue: maxPeers];
    else
        [fPeersConnectField setStringValue: @""];
}

- (NSRect) windowWillUseStandardFrame: (NSWindow *) window defaultFrame: (NSRect) defaultFrame
{
    NSRect windowRect = [window frame];
    windowRect.size.width = [window minSize].width;
    return windowRect;
}

- (void) setTab: (id) sender
{
    [self updateInfoStats];
    
    BOOL oldCanResizeVertical = fCurrentTabTag == TAB_PEERS_TAG || fCurrentTabTag == TAB_FILES_TAG, canResizeVertical;
    int oldTabTag = fCurrentTabTag;
    fCurrentTabTag = [fTabMatrix selectedTag];
    
    NSView * view;
    NSString * identifier, * title;;
    switch (fCurrentTabTag)
    {
        case TAB_INFO_TAG:
            view = fInfoView;
            identifier = TAB_INFO_IDENT;
            title = NSLocalizedString(@"General Info", "Inspector -> title");
            canResizeVertical = NO;
            break;
        case TAB_ACTIVITY_TAG:
            view = fActivityView;
            identifier = TAB_ACTIVITY_IDENT;
            title = NSLocalizedString(@"Activity", "Inspector -> title");
            canResizeVertical = NO;
            
            [fPiecesView updateView: YES];
            break;
        case TAB_PEERS_TAG:
            view = fPeersView;
            identifier = TAB_PEERS_IDENT;
            title = NSLocalizedString(@"Peers", "Inspector -> title");
            canResizeVertical = YES;
            break;
        case TAB_FILES_TAG:
            view = fFilesView;
            identifier = TAB_FILES_IDENT;
            title = NSLocalizedString(@"Files", "Inspector -> title");
            canResizeVertical = YES;
            break;
        case TAB_OPTIONS_TAG:
            view = fOptionsView;
            identifier = TAB_OPTIONS_IDENT;
            title = NSLocalizedString(@"Options", "Inspector -> title");
            canResizeVertical = NO;
            break;
        default:
            return;
    }
        
    [[NSUserDefaults standardUserDefaults] setObject: identifier forKey: @"InspectorSelected"];
    
    NSWindow * window = [self window];
    
    float oldHeight = 0;
    if (oldTabTag != INVALID)
    {
        if (fCurrentTabTag == oldTabTag)
            return;
        
        //deselect old tab item
        [(InfoTabButtonCell *)[fTabMatrix cellWithTag: oldTabTag] setSelectedTab: NO];
        
        //get old view
        NSView * oldView = [self tabViewForTag: oldTabTag];
        [oldView setHidden: YES];
        [oldView removeFromSuperview];
        
        oldHeight = [oldView frame].size.height;
        
        //save old size
        if (oldCanResizeVertical)
            [[NSUserDefaults standardUserDefaults] setFloat: [oldView frame].size.height forKey: @"InspectorContentHeight"];
    }
    
    [window setTitle: [NSString stringWithFormat: @"%@ - %@", title, NSLocalizedString(@"Torrent Inspector", "Inspector -> title")]];
    
    //selected tab item
    [(InfoTabButtonCell *)[fTabMatrix selectedCell] setSelectedTab: YES];
    
    NSRect windowRect = [window frame], viewRect = [view frame];
    
    if (canResizeVertical)
    {
        float height = [[NSUserDefaults standardUserDefaults] floatForKey: @"InspectorContentHeight"];
        if (height != 0)
            viewRect.size.height = MAX(height, TAB_MIN_HEIGHT);
    }
    
    float difference = (viewRect.size.height - oldHeight) * [window userSpaceScaleFactor];
    windowRect.origin.y -= difference;
    windowRect.size.height += difference;
    
    if (canResizeVertical)
    {
        if (!oldCanResizeVertical)
        {
            [window setMinSize: NSMakeSize([window minSize].width, windowRect.size.height - viewRect.size.height + TAB_MIN_HEIGHT)];
            [window setMaxSize: NSMakeSize(FLT_MAX, FLT_MAX)];
        }
    }
    else
    {
        [window setMinSize: NSMakeSize([window minSize].width, windowRect.size.height)];
        [window setMaxSize: NSMakeSize(FLT_MAX, windowRect.size.height)];
    }
    
    viewRect.size.width = windowRect.size.width;
    [view setFrame: viewRect];
    
    [window setFrame: windowRect display: YES animate: oldTabTag != INVALID];
    [[window contentView] addSubview: view];
    [view setHidden: NO];
}

- (void) setNextTab
{
    int tag = [fTabMatrix selectedTag]+1;
    if (tag >= [fTabMatrix numberOfColumns])
        tag = 0;
    
    [fTabMatrix selectCellWithTag: tag];
    [self setTab: nil];
}

- (void) setPreviousTab
{
    int tag = [fTabMatrix selectedTag]-1;
    if (tag < 0)
        tag = [fTabMatrix numberOfColumns]-1;
    
    [fTabMatrix selectCellWithTag: tag];
    [self setTab: nil];
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
        
        if ([ident isEqualToString: @"Encryption"])
            return [[peer objectForKey: @"Encryption"] boolValue] ? [NSImage imageNamed: @"Lock.png"] : nil;
        else if ([ident isEqualToString: @"Client"])
            return [peer objectForKey: @"Client"];
        else if  ([ident isEqualToString: @"Progress"])
            return [peer objectForKey: @"Progress"];
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

- (BOOL) tableView: (NSTableView *) tableView shouldSelectRow: (int) row
{
    return tableView != fPeerTable;
}

- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) column row: (int) row mouseLocation: (NSPoint) mouseLocation
{
    if (tableView == fPeerTable)
    {
        NSDictionary * peer = [fPeers objectAtIndex: row];
        NSMutableArray * components = [NSMutableArray arrayWithCapacity: 5];
        
        [components addObject: [NSString stringWithFormat: NSLocalizedString(@"Progress: %.1f%%",
            "Inspector -> Peers tab -> table row tooltip"), [[peer objectForKey: @"Progress"] floatValue] * 100.0]];
        
        if ([[peer objectForKey: @"Encryption"] boolValue])
            [components addObject: NSLocalizedString(@"Encrypted Connection", "Inspector -> Peers tab -> table row tooltip")];
        
        int port;
        if ((port = [[peer objectForKey: @"Port"] intValue]) > 0)
            [components addObject: [NSString stringWithFormat:
                                    NSLocalizedString(@"Port: %d", "Inspector -> Peers tab -> table row tooltip"), port]];
        else
            [components addObject: NSLocalizedString(@"Port: N/A", "Inspector -> Peers tab -> table row tooltip")];
        
        switch ([[peer objectForKey: @"From"] intValue])
        {
            case TR_PEER_FROM_TRACKER:
                [components addObject: NSLocalizedString(@"From: tracker", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_INCOMING:
                [components addObject: NSLocalizedString(@"From: incoming connection", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_CACHE:
                [components addObject: NSLocalizedString(@"From: cache", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_PEX:
                [components addObject: NSLocalizedString(@"From: peer exchange", "Inspector -> Peers tab -> table row tooltip")];
                break;
        }
        
        #warning redo
        NSMutableArray * peerStatusArray = [NSMutableArray arrayWithCapacity: 2];
        if ([[peer objectForKey: @"PeerChoked"] boolValue])
            [peerStatusArray addObject: NSLocalizedString(@"Refusing to send data to peer", "Inspector -> peer -> status")];
        if ([[peer objectForKey: @"PeerInterested"] boolValue])
            [peerStatusArray addObject: NSLocalizedString(@"Peer wants our data", "Inspector -> peer -> status")];
        if ([[peer objectForKey: @"ClientChoked"] boolValue])
            [peerStatusArray addObject: NSLocalizedString(@"Peer will not send us data", "Inspector -> peer -> status")];
        if ([[peer objectForKey: @"ClientInterested"] boolValue])
            [peerStatusArray addObject: NSLocalizedString(@"Waiting to request data from peer", "Inspector -> peer -> status")];
        
        if ([peerStatusArray count] > 0)
            [components addObject: [@"\n" stringByAppendingString: [peerStatusArray componentsJoinedByString: @"\n"]]];
        
        return [components componentsJoinedByString: @"\n"];
    }
    return nil;
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

- (void) setSpeedMode: (id) sender
{
    BOOL upload = sender == fUploadLimitPopUp;
    int mode;
    switch ([sender indexOfSelectedItem])
    {
        case OPTION_POPUP_LIMIT:
            mode = TR_SPEEDLIMIT_SINGLE;
            break;
        case OPTION_POPUP_NO_LIMIT:
            mode = TR_SPEEDLIMIT_UNLIMITED;
            break;
        case OPTION_POPUP_GLOBAL:
            mode = TR_SPEEDLIMIT_GLOBAL;
            break;
        default:
            return;
    }
    
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
    int limit = [sender intValue];
    
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    
    while ((torrent = [enumerator nextObject]))
        [torrent setSpeedLimit: limit upload: upload];
}

- (void) setRatioSetting: (id) sender
{
    int setting;
    switch ([sender indexOfSelectedItem])
    {
        case OPTION_POPUP_LIMIT:
            setting = NSOnState;
            break;
        case OPTION_POPUP_NO_LIMIT:
            setting = NSOffState;
            break;
        case OPTION_POPUP_GLOBAL:
            setting = NSMixedState;
            break;
        default:
            return;
    }
    
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
    float limit = [sender floatValue];
    
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [torrent setRatioLimit: limit];
}

- (void) setPeersConnectLimit: (id) sender
{
    int limit = [sender intValue];
    
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [torrent setMaxPeerConnect: limit];
}


- (BOOL) control: (NSControl *) control textShouldBeginEditing: (NSText *) fieldEditor
{
    [fInitialString release];
    fInitialString = [[control stringValue] retain];
    
    return YES;
}

- (BOOL) control: (NSControl *) control didFailToFormatString: (NSString *) string errorDescription: (NSString *) error
{
    NSBeep();
    if (fInitialString)
    {
        [control setStringValue: fInitialString];
        [fInitialString release];
        fInitialString = nil;
    }
    return NO;
}

@end

@implementation InfoWindowController (Private)

- (void) updateInfoGeneral
{   
    if ([fTorrents count] != 1)
        return;
    
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    
    NSString * tracker = [[torrent trackerAddress] stringByAppendingString: [torrent trackerAddressAnnounce]];
    [fTrackerField setStringValue: tracker];
    
    NSString * location = [torrent dataLocation];
    [fDataLocationField setStringValue: [location stringByAbbreviatingWithTildeInPath]];
    [fDataLocationField setToolTip: location];
}

- (void) updateInfoActivity
{
    int numberSelected = [fTorrents count];
    if (numberSelected == 0)
        return;
    
    uint64_t have = 0, haveVerified = 0, downloadedTotal = 0, uploadedTotal = 0, failedHash = 0;
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        have += [torrent haveTotal];
        haveVerified += [torrent haveVerified];
        downloadedTotal += [torrent downloadedTotal];
        uploadedTotal += [torrent uploadedTotal];
        failedHash += [torrent failedHash];
    }
    
    if (have == 0)
        [fHaveField setStringValue: [NSString stringForFileSize: 0]];
    else if (have == haveVerified)
        [fHaveField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ verified",
                "Inspector -> Activity tab -> have"), [NSString stringForFileSize: haveVerified]]];
    else
        [fHaveField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ (%@ verified)",
                "Inspector -> Activity tab -> have"), [NSString stringForFileSize: have], [NSString stringForFileSize: haveVerified]]];
    
    [fDownloadedTotalField setStringValue: [NSString stringForFileSize: downloadedTotal]];
    [fUploadedTotalField setStringValue: [NSString stringForFileSize: uploadedTotal]];
    [fFailedHashField setStringValue: [NSString stringForFileSize: failedHash]];
    
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
    
    int seeders = [torrent seeders], leechers = [torrent leechers], completed = [torrent completedFromTracker];
    [fSeedersField setStringValue: seeders >= 0 ? [NSString stringWithFormat: @"%d", seeders] : @""];
    [fLeechersField setStringValue: leechers >= 0 ? [NSString stringWithFormat: @"%d", leechers] : @""];
    [fCompletedFromTrackerField setStringValue: completed >= 0 ? [NSString stringWithFormat: @"%d", completed] : @""];
    
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
            
            connected = [connected stringByAppendingFormat: @": %@", [components componentsJoinedByString: @", "]];
        }
        
        [fConnectedPeersField setStringValue: connected];
        
        [fDownloadingFromField setIntValue: [torrent peersSendingToUs]];
        [fUploadingToField setIntValue: [torrent peersGettingFromUs]];
    }
    else
    {
        [fConnectedPeersField setStringValue: NSLocalizedString(@"info not available", "Inspector -> Peers tab -> peers")];
        [fDownloadingFromField setStringValue: @""];
        [fUploadingToField setStringValue: @""];
    }
    
    [fKnownField setIntValue: [torrent totalPeersKnown]];
    
    [fPeers release];
    fPeers = [[[torrent peers] sortedArrayUsingDescriptors: [self peerSortDescriptors]] retain];
    
    [fPeerTable reloadData];
}

- (void) updateInfoFiles
{
    if ([fTorrents count] == 1)
        [fFileController reloadData];
}

- (NSView *) tabViewForTag: (int) tag
{
    switch (tag)
    {
        case TAB_INFO_TAG:
            return fInfoView;
        case TAB_ACTIVITY_TAG:
            return fActivityView;
        case TAB_PEERS_TAG:
            return fPeersView;
        case TAB_FILES_TAG:
            return fFilesView;
        case TAB_OPTIONS_TAG:
            return fOptionsView;
        default:
            return nil;
    }
}

- (NSArray *) peerSortDescriptors
{
    NSMutableArray * descriptors = [NSMutableArray arrayWithCapacity: 2];
    
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

@end
