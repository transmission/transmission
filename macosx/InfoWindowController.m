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
#import "InfoTabButtonCell.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

#define FILE_ROW_SMALL_HEIGHT 18.0

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
    TAB_ACTIVITY_TAG,
    TAB_PEERS_TAG,
    TAB_FILES_TAG,
    TAB_OPTIONS_TAG,
} tabTag;

@interface InfoWindowController (Private)

- (void) updateInfoGeneral;
- (void) updateInfoActivity;
- (void) updateInfoPeers;
- (void) updateInfoFiles;
- (void) updateInfoOptions;

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
    [window setAcceptsMouseMovedEvents: YES];
    
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
    
    //set file table
    [fFileOutline setDoubleAction: @selector(revealFile:)];
    
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
        
        [[fFileOutline tableColumnWithIdentifier: @"Check"] setHeaderToolTip: NSLocalizedString(@"Download",
                                                                            "inspector -> file table -> header tool tip")];
        [[fFileOutline tableColumnWithIdentifier: @"Priority"] setHeaderToolTip: NSLocalizedString(@"Priority",
                                                                            "inspector -> file table -> header tool tip")];                                                               
    }
    
    //set priority item images
    [fFilePriorityNormal setImage: [NSImage imageNamed: @"PriorityNormal.png"]];
    [fFilePriorityLow setImage: [NSImage imageNamed: @"PriorityLow.png"]];
    [fFilePriorityHigh setImage: [NSImage imageNamed: @"PriorityHigh.png"]];
    
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
    [fFiles release];
    
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
            NSEnumerator * enumerator = [torrents objectEnumerator];
            Torrent * torrent;
            while ((torrent = [enumerator nextObject]))
                size += [torrent size];
            
            [fSizeField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ Total",
                "Inspector -> above tabs -> total size (several torrents selected)"), [NSString stringForFileSize: size]]];
        }
        else
        {
            [fImageView setImage: [NSImage imageNamed: @"NSApplicationIcon"]];
            
            [fNameField setStringValue: NSLocalizedString(@"No Torrents Selected", "Inspector -> above tabs -> selected torrents")];
            [fSizeField setStringValue: @""];
    
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
            
            [fPexCheck setEnabled: NO];
            [fPexCheck setState: NSOffState];
            [fPexCheck setToolTip: nil];
        }
        
        [fFileOutline setTorrent: nil];
        
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
        
        [fFileOutline setTorrent: torrent];
        
        NSImage * icon = [[torrent icon] copy];
        [icon setFlipped: NO];
        [fImageView setImage: icon];
        [icon release];
        
        NSString * name = [torrent name];
        [fNameField setStringValue: name];
        [fNameField setToolTip: name];
        [fSizeField setStringValue: [NSString stringForFileSize: [torrent size]]];
        
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
    [self updateOptions];
    
    [fPeerTable reloadData];
    [fFileOutline deselectAll: nil];
    [fFileOutline reloadData];
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
        case TAB_OPTIONS_TAG:
            [self updateInfoOptions];
            break;
    }
}

- (void) updateOptions
{
    if ([fTorrents count] == 0)
        return;
    
    [self updateInfoOptions];
    
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
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];
    
    if (action == @selector(revealFile:))
    {
        if ([fTabMatrix selectedTag] != TAB_FILES_TAG)
            return NO;
        
        NSString * downloadFolder = [[fTorrents objectAtIndex: 0] downloadFolder];
        NSIndexSet * indexSet = [fFileOutline selectedRowIndexes];
        int i;
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            if ([[NSFileManager defaultManager] fileExistsAtPath:
                    [downloadFolder stringByAppendingPathComponent: [[fFiles objectAtIndex: i] objectForKey: @"Path"]]])
                return YES;
        return NO;
    }
    
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
        
        switch ([[peer objectForKey: @"Status"] intValue]) 
        { 
            case TR_PEER_STATUS_HANDSHAKE:
                [components addObject: NSLocalizedString(@"Handshaking", "peer -> status")];
                break;
            case TR_PEER_STATUS_PEER_IS_CHOKED:
                [components addObject: NSLocalizedString(@"Peer is Choked", "peer -> status")];
                break;
            case TR_PEER_STATUS_CLIENT_IS_CHOKED:
                [components addObject: NSLocalizedString(@"Choked", "peer -> status")];
                break;
            case TR_PEER_STATUS_CLIENT_IS_INTERESTED:
                [components addObject: NSLocalizedString(@"Choked & Interested", "peer -> status")];
                break;
            case TR_PEER_STATUS_READY:
                [components addObject: NSLocalizedString(@"Ready", "peer -> status")];
                break;
            case TR_PEER_STATUS_REQUEST_SENT:
                [components addObject: NSLocalizedString(@"Request Sent", "peer -> status")];
                break;
            case TR_PEER_STATUS_ACTIVE:
                [components addObject: NSLocalizedString(@"Active", "peer -> status")];
                break;
            case TR_PEER_STATUS_ACTIVE_AND_CHOKED:
                [components addObject: NSLocalizedString(@"Active & Interested", "peer -> status")];
                break;
        }
        
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

