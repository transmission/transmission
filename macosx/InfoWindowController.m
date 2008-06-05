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
#import "FileOutlineView.h"
#import "FileListNode.h"
#import "TrackerTableView.h"
#import "QuickLookController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

#define TAB_INFO_IDENT @"Info"
#define TAB_ACTIVITY_IDENT @"Activity"
#define TAB_TRACKER_IDENT @"Tracker"
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

#define TRACKER_ADD_TAG 0
#define TRACKER_REMOVE_TAG 1

typedef enum
{
    TAB_INFO_TAG = 0,
    TAB_ACTIVITY_TAG = 1,
    TAB_TRACKER_TAG = 2,
    TAB_PEERS_TAG = 3,
    TAB_FILES_TAG = 4,
    TAB_OPTIONS_TAG = 5
} tabTag;

@interface InfoWindowController (Private)

- (void) updateInfoGeneral;
- (void) updateInfoActivity;
- (void) updateInfoTracker;
- (void) updateInfoPeers;
- (void) updateInfoFiles;

- (NSView *) tabViewForTag: (int) tag;
- (NSArray *) peerSortDescriptors;

- (void) addTrackers;
- (void) removeTrackers;

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
    [[fTabMatrix cellWithTag: TAB_TRACKER_TAG] setIcon: [NSImage imageNamed: @"InfoTracker.png"]];
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
    else if ([identifier isEqualToString: TAB_TRACKER_IDENT])
        tag = TAB_TRACKER_TAG;
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
    
    //reset images for reveal buttons, since the images are also used in the main table
    NSImage * revealOn = [[NSImage imageNamed: @"RevealOn.png"] copy],
            * revealOff = [[NSImage imageNamed: @"RevealOff.png"] copy];
    [revealOn setFlipped: NO];
    [revealOff setFlipped: NO];
    
    [fRevealDataButton setImage: revealOff];
    [fRevealDataButton setAlternateImage: revealOn];
    [fRevealTorrentButton setImage: revealOff];
    [fRevealTorrentButton setAlternateImage: revealOn];
    
    [revealOn release];
    [revealOff release];
    
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
    else
    {
        [fTrackerAddRemoveControl sizeToFit];
        [fTrackerAddRemoveControl setLabel: @"+" forSegment: TRACKER_ADD_TAG];
        [fTrackerAddRemoveControl setLabel: @"-" forSegment: TRACKER_REMOVE_TAG];
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
    NSString * resizeSaveKey = nil;
    switch (fCurrentTabTag)
    {
        case TAB_TRACKER_TAG:
            resizeSaveKey = @"InspectorContentHeightTracker";
            break;
        case TAB_PEERS_TAG:
            resizeSaveKey = @"InspectorContentHeightPeers";
            break;
        case TAB_FILES_TAG:
            resizeSaveKey = @"InspectorContentHeightFiles";
            break;
    }
    if (resizeSaveKey)
        [[NSUserDefaults standardUserDefaults] setFloat: [[self tabViewForTag: fCurrentTabTag] frame].size.height forKey: resizeSaveKey];
    
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fTorrents release];
    [fPeers release];
    [fTrackers release];
    
    [super dealloc];
}

- (void) setInfoForTorrents: (NSArray *) torrents
{
    if (fTorrents && [fTorrents isEqualToArray: torrents])
        return;
    
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
                                            "Inspector -> selected torrents"), numberSelected]];
        
            uint64_t size = 0;
            int fileCount = 0;
            NSEnumerator * enumerator = [torrents objectEnumerator];
            Torrent * torrent;
            while ((torrent = [enumerator nextObject]))
            {
                size += [torrent size];
                fileCount += [torrent fileCount];
            }
            
            [fBasicInfoField setStringValue: [NSString stringWithFormat: @"%@, %@",
                [NSString stringWithFormat: NSLocalizedString(@"%d files", "Inspector -> selected torrents"), fileCount],
                [NSString stringWithFormat: NSLocalizedString(@"%@ total", "Inspector -> selected torrents"),
                [NSString stringForFileSize: size]]]];
            [fBasicInfoField setToolTip: [NSString stringWithFormat: NSLocalizedString(@"%llu bytes", "Inspector -> selected torrents"),
                                            size]];
        }
        else
        {
            [fImageView setImage: [NSImage imageNamed: @"NSApplicationIcon"]];
            
            [fNameField setStringValue: NSLocalizedString(@"No Torrents Selected", "Inspector -> selected torrents")];
            [fBasicInfoField setStringValue: @""];
            [fBasicInfoField setToolTip: @""];
    
            [fHaveField setStringValue: @""];
            [fDownloadedTotalField setStringValue: @""];
            [fUploadedTotalField setStringValue: @""];
            [fFailedHashField setStringValue: @""];
            [fDateActivityField setStringValue: @""];
            
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
        
        [fAnnounceAddressField setStringValue: @""];
        [fAnnounceAddressField setToolTip: nil];
        [fAnnounceAddressField setSelectable: NO];
        [fAnnounceLastField setStringValue: @""];
        [fAnnounceResponseField setStringValue: @""];
        [fAnnounceResponseField setToolTip: nil];
        [fAnnounceResponseField setSelectable: NO];
        [fAnnounceNextField setStringValue: @""];
        
        [fScrapeAddressField setStringValue: @""];
        [fScrapeAddressField setToolTip: nil];
        [fScrapeAddressField setSelectable: NO];
        [fScrapeLastField setStringValue: @""];
        [fScrapeResponseField setStringValue: @""];
        [fScrapeResponseField setToolTip: nil];
        [fScrapeResponseField setSelectable: NO];
        [fScrapeNextField setStringValue: @""];
        
        [fConnectedPeersField setStringValue: @""];
        [fDownloadingFromField setStringValue: @""];
        [fUploadingToField setStringValue: @""];
        [fKnownField setStringValue: @""];
        [fSeedersField setStringValue: @""];
        [fLeechersField setStringValue: @""];
        [fCompletedFromTrackerField setStringValue: @""];
        
        [fDateAddedField setStringValue: @""];
        [fDateCompletedField setStringValue: @""];
        
        [fPiecesControl setSelected: NO forSegment: PIECES_CONTROL_AVAILABLE];
        [fPiecesControl setSelected: NO forSegment: PIECES_CONTROL_PROGRESS];
        [fPiecesControl setEnabled: NO];
        [fPiecesView setTorrent: nil];
        
        [fPeers release];
        fPeers = nil;
        
        [fTrackers release];
        fTrackers = nil;
        
        [fTrackerAddRemoveControl setEnabled: NO forSegment: TRACKER_ADD_TAG];
        [fTrackerAddRemoveControl setEnabled: NO forSegment: TRACKER_REMOVE_TAG];
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
        if ([torrent isFolder])
        {
            NSString * fileString;
            int fileCount = [torrent fileCount];
            if (fileCount == 1)
                fileString = NSLocalizedString(@"1 file", "Inspector -> selected torrents");
            else
                fileString= [NSString stringWithFormat: NSLocalizedString(@"%d files", "Inspector -> selected torrents"), fileCount];
            basicString = [NSString stringWithFormat: @"%@, %@", fileString, basicString];
        }
        [fBasicInfoField setStringValue: basicString];
        [fBasicInfoField setToolTip: [NSString stringWithFormat: NSLocalizedString(@"%llu bytes", "Inspector -> selected torrents"),
                                        [torrent size]]];
        
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
            [fTorrentLocationField setToolTip: [[torrent publicTorrentLocation] stringByAppendingFormat: @"\n\n%@",
                                                [torrent torrentLocation]]];
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
        [fAnnounceAddressField setSelectable: YES];
        [fScrapeAddressField setSelectable: YES];
        
        //set pieces view
        BOOL piecesAvailableSegment = [[NSUserDefaults standardUserDefaults] boolForKey: @"PiecesViewShowAvailability"];
        [fPiecesControl setSelected: piecesAvailableSegment forSegment: PIECES_CONTROL_AVAILABLE];
        [fPiecesControl setSelected: !piecesAvailableSegment forSegment: PIECES_CONTROL_PROGRESS];
        [fPiecesControl setEnabled: YES];
        [fPiecesView setTorrent: torrent];
        
        //get trackers for table
        [fTrackers release];
        fTrackers = [[torrent allTrackers: YES] retain];
        [fTrackerTable deselectAll: self];
        
        [fTrackerAddRemoveControl setEnabled: YES forSegment: TRACKER_ADD_TAG];
        [fTrackerAddRemoveControl setEnabled: NO forSegment: TRACKER_REMOVE_TAG];
    }
    
    //update stats and settings
    [self updateInfoStats];
    [self updateOptions];
    
    [fTrackerTable setTrackers: fTrackers];
    [fTrackerTable reloadData];
    
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
        case TAB_TRACKER_TAG:
            [self updateInfoTracker];
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
    int oldTabTag = fCurrentTabTag;
    fCurrentTabTag = [fTabMatrix selectedTag];
    if (fCurrentTabTag == oldTabTag)
        return;
    
    [self updateInfoStats];
    
    //take care of old view
    float oldHeight = 0;
    NSString * oldResizeSaveKey = nil;
    if (oldTabTag != INVALID)
    {
        //deselect old tab item
        [(InfoTabButtonCell *)[fTabMatrix cellWithTag: oldTabTag] setSelectedTab: NO];
        
        switch (oldTabTag)
        {
            case TAB_TRACKER_TAG:
                oldResizeSaveKey = @"InspectorContentHeightTracker";
                break;
            case TAB_PEERS_TAG:
                [fPeers release];
                fPeers = nil;
                
                oldResizeSaveKey = @"InspectorContentHeightPeers";
                break;
            case TAB_FILES_TAG:
                [[QuickLookController quickLook] updateQuickLook];
                
                oldResizeSaveKey = @"InspectorContentHeightFiles";
                break;
        }
        
        NSView * oldView = [self tabViewForTag: oldTabTag];
        oldHeight = [oldView frame].size.height;
        if (oldResizeSaveKey)
            [[NSUserDefaults standardUserDefaults] setFloat: oldHeight forKey: oldResizeSaveKey];
        
        //remove old view
        [oldView setHidden: YES];
        [oldView removeFromSuperview];
    }
    
    //set new tab item
    NSView * view = [self tabViewForTag: fCurrentTabTag];
    
    NSString * resizeSaveKey = nil;
    NSString * identifier, * title;
    switch (fCurrentTabTag)
    {
        case TAB_INFO_TAG:
            identifier = TAB_INFO_IDENT;
            title = NSLocalizedString(@"General Info", "Inspector -> title");
            break;
        case TAB_ACTIVITY_TAG:
            identifier = TAB_ACTIVITY_IDENT;
            title = NSLocalizedString(@"Activity", "Inspector -> title");
            
            [fPiecesView updateView: YES];
            break;
        case TAB_TRACKER_TAG:
            identifier = TAB_TRACKER_IDENT;
            title = NSLocalizedString(@"Tracker", "Inspector -> title");
            resizeSaveKey = @"InspectorContentHeightTracker";
            break;
        case TAB_PEERS_TAG:
            identifier = TAB_PEERS_IDENT;
            title = NSLocalizedString(@"Peers", "Inspector -> title");
            resizeSaveKey = @"InspectorContentHeightPeers";
            break;
        case TAB_FILES_TAG:
            identifier = TAB_FILES_IDENT;
            title = NSLocalizedString(@"Files", "Inspector -> title");
            resizeSaveKey = @"InspectorContentHeightFiles";
            break;
        case TAB_OPTIONS_TAG:
            identifier = TAB_OPTIONS_IDENT;
            title = NSLocalizedString(@"Options", "Inspector -> title");
            break;
        default:
            return;
    }
    
    [[NSUserDefaults standardUserDefaults] setObject: identifier forKey: @"InspectorSelected"];
    
    NSWindow * window = [self window];
    
    [window setTitle: [NSString stringWithFormat: @"%@ - %@", title, NSLocalizedString(@"Torrent Inspector", "Inspector -> title")]];
    
    //selected tab item
    [(InfoTabButtonCell *)[fTabMatrix selectedCell] setSelectedTab: YES];
    
    NSRect windowRect = [window frame], viewRect = [view frame];
    
    if (resizeSaveKey)
    {
        float height = [[NSUserDefaults standardUserDefaults] floatForKey: resizeSaveKey];
        if (height != 0)
            viewRect.size.height = MAX(height, TAB_MIN_HEIGHT);
    }
    
    float difference = (viewRect.size.height - oldHeight) * [window userSpaceScaleFactor];
    windowRect.origin.y -= difference;
    windowRect.size.height += difference;
    
    if (resizeSaveKey)
    {
        if (!oldResizeSaveKey)
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
    
    if (fCurrentTabTag == TAB_FILES_TAG)
        [[QuickLookController quickLook] updateQuickLook];
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
    else if (tableView == fTrackerTable)
        return fTrackers ? [fTrackers count] : 0;
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
    else if (tableView == fTrackerTable)
    {
        id item = [fTrackers objectAtIndex: row];
        if ([item isKindOfClass: [NSNumber class]])
        {
            int tier = [item intValue];
            if (tier == 0)
                return NSLocalizedString(@"User-Added", "Inspector -> tracker table");
            else
                return [NSString stringWithFormat: NSLocalizedString(@"Tier %d", "Inspector -> tracker table"), tier];
        }
        else
            return item;
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
    return tableView == fTrackerTable;
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    if ([notification object] == fTrackerTable)
    {
        int numSelected = [fTrackerTable numberOfSelectedRows];
        [fTrackerAddRemoveControl setEnabled: numSelected > 0 forSegment: TRACKER_REMOVE_TAG];
    }
}

- (BOOL) tableView: (NSTableView *) tableView isGroupRow: (NSInteger) row
{
    if (tableView == fTrackerTable)
        return [[fTrackers objectAtIndex: row] isKindOfClass: [NSNumber class]];
    return NO;
}

- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) column row: (int) row mouseLocation: (NSPoint) mouseLocation
{
    if (tableView == fPeerTable)
    {
        NSDictionary * peer = [fPeers objectAtIndex: row];
        NSMutableArray * components = [NSMutableArray arrayWithCapacity: 5];
        
        [components addObject: [NSString localizedStringWithFormat: NSLocalizedString(@"Progress: %.1f%%",
            "Inspector -> Peers tab -> table row tooltip"), [[peer objectForKey: @"Progress"] floatValue] * 100.0]];
        
        if ([[peer objectForKey: @"Encryption"] boolValue])
            [components addObject: NSLocalizedString(@"Encrypted Connection", "Inspector -> Peers tab -> table row tooltip")];
        
        NSString * portString;
        int port;
        if ((port = [[peer objectForKey: @"Port"] intValue]) > 0)
            portString = [NSString stringWithFormat: @"%d", port];
        else
            portString = NSLocalizedString(@"N/A", "Inspector -> Peers tab -> table row tooltip");
        [components addObject: [NSString stringWithFormat: @"%@: %@", NSLocalizedString(@"Port",
            "Inspector -> Peers tab -> table row tooltip"), portString]];
        
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
        
        //determing status strings from flags 
        NSMutableArray * statusArray = [NSMutableArray arrayWithCapacity: 3];
        NSString * flags = [peer objectForKey: @"Flags"];
        
        if ([flags rangeOfString: @"D"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Currently downloading (interested and not choked)",
                "Inspector -> peer -> status")];
        else if ([flags rangeOfString: @"d"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"You want to download, but peer does not want to send (interested and choked)",
                "Inspector -> peer -> status")];
        else;
        
        if ([flags rangeOfString: @"U"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Currently uploading (interested and not choked)",
                "Inspector -> peer -> status")];
        else if ([flags rangeOfString: @"u"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Peer wants you to upload, but you do not want to (interested and choked)",
                "Inspector -> peer -> status")];
        else;
        
        if ([flags rangeOfString: @"K"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Peer is unchoking you, but you are not interested",
                "Inspector -> peer -> status")];
        
        if ([flags rangeOfString: @"?"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"You unchoked the peer, but the peer is not interested",
                "Inspector -> peer -> status")];
        
        if ([statusArray count] > 0)
            [components addObject: [@"\n" stringByAppendingString: [statusArray componentsJoinedByString: @"\n\n"]]];
        
        return [components componentsJoinedByString: @"\n"];
    }
    return nil;
}