- (NSString *) outlineView: (NSOutlineView *) outlineView typeSelectStringForTableColumn: (NSTableColumn *) tableColumn item: (id) item
{
    return [item objectForKey: @"Name"];
}

- (NSString *) outlineView: (NSOutlineView *) outlineView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
        tableColumn: (NSTableColumn *) tableColumn item: (id) item mouseLocation: (NSPoint) mouseLocation
{
    NSString * ident = [tableColumn identifier];
    if ([ident isEqualToString: @"Name"])
        return [[[fTorrents objectAtIndex: 0] downloadFolder] stringByAppendingPathComponent: [item objectForKey: @"Path"]];
    else if ([ident isEqualToString: @"Check"])
    {
        switch ([cell state])
        {
            case NSOffState:
                return NSLocalizedString(@"Don't Download", "Inspector -> files tab -> tooltip");
            case NSOnState:
                return NSLocalizedString(@"Download", "Inspector -> files tab -> tooltip");
            case NSMixedState:
                return NSLocalizedString(@"Download Some", "Inspector -> files tab -> tooltip");
        }
    }
    else if ([ident isEqualToString: @"Priority"])
    {
        NSSet * priorities = [[fTorrents objectAtIndex: 0] filePrioritiesForIndexes: [item objectForKey: @"Indexes"]];
        switch([priorities count])
        {
            case 0:
                return NSLocalizedString(@"Priority Not Available", "Inspector -> files tab -> tooltip");
            case 1:
                switch ([[priorities anyObject] intValue])
                {
                    case TR_PRI_LOW:
                        return NSLocalizedString(@"Low Priority", "Inspector -> files tab -> tooltip");
                    case TR_PRI_HIGH:
                        return NSLocalizedString(@"High Priority", "Inspector -> files tab -> tooltip");
                    case TR_PRI_NORMAL:
                        return NSLocalizedString(@"Normal Priority", "Inspector -> files tab -> tooltip");
                }
                break;
            default:
                return NSLocalizedString(@"Multiple Priorities", "Inspector -> files tab -> tooltip");
        }
    }
    else;
    
    return nil;
}

- (float) outlineView: (NSOutlineView *) outlineView heightOfRowByItem: (id) item
{
    if ([[item objectForKey: @"IsFolder"] boolValue])
        return FILE_ROW_SMALL_HEIGHT;
    else
        return [outlineView rowHeight];
}

- (void) mouseMoved: (NSEvent *) event
{
    [fFileOutline setHoverRowForEvent: fCurrentTabTag == TAB_FILES_TAG ? event : nil];
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
    {
        [[fTorrents objectAtIndex: 0] updateFileStat];
        [fFileOutline reloadData];
    }
}

- (void) updateInfoOptions
{
    if ([fTorrents count] == 0)
        return;
    
    //set pex check
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent = [enumerator nextObject]; //first torrent
    
    BOOL pexEnabled = ![torrent privateTorrent] && ![torrent isActive];
    int pexState = [torrent pex] ? NSOnState : NSOffState;
    
    while ((torrent = [enumerator nextObject]) && (pexEnabled || pexState != NSMixedState))
    {
        if (pexEnabled)
            pexEnabled = ![torrent privateTorrent] && ![torrent isActive];
        
        if (pexState != NSMixedState && pexState != ([torrent pex] ? NSOnState : NSOffState))
            pexState = NSMixedState;
    }
    
    [fPexCheck setEnabled: pexEnabled];
    [fPexCheck setState: pexState];
    [fPexCheck setToolTip: !pexEnabled ? NSLocalizedString(@"PEX can only be toggled on paused public torrents.",
                                "Inspector -> pex check") : nil];
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