- (void) tableView: (NSTableView *) tableView setObjectValue: (id) object forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    if (tableView != fTrackerTable)
        return;
    
    [fTrackers replaceObjectAtIndex: row withObject: object];
    
    Torrent * torrent= [fTorrents objectAtIndex: 0];
    if (![torrent updateAllTrackersForAdd: fTrackers])
        NSBeep();
    
    //reset table with either new or old value
    [fTrackers release];
    fTrackers = [[torrent allTrackers: YES] retain];
    [fTrackerTable deselectAll: self];
    
    [fTrackerTable setTrackers: fTrackers];
    [fTrackerTable reloadData];
}

- (void) addRemoveTracker: (id) sender
{
    //don't allow add/remove when currently adding - it leads to weird results
    if ([fTrackerTable editedRow] != -1)
        return;
    
    if ([[sender cell] tagForSegment: [sender selectedSegment]] == TRACKER_REMOVE_TAG)
        [self removeTrackers];
    else
        [self addTrackers];
}

- (BOOL) tableView: (NSTableView *) tableView shouldEditTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    if (tableView != fTrackerTable)
        return NO;
    
    //only allow modification of custom-added trackers
    if ([[fTrackers objectAtIndex: row] isKindOfClass: [NSNumber class]] || ![[fTorrents objectAtIndex: 0] hasAddedTrackers])
        return NO;
    
    NSUInteger i;
    for (i = row-1; ![[fTrackers objectAtIndex: i] isKindOfClass: [NSNumber class]]; i--);
    
    return [[fTrackers objectAtIndex: i] intValue] == 0;
}

- (BOOL) shouldQuickLookFileView
{
    return [[self window] isVisible] && fCurrentTabTag == TAB_FILES_TAG && [[fFileController outlineView] numberOfSelectedRows] > 0;
}

- (NSArray *) quickLookURLs
{
    FileOutlineView * fileOutlineView = [fFileController outlineView];
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSString * folder = [torrent downloadFolder];
    NSIndexSet * indexes = [fileOutlineView selectedRowIndexes];
    NSMutableArray * urlArray = [NSMutableArray arrayWithCapacity: [indexes count]];

    int i;
    for (i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
    {
        FileListNode * item = [fileOutlineView itemAtRow: i];
        if ([item isFolder] || [torrent fileProgress: [[item indexes] firstIndex]] == 1.0)
            [urlArray addObject: [NSURL fileURLWithPath: [folder stringByAppendingPathComponent: [item fullPath]]]];
    }
    
    return urlArray;
}

- (BOOL) canQuickLook
{
    FileOutlineView * fileOutlineView = [fFileController outlineView];
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSIndexSet * indexes = [fileOutlineView selectedRowIndexes];

    int i;
    for (i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
    {
        FileListNode * item = [fileOutlineView itemAtRow: i];
        if ([item isFolder] || [torrent fileProgress: [[item indexes] firstIndex]] == 1.0)
            return YES;
    }
    
    return NO;
}

- (NSRect) quickLookFrameWithURL: (NSURL *) url
{
    FileOutlineView * fileOutlineView = [fFileController outlineView];
    
    NSString * fullPath = [url path];
    NSString * folder = [[fTorrents objectAtIndex: 0] downloadFolder];
    NSRange visibleRows = [fileOutlineView rowsInRect: [fileOutlineView bounds]];
    
    int row;
    for (row = visibleRows.location; row < NSMaxRange(visibleRows); row++)
    {
        FileListNode * rowItem = [fileOutlineView itemAtRow: row];
        if ([[folder stringByAppendingPathComponent: [rowItem fullPath]] isEqualToString: fullPath])
        {
            NSRect frame = [fileOutlineView iconRectForRow: row];
            frame.origin = [fileOutlineView convertPoint: frame.origin toView: nil];
            frame.origin = [[self window] convertBaseToScreen: frame.origin];
            frame.origin.y -= frame.size.height;
            return frame;
        }
    }
    
    return NSZeroRect;
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
    
    [fTrackerField setStringValue: [torrent trackerAddressAnnounce]];
    
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
    NSDate * lastActivity = nil;
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        have += [torrent haveTotal];
        haveVerified += [torrent haveVerified];
        downloadedTotal += [torrent downloadedTotal];
        uploadedTotal += [torrent uploadedTotal];
        failedHash += [torrent failedHash];
        
        NSDate * nextLastActivity;
        if ((nextLastActivity = [torrent dateActivity]))
            lastActivity = lastActivity ? [lastActivity laterDate: nextLastActivity] : nextLastActivity;
    }
    
    if (have == 0)
        [fHaveField setStringValue: [NSString stringForFileSize: 0]];
    else
    {
        NSString * verifiedString = [NSString stringWithFormat: NSLocalizedString(@"%@ verified", "Inspector -> Activity tab -> have"),
                                        [NSString stringForFileSize: haveVerified]];
        if (have == haveVerified)
            [fHaveField setStringValue: verifiedString];
        else
            [fHaveField setStringValue: [NSString stringWithFormat: @"%@ (%@)", [NSString stringForFileSize: have], verifiedString]];
    }
    
    [fDownloadedTotalField setStringValue: [NSString stringForFileSize: downloadedTotal]];
    [fUploadedTotalField setStringValue: [NSString stringForFileSize: uploadedTotal]];
    [fFailedHashField setStringValue: [NSString stringForFileSize: failedHash]];
    
    [fDateActivityField setObjectValue: lastActivity];
    
    if (numberSelected == 1)
    {
        torrent = [fTorrents objectAtIndex: 0];
        
        [fStateField setStringValue: [torrent stateString]];
        
        if ([torrent isFolder])
            [fProgressField setStringValue: [NSString localizedStringWithFormat: NSLocalizedString(@"%.2f%% (%.2f%% selected)",
                "Inspector -> Activity tab -> progress"), 100.0 * [torrent progress], 100.0 * [torrent progressDone]]];
        else
            [fProgressField setStringValue: [NSString localizedStringWithFormat: @"%.2f%%", 100.0 * [torrent progress]]];
            
        [fRatioField setStringValue: [NSString stringForRatio: [torrent ratio]]];
        [fSwarmSpeedField setStringValue: [torrent isActive] ? [NSString stringForSpeed: [torrent swarmSpeed]] : @""];
        
        NSString * errorMessage = [torrent errorMessage];
        if (![errorMessage isEqualToString: [fErrorMessageView string]])
        {
            [fErrorMessageView setString: errorMessage];
            [fErrorMessageView setSelectable: ![errorMessage isEqualToString: @""]];
        }
        
        [fDateCompletedField setObjectValue: [torrent dateCompleted]];
        
        [fPiecesView updateView: NO];
    }
}

#warning reload table when necessary?
- (void) updateInfoTracker
{
    if ([fTorrents count] != 1)
        return;
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    
    //announce fields
    NSString * announceAddress = [torrent trackerAddressAnnounce];
    [fAnnounceAddressField setStringValue: announceAddress];
    [fAnnounceAddressField setToolTip: announceAddress];
    
    [fAnnounceLastField setObjectValue: [torrent lastAnnounceTime]];
    
    NSString * announceResponse = [torrent announceResponse];
    [fAnnounceResponseField setStringValue: announceResponse];
    [fAnnounceResponseField setToolTip: announceResponse];
    [fAnnounceResponseField setSelectable: ![announceResponse isEqualToString: @""]];
    
    int announceNext = [torrent nextAnnounceTime];
    [fAnnounceNextField setStringValue: announceNext > 0 ? [NSString timeString: announceNext showSeconds: YES] : @""];
    
    //scrape fields
    NSString * scrapeAddress;
    if ((scrapeAddress = [torrent trackerAddressScrape]))
    {
        [fScrapeAddressField setStringValue: scrapeAddress];
        [fScrapeAddressField setToolTip: scrapeAddress];
    }
    else
    {
        [fScrapeAddressField setStringValue: @""];
        [fScrapeAddressField setToolTip: @""];
    }
    
    [fScrapeLastField setObjectValue: [torrent lastScrapeTime]];
    
    NSString * scrapeResponse = [torrent scrapeResponse];
    [fScrapeResponseField setStringValue: scrapeResponse];
    [fScrapeResponseField setToolTip: scrapeResponse];
    [fScrapeResponseField setSelectable: ![scrapeResponse isEqualToString: @""]];
    
    int scrapeNext = [torrent nextScrapeTime];
    [fScrapeNextField setStringValue: scrapeNext > 0 ? [NSString timeString: scrapeNext showSeconds: YES] : @""];
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
        [fConnectedPeersField setStringValue: @""];
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
        case TAB_TRACKER_TAG:
            return fTrackerView;
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

- (void) addTrackers
{
    [[self window] makeKeyWindow];
    
    int i;
    if ([[fTorrents objectAtIndex: 0] hasAddedTrackers])
    {
        for (i = 1; i < [fTrackers count]; i++)
            if ([[fTrackers objectAtIndex: i] isKindOfClass: [NSNumber class]])
                break;
    }
    else
    {
        [fTrackers insertObject: [NSNumber numberWithInt: 0] atIndex: 0];
        i = 1;
    }
    
    [fTrackers insertObject: @"" atIndex: i];
    [fTrackerTable reloadData];
    [fTrackerTable selectRow: i byExtendingSelection: NO];
    [fTrackerTable editColumn: 0 row: i withEvent: nil select: YES];
}

- (void) removeTrackers
{
    NSMutableIndexSet * indexes = [[[fTrackerTable selectedRowIndexes] mutableCopy] autorelease];
    
    //get all rows to remove and determine if any built-in trackers are being remove
    NSUInteger i = 0, numberBuiltIn = 0;
    while (i < [fTrackers count])
    {
        BOOL builtIn = i != 0 || [[fTrackers objectAtIndex: i] intValue] != 0;
        
        //if a group is selected, remove all trackers in the group
        if ([indexes containsIndex: i])
        {
            for (i = i+1; i < [fTrackers count] && ![[fTrackers objectAtIndex: i] isKindOfClass: [NSNumber class]]; i++)
            {
                [indexes addIndex: i];
                if (builtIn)
                    numberBuiltIn++;
            }
        }
        //remove empty groups
        else
        {
            BOOL allSelected = YES;
            NSUInteger j;
            for (j = i+1; j < [fTrackers count] && ![[fTrackers objectAtIndex: j] isKindOfClass: [NSNumber class]]; j++)
            {
                if (![indexes containsIndex: j])
                    allSelected = NO;
                else if (builtIn)
                    numberBuiltIn++;
                else;
            }
            
            if (allSelected)
                [indexes addIndex: i];
            
            i = j;
        }
    }
    
    if ([fTrackers count] == [indexes count])
    {
        NSBeep();
        return;
    }
    
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    
    //determine if removing trackers built into the torrent
    if (numberBuiltIn > 0 && [[NSUserDefaults standardUserDefaults] boolForKey: @"WarningRemoveBuiltInTracker"])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        
        if (numberBuiltIn > 1)
        {
            [alert setMessageText: [NSString stringWithFormat:
                                    NSLocalizedString(@"Are you sure you want to remove %d built-in trackers?",
                                    "Remove built-in tracker alert -> title"), numberBuiltIn]];
            [alert setInformativeText: NSLocalizedString(@"These tracker addresses are part of the torrent file."
                " Once removed, Transmission will no longer attempt to contact them.", "Remove built-in tracker alert -> message")];
        }
        else
        {
            [alert setMessageText: NSLocalizedString(@"Are you sure you want to remove a built-in tracker?",
                                    "Remove built-in tracker alert -> title")];
            [alert setInformativeText: NSLocalizedString(@"The tracker address is part of the torrent file."
                " Once removed, Transmission will no longer attempt to contact it.", "Remove built-in tracker alert -> message")];
        }
        
        [alert addButtonWithTitle: NSLocalizedString(@"Remove", "Remove built-in tracker alert -> button")];
        [alert addButtonWithTitle: NSLocalizedString(@"Cancel", "Remove built-in tracker alert -> button")];
        
        BOOL onLeopard = [NSApp isOnLeopardOrBetter];
        if (onLeopard)
            [alert setShowsSuppressionButton: YES];
        else
            [alert addButtonWithTitle: NSLocalizedString(@"Don't Alert Again", "Remove built-in tracker alert -> button")];

        NSInteger result = [alert runModal];
        if ((onLeopard ? [[alert suppressionButton] state] == NSOnState : result == NSAlertThirdButtonReturn))
            [[NSUserDefaults standardUserDefaults] setBool: NO forKey: @"WarningRemoveBuiltInTracker"];
        [alert release];
        
        if (result != NSAlertFirstButtonReturn)
            return;
    }
    
    [fTrackers removeObjectsAtIndexes: indexes];
    
    if (![torrent updateAllTrackersForRemove: fTrackers])
        NSBeep();
    else
        [fTrackerTable deselectAll: self];
    
    //reset table with either new or old value
    [fTrackers release];
    fTrackers = [[torrent allTrackers: YES] retain];
    
    [fTrackerTable setTrackers: fTrackers];
    [fTrackerTable reloadData];
}

@end
