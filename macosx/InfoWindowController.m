/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2010 Transmission authors and contributors
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
#import "FileListNode.h"
#import "FileOutlineController.h"
#import "FileOutlineView.h"
#import "InfoTabButtonCell.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "PeerProgressIndicatorCell.h"
#import "PiecesView.h"
#import "Torrent.h"
#import "TrackerCell.h"
#import "TrackerNode.h"
#import "TrackerTableView.h"
#include "utils.h" //tr_getRatio()

#define TAB_INFO_IDENT @"Info"
#define TAB_ACTIVITY_IDENT @"Activity"
#define TAB_TRACKER_IDENT @"Tracker"
#define TAB_PEERS_IDENT @"Peers"
#define TAB_FILES_IDENT @"Files"
#define TAB_OPTIONS_IDENT @"Options"

#define TAB_MIN_HEIGHT 250

#define TRACKER_GROUP_SEPARATOR_HEIGHT 14.0

#define PIECES_CONTROL_PROGRESS 0
#define PIECES_CONTROL_AVAILABLE 1

#define OPTION_POPUP_GLOBAL 0
#define OPTION_POPUP_NO_LIMIT 1
#define OPTION_POPUP_LIMIT 2

#define OPTION_POPUP_PRIORITY_HIGH 0
#define OPTION_POPUP_PRIORITY_NORMAL 1
#define OPTION_POPUP_PRIORITY_LOW 2

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

- (void) resetInfo;
- (void) resetInfoForTorrent: (NSNotification *) notification;

- (void) updateInfoGeneral;
- (void) updateInfoActivity;
- (void) updateInfoTracker;
- (void) updateInfoPeers;
- (void) updateInfoFiles;

- (NSView *) tabViewForTag: (NSInteger) tag;
- (void) setWebSeedTableHidden: (BOOL) hide animate: (BOOL) animate;
- (NSArray *) peerSortDescriptors;

- (BOOL) canQuickLookFile: (FileListNode *) item;

- (void) addTrackers;
- (void) removeTrackers;

@end

@implementation InfoWindowController

- (id) init
{
    if ((self = [super initWithWindowNibName: @"InfoWindow"]))
    {
        fTrackerCell = [[TrackerCell alloc] init];
    }
    
    return self;
}

- (void) awakeFromNib
{
    //window location and size
    NSPanel * window = (NSPanel *)[self window];
    
    CGFloat windowHeight = [window frame].size.height;
    
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
    NSInteger tag;
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
    
    if (![NSApp isOnSnowLeopardOrBetter])
    {
        //reset images for reveal button, since the images are also used in the main table
        NSImage * revealOn = [[NSImage imageNamed: @"RevealOn.png"] copy],
                * revealOff = [[NSImage imageNamed: @"RevealOff.png"] copy];
        
        [revealOn setFlipped: NO];
        [revealOff setFlipped: NO];
        
        [fRevealDataButton setImage: revealOff];
        [fRevealDataButton setAlternateImage: revealOn];
        
        [revealOn release];
        [revealOff release];
    }
    
    //initially sort peer table by IP
    if ([[fPeerTable sortDescriptors] count] == 0)
        [fPeerTable setSortDescriptors: [NSArray arrayWithObject: [[fPeerTable tableColumnWithIdentifier: @"IP"]
                                            sortDescriptorPrototype]]];
    
    //initially sort webseed table by address
    if ([[fWebSeedTable sortDescriptors] count] == 0)
        [fWebSeedTable setSortDescriptors: [NSArray arrayWithObject: [[fWebSeedTable tableColumnWithIdentifier: @"Address"]
                                            sortDescriptorPrototype]]];
    
    //set table header tool tips
    [[fPeerTable tableColumnWithIdentifier: @"Encryption"] setHeaderToolTip: NSLocalizedString(@"Encrypted Connection",
                                                                        "inspector -> peer table -> header tool tip")];
    [[fPeerTable tableColumnWithIdentifier: @"Progress"] setHeaderToolTip: NSLocalizedString(@"Available",
                                                                        "inspector -> peer table -> header tool tip")];
    [[fPeerTable tableColumnWithIdentifier: @"UL To"] setHeaderToolTip: NSLocalizedString(@"Uploading To Peer",
                                                                        "inspector -> peer table -> header tool tip")];
    [[fPeerTable tableColumnWithIdentifier: @"DL From"] setHeaderToolTip: NSLocalizedString(@"Downloading From Peer",
                                                                        "inspector -> peer table -> header tool tip")];
    
    [[fWebSeedTable tableColumnWithIdentifier: @"DL From"] setHeaderToolTip: NSLocalizedString(@"Downloading From Web Seed",
                                                                        "inspector -> web seed table -> header tool tip")];
    
    //prepare for animating peer table and web seed table
    NSRect webSeedTableFrame = [[fWebSeedTable enclosingScrollView] frame];
    fWebSeedTableHeight = webSeedTableFrame.size.height;
    fSpaceBetweenWebSeedAndPeer = webSeedTableFrame.origin.y - NSMaxY([[fPeerTable enclosingScrollView] frame]);
    
    [self setWebSeedTableHidden: YES animate: NO];
    
    //set blank inspector
    [self setInfoForTorrents: [NSArray array]];
    
    //allow for update notifications
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
    [nc addObserver: self selector: @selector(resetInfoForTorrent:) name: @"ResetInspector" object: nil];
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
    [fWebSeeds release];
    [fTrackers release];
    
    [fWebSeedTableAnimation release];
    
    [fTrackerCell release];
    
    [fPreviewPanel release];
    
    [super dealloc];
}

- (void) setInfoForTorrents: (NSArray *) torrents
{
    if (fTorrents && [fTorrents isEqualToArray: torrents])
        return;
    
    [fTorrents release];
    fTorrents = [torrents retain];
    
    [self resetInfo];
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
    
    NSInteger uploadUseSpeedLimit = [torrent usesSpeedLimit: YES] ? NSOnState : NSOffState,
                uploadSpeedLimit = [torrent speedLimit: YES],
                downloadUseSpeedLimit = [torrent usesSpeedLimit: NO] ? NSOnState : NSOffState,
                downloadSpeedLimit = [torrent speedLimit: NO],
                globalUseSpeedLimit = [torrent usesGlobalSpeedLimit] ? NSOnState : NSOffState;
    
    while ((torrent = [enumerator nextObject])
            && (uploadUseSpeedLimit != NSMixedState || uploadSpeedLimit != INVALID
                || downloadUseSpeedLimit != NSMixedState || downloadSpeedLimit != INVALID
                || globalUseSpeedLimit != NSMixedState))
    {
        if (uploadUseSpeedLimit != NSMixedState && uploadUseSpeedLimit != ([torrent usesSpeedLimit: YES] ? NSOnState : NSOffState))
            uploadUseSpeedLimit = NSMixedState;
        
        if (uploadSpeedLimit != INVALID && uploadSpeedLimit != [torrent speedLimit: YES])
            uploadSpeedLimit = INVALID;
        
        if (downloadUseSpeedLimit != NSMixedState && downloadUseSpeedLimit != ([torrent usesSpeedLimit: NO] ? NSOnState : NSOffState))
            downloadUseSpeedLimit = NSMixedState;
        
        if (downloadSpeedLimit != INVALID && downloadSpeedLimit != [torrent speedLimit: NO])
            downloadSpeedLimit = INVALID;
        
        if (globalUseSpeedLimit != NSMixedState && globalUseSpeedLimit != ([torrent usesGlobalSpeedLimit] ? NSOnState : NSOffState))
            globalUseSpeedLimit = NSMixedState;
    }
    
    //set upload view
    [fUploadLimitCheck setState: uploadUseSpeedLimit];
    [fUploadLimitCheck setEnabled: YES];
    
    [fUploadLimitLabel setEnabled: uploadUseSpeedLimit == NSOnState];
    [fUploadLimitField setEnabled: uploadUseSpeedLimit == NSOnState];
    if (uploadSpeedLimit != INVALID)
        [fUploadLimitField setIntValue: uploadSpeedLimit];
    else
        [fUploadLimitField setStringValue: @""];
    
    //set download view
    [fDownloadLimitCheck setState: downloadUseSpeedLimit];
    [fDownloadLimitCheck setEnabled: YES];
    
    [fDownloadLimitLabel setEnabled: downloadUseSpeedLimit == NSOnState];
    [fDownloadLimitField setEnabled: downloadUseSpeedLimit == NSOnState];
    if (downloadSpeedLimit != INVALID)
        [fDownloadLimitField setIntValue: downloadSpeedLimit];
    else
        [fDownloadLimitField setStringValue: @""];
    
    //set global check
    [fGlobalLimitCheck setState: globalUseSpeedLimit];
    [fGlobalLimitCheck setEnabled: YES];
    
    //get ratio info
    enumerator = [fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent
    
    NSInteger checkRatio = [torrent ratioSetting];
    CGFloat ratioLimit = [torrent ratioLimit];
    
    while ((torrent = [enumerator nextObject]) && (checkRatio != INVALID || ratioLimit != INVALID))
    {
        if (checkRatio != INVALID && checkRatio != [torrent ratioSetting])
            checkRatio = INVALID;
        
        if (ratioLimit != INVALID && ratioLimit != [torrent ratioLimit])
            ratioLimit = INVALID;
    }
    
    //set ratio view
    NSInteger index;
    if (checkRatio == TR_RATIOLIMIT_SINGLE)
        index = OPTION_POPUP_LIMIT;
    else if (checkRatio == TR_RATIOLIMIT_UNLIMITED)
        index = OPTION_POPUP_NO_LIMIT;
    else if (checkRatio == TR_RATIOLIMIT_GLOBAL)
        index = OPTION_POPUP_GLOBAL;
    else
        index = -1;
    [fRatioPopUp selectItemAtIndex: index];
    [fRatioPopUp setEnabled: YES];
    
    [fRatioLimitField setHidden: checkRatio != TR_RATIOLIMIT_SINGLE];
    if (ratioLimit != INVALID)
        [fRatioLimitField setFloatValue: ratioLimit];
    else
        [fRatioLimitField setStringValue: @""];
    
    //get priority info
    enumerator = [fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent
    
    NSInteger priority = [torrent priority];
    
    while ((torrent = [enumerator nextObject]) && priority != INVALID)
    {
        if (priority != INVALID && priority != [torrent priority])
            priority = INVALID;
    }
    
    //set priority view
    if (priority == TR_PRI_HIGH)
        index = OPTION_POPUP_PRIORITY_HIGH;
    else if (priority == TR_PRI_NORMAL)
        index = OPTION_POPUP_PRIORITY_NORMAL;
    else if (priority == TR_PRI_LOW)
        index = OPTION_POPUP_PRIORITY_LOW;
    else
        index = -1;
    [fPriorityPopUp selectItemAtIndex: index];
    [fPriorityPopUp setEnabled: YES];
    
    //get peer info
    enumerator = [fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent
    
    NSInteger maxPeers = [torrent maxPeerConnect];
    
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
    [fPeersConnectLabel setEnabled: YES];
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

- (void) animationDidEnd: (NSAnimation *) animation
{
    if (animation == fWebSeedTableAnimation)
    {
        [fWebSeedTableAnimation release];
        fWebSeedTableAnimation = nil;
    }
}

- (NSSize) windowWillResize: (NSWindow *) window toSize: (NSSize) proposedFrameSize
{
    //this is an edge-case - just stop the animation (stopAnimation jumps to end frame)
    if (fWebSeedTableAnimation)
    {
        [fWebSeedTableAnimation stopAnimation];
        [fWebSeedTableAnimation release];
        fWebSeedTableAnimation = nil;
    }
    
    return proposedFrameSize;
}

- (void) windowWillClose: (NSNotification *) notification
{
    if ([NSApp isOnSnowLeopardOrBetter] && fCurrentTabTag == TAB_FILES_TAG
        && ([QLPreviewPanelSL sharedPreviewPanelExists] && [[QLPreviewPanelSL sharedPreviewPanel] isVisible]))
        [[QLPreviewPanelSL sharedPreviewPanel] reloadData];
}

- (void) setTab: (id) sender
{
    const NSInteger oldTabTag = fCurrentTabTag;
    fCurrentTabTag = [fTabMatrix selectedTag];
    if (fCurrentTabTag == oldTabTag)
        return;
    
    [self updateInfoStats];
    
    //take care of old view
    CGFloat oldHeight = 0;
    NSString * oldResizeSaveKey = nil;
    if (oldTabTag != INVALID)
    {
        //deselect old tab item
        [(InfoTabButtonCell *)[fTabMatrix cellWithTag: oldTabTag] setSelectedTab: NO];
        
        switch (oldTabTag)
        {
            case TAB_ACTIVITY_TAG:
                [fPiecesView clearView];
                break;
            
            case TAB_TRACKER_TAG:
                oldResizeSaveKey = @"InspectorContentHeightTracker";
                break;
            
            case TAB_PEERS_TAG:
                //if in the middle of animating, just stop and resize immediately
                if (fWebSeedTableAnimation)
                    [self setWebSeedTableHidden: !fWebSeeds animate: NO];
                
                [fPeers release];
                fPeers = nil;
                [fWebSeeds release];
                fWebSeeds = nil;
                
                oldResizeSaveKey = @"InspectorContentHeightPeers";
                break;
            
            case TAB_FILES_TAG:
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
            break;
        case TAB_TRACKER_TAG:
            identifier = TAB_TRACKER_IDENT;
            title = NSLocalizedString(@"Trackers", "Inspector -> title");
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
            NSAssert1(NO, @"Unknown info tab selected: %d", fCurrentTabTag);
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
        CGFloat height = [[NSUserDefaults standardUserDefaults] floatForKey: resizeSaveKey];
        if (height != 0.0)
            viewRect.size.height = MAX(height, TAB_MIN_HEIGHT);
    }
    
    CGFloat difference = (viewRect.size.height - oldHeight) * [window userSpaceScaleFactor];
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
    
    if ([NSApp isOnSnowLeopardOrBetter] && (fCurrentTabTag == TAB_FILES_TAG || oldTabTag == TAB_FILES_TAG)
        && ([QLPreviewPanelSL sharedPreviewPanelExists] && [[QLPreviewPanelSL sharedPreviewPanel] isVisible]))
        [[QLPreviewPanelSL sharedPreviewPanel] reloadData];
}

- (void) setNextTab
{
    NSInteger tag = [fTabMatrix selectedTag]+1;
    if (tag >= [fTabMatrix numberOfColumns])
        tag = 0;
    
    [fTabMatrix selectCellWithTag: tag];
    [self setTab: nil];
}

- (void) setPreviousTab
{
    NSInteger tag = [fTabMatrix selectedTag]-1;
    if (tag < 0)
        tag = [fTabMatrix numberOfColumns]-1;
    
    [fTabMatrix selectCellWithTag: tag];
    [self setTab: nil];
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableView
{
    if (tableView == fPeerTable)
        return fPeers ? [fPeers count] : 0;
    else if (tableView == fWebSeedTable)
        return fWebSeeds ? [fWebSeeds count] : 0;
    else if (tableView == fTrackerTable)
        return fTrackers ? [fTrackers count] : 0;
    return 0;
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) column row: (NSInteger) row
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
    else if (tableView == fWebSeedTable)
    {
        NSString * ident = [column identifier];
        NSDictionary * webSeed = [fWebSeeds objectAtIndex: row];
        
        if ([ident isEqualToString: @"DL From"])
        {
            NSNumber * rate;
            return (rate = [webSeed objectForKey: @"DL From Rate"]) ? [NSString stringForSpeedAbbrev: [rate floatValue]] : @"";
        }
        else
            return [webSeed objectForKey: @"Address"];
    }
    else if (tableView == fTrackerTable)
    {
        id item = [fTrackers objectAtIndex: row];
        
        if ([item isKindOfClass: [NSNumber class]])
            return [NSString stringWithFormat: NSLocalizedString(@"Tier %d", "Inspector -> tracker table"), [item integerValue]];
        else
            return item;
    }
    return nil;
}

- (NSCell *) tableView: (NSTableView *) tableView dataCellForTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    if (tableView == fTrackerTable)
    {
        const BOOL tracker = [[fTrackers objectAtIndex: row] isKindOfClass: [TrackerNode class]];
        return tracker ? fTrackerCell : [tableColumn dataCellForRow: row];
    }
    
    return nil;
}

- (CGFloat) tableView: (NSTableView *) tableView heightOfRow: (NSInteger) row
{
    if (tableView == fTrackerTable)
    {
        if ([[fTrackers objectAtIndex: row] isKindOfClass: [NSNumber class]])
            return TRACKER_GROUP_SEPARATOR_HEIGHT;
    }
    
    return [tableView rowHeight];
}

- (void) tableView: (NSTableView *) tableView willDisplayCell: (id) cell forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    if (tableView == fPeerTable)
    {
        NSString * ident = [tableColumn identifier];
        
        if  ([ident isEqualToString: @"Progress"])
        {
            NSDictionary * peer = [fPeers objectAtIndex: row];
            [(PeerProgressIndicatorCell *)cell setSeed: [[peer objectForKey: @"Seed"] boolValue]];
        }
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
    else if (tableView == fWebSeedTable)
    {
        if (fWebSeeds)
        {
            NSArray * oldWebSeeds = fWebSeeds;
            fWebSeeds = [[fWebSeeds sortedArrayUsingDescriptors: [fWebSeedTable sortDescriptors]] retain];
            [oldWebSeeds release];
            [tableView reloadData];
        }
    }
    else;
}

- (BOOL) tableView: (NSTableView *) tableView shouldSelectRow: (NSInteger) row
{
    return tableView == fTrackerTable;
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    if ([notification object] == fTrackerTable)
    {
        NSInteger numSelected = [fTrackerTable numberOfSelectedRows];
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
                tableColumn: (NSTableColumn *) column row: (NSInteger) row mouseLocation: (NSPoint) mouseLocation
{
    if (tableView == fPeerTable)
    {
        NSDictionary * peer = [fPeers objectAtIndex: row];
        NSMutableArray * components = [NSMutableArray arrayWithCapacity: 5];
        
        CGFloat progress = [[peer objectForKey: @"Progress"] floatValue];
        NSString * progressString = [NSString localizedStringWithFormat: NSLocalizedString(@"Progress: %.1f%%",
                                        "Inspector -> Peers tab -> table row tooltip"), progress * 100.0];
        if (progress < 1.0 && [[peer objectForKey: @"Seed"] boolValue])
            progressString = [progressString stringByAppendingFormat: @" (%@)", NSLocalizedString(@"Partial Seed",
                                "Inspector -> Peers tab -> table row tooltip")];
        [components addObject: progressString];
        
        if ([[peer objectForKey: @"Encryption"] boolValue])
            [components addObject: NSLocalizedString(@"Encrypted Connection", "Inspector -> Peers tab -> table row tooltip")];
        
        NSString * portString;
        NSInteger port;
        if ((port = [[peer objectForKey: @"Port"] intValue]) > 0)
            portString = [NSString stringWithFormat: @"%d", port];
        else
            portString = NSLocalizedString(@"N/A", "Inspector -> Peers tab -> table row tooltip");
        [components addObject: [NSString stringWithFormat: @"%@: %@", NSLocalizedString(@"Port",
            "Inspector -> Peers tab -> table row tooltip"), portString]];
        
        const NSInteger peerFrom = [[peer objectForKey: @"From"] integerValue];
        switch (peerFrom)
        {
            case TR_PEER_FROM_TRACKER:
                [components addObject: NSLocalizedString(@"From: tracker", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_INCOMING:
                [components addObject: NSLocalizedString(@"From: incoming connection", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_RESUME:
                [components addObject: NSLocalizedString(@"From: cache", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_PEX:
                [components addObject: NSLocalizedString(@"From: peer exchange", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_DHT:
                [components addObject: NSLocalizedString(@"From: distributed hash table", "Inspector -> Peers tab -> table row tooltip")];
                break;
            case TR_PEER_FROM_LTEP:
                [components addObject: NSLocalizedString(@"From: libtorrent extension protocol handshake",
                                        "Inspector -> Peers tab -> table row tooltip")];
                break;
            default:
                NSAssert1(NO, @"Peer from unknown source: %d", peerFrom);
        }
        
        //determing status strings from flags
        NSMutableArray * statusArray = [NSMutableArray arrayWithCapacity: 6];
        NSString * flags = [peer objectForKey: @"Flags"];
        
        if ([flags rangeOfString: @"D"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Currently downloading (interested and not choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"d"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"You want to download, but peer does not want to send (interested and choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"U"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Currently uploading (interested and not choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"u"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Peer wants you to upload, but you do not want to (interested and choked)",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"K"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"Peer is unchoking you, but you are not interested",
                "Inspector -> peer -> status")];
        if ([flags rangeOfString: @"?"].location != NSNotFound)
            [statusArray addObject: NSLocalizedString(@"You unchoked the peer, but the peer is not interested",
                "Inspector -> peer -> status")];
        
        if ([statusArray count] > 0)
        {
            NSString * statusStrings = [statusArray componentsJoinedByString: @"\n\n"];
            [components addObject: [@"\n" stringByAppendingString: statusStrings]];
        }
        
        return [components componentsJoinedByString: @"\n"];
    }
    else if (tableView == fTrackerTable)
    {
        id node = [fTrackers objectAtIndex: row];
        if (![node isKindOfClass: [NSNumber class]])
            return [(TrackerNode *)node fullAnnounceAddress];
    }
    
    return nil;
}

- (void) tableView: (NSTableView *) tableView setObjectValue: (id) object forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    if (tableView != fTrackerTable)
        return;
    
    Torrent * torrent= [fTorrents objectAtIndex: 0];
    
    BOOL added = NO;
    for (NSString * tracker in [object componentsSeparatedByString: @"\n"])
        if ([torrent addTrackerToNewTier: tracker])
            added = YES;
    
    if (!added)
        NSBeep();
    
    //reset table with either new or old value
    [fTrackers release];
    fTrackers = [[torrent allTrackerStats] retain];
    
    [fTrackerTable setTrackers: fTrackers];
    [fTrackerTable reloadData];
    [fTrackerTable deselectAll: self];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil]; //incase sort by tracker
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

- (NSArray *) quickLookURLs
{
    FileOutlineView * fileOutlineView = [fFileController outlineView];
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSIndexSet * indexes = [fileOutlineView selectedRowIndexes];
    NSMutableArray * urlArray = [NSMutableArray arrayWithCapacity: [indexes count]];
    
    for (NSUInteger i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
    {
        FileListNode * item = [fileOutlineView itemAtRow: i];
        if ([self canQuickLookFile: item])
            [urlArray addObject: [NSURL fileURLWithPath: [torrent fileLocation: item]]];
    }
    
    return urlArray;
}

- (BOOL) canQuickLook
{
    if (fCurrentTabTag != TAB_FILES_TAG || ![[self window] isVisible] || [fTorrents count] != 1 || ![NSApp isOnSnowLeopardOrBetter])
        return NO;
    
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    if (![torrent isFolder])
        return NO;
    
    FileOutlineView * fileOutlineView = [fFileController outlineView];
    NSIndexSet * indexes = [fileOutlineView selectedRowIndexes];
    
    for (NSUInteger i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
        if ([self canQuickLookFile: [fileOutlineView itemAtRow: i]])
            return YES;
    
    return NO;
}

#warning uncomment (in header too)
- (NSRect) quickLookSourceFrameForPreviewItem: (id /*<QLPreviewItem>*/) item
{
    FileOutlineView * fileOutlineView = [fFileController outlineView];
    
    NSString * fullPath = [(NSURL *)item path];
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSRange visibleRows = [fileOutlineView rowsInRect: [fileOutlineView bounds]];
    
    for (NSUInteger row = visibleRows.location; row < NSMaxRange(visibleRows); row++)
    {
        FileListNode * rowItem = [fileOutlineView itemAtRow: row];
        if ([[torrent fileLocation: rowItem] isEqualToString: fullPath])
        {
            NSRect frame = [fileOutlineView iconRectForRow: row];
            
            if (!NSIntersectsRect([fileOutlineView visibleRect], frame))
                return NSZeroRect;
            
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
    [fPiecesView updateView];
}

- (void) revealDataFile: (id) sender
{
    if ([fTorrents count] > 0)
    {
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        NSString * location = [torrent dataLocation];
        if (!location)
            return;
        
        if ([NSApp isOnSnowLeopardOrBetter])
        {
            NSURL * file = [NSURL fileURLWithPath: location];
            [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs: [NSArray arrayWithObject: file]];
        }
        else
            [[NSWorkspace sharedWorkspace] selectFile: location inFileViewerRootedAtPath: nil];
    }
}

- (void) setFileFilterText: (id) sender
{
    [fFileController setFilterText: [sender stringValue]];
}

- (void) setUseSpeedLimit: (id) sender
{
    const BOOL upload = sender == fUploadLimitCheck;
    
    if ([sender state] == NSMixedState)
        [sender setState: NSOnState];
    const BOOL limit = [sender state] == NSOnState;
    
    for (Torrent * torrent in fTorrents)
        [torrent setUseSpeedLimit: limit upload: upload];
    
    NSTextField * field = upload ? fUploadLimitField : fDownloadLimitField;
    [field setEnabled: limit];
    if (limit)
    {
        [field selectText: self];
        [[self window] makeKeyAndOrderFront: self];
    }
    
    NSTextField * label = upload ? fUploadLimitLabel : fDownloadLimitLabel;
    [label setEnabled: limit];
}

- (void) setUseGlobalSpeedLimit: (id) sender
{
    if ([sender state] == NSMixedState)
        [sender setState: NSOnState];
    const BOOL limit = [sender state] == NSOnState;
    
    for (Torrent * torrent in fTorrents)
        [torrent setUseGlobalSpeedLimit: limit];
}

- (void) setSpeedLimit: (id) sender
{
    const BOOL upload = sender == fUploadLimitField;
    const NSInteger limit = [sender intValue];
    
    for (Torrent * torrent in fTorrents)
        [torrent setSpeedLimit: limit upload: upload];
}

- (void) setRatioSetting: (id) sender
{
    NSInteger setting;
    bool single = NO;
    switch ([sender indexOfSelectedItem])
    {
        case OPTION_POPUP_LIMIT:
            setting = TR_RATIOLIMIT_SINGLE;
            single = YES;
            break;
        case OPTION_POPUP_NO_LIMIT:
            setting = TR_RATIOLIMIT_UNLIMITED;
            break;
        case OPTION_POPUP_GLOBAL:
            setting = TR_RATIOLIMIT_GLOBAL;
            break;
        default:
            NSAssert1(NO, @"Unknown option selected in ratio popup: %d", [sender indexOfSelectedItem]);
            return;
    }
    
    for (Torrent * torrent in fTorrents)
        [torrent setRatioSetting: setting];
    
    [fRatioLimitField setHidden: !single];
    if (single)
    {
        [fRatioLimitField selectText: self];
        [[self window] makeKeyAndOrderFront: self];
    }
}

- (void) setRatioLimit: (id) sender
{
    CGFloat limit = [sender floatValue];
    
    for (Torrent * torrent in fTorrents)
        [torrent setRatioLimit: limit];
}

- (void) setPriority: (id) sender
{
    tr_priority_t priority;
    switch ([sender indexOfSelectedItem])
    {
        case OPTION_POPUP_PRIORITY_HIGH:
            priority = TR_PRI_HIGH;
            break;
        case OPTION_POPUP_PRIORITY_NORMAL:
            priority = TR_PRI_NORMAL;
            break;
        case OPTION_POPUP_PRIORITY_LOW:
            priority = TR_PRI_LOW;
            break;
        default:
            NSAssert1(NO, @"Unknown option selected in priority popup: %d", [sender indexOfSelectedItem]);
            return;
    }
    
    for (Torrent * torrent in fTorrents)
        [torrent setPriority: priority];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
}

- (void) setPeersConnectLimit: (id) sender
{
    NSInteger limit = [sender intValue];
    
    for (Torrent * torrent in fTorrents)
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

- (void) resetInfo
{
    const NSUInteger numberSelected = [fTorrents count];
    if (numberSelected != 1)
    {
        if (numberSelected > 0)
        {
            [fImageView setImage: [NSImage imageNamed: NSImageNameMultipleDocuments]];
            
            [fNameField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d Torrents Selected",
                                            "Inspector -> selected torrents"), numberSelected]];
        
            uint64_t size = 0;
            NSUInteger fileCount = 0, magnetCount = 0;
            for (Torrent * torrent in fTorrents)
            {
                size += [torrent size];
                fileCount += [torrent fileCount];
                if ([torrent isMagnet])
                    ++magnetCount;
            }
            
            NSMutableArray * fileStrings = [NSMutableArray arrayWithCapacity: 2];
            if (fileCount > 0)
            {
                NSString * fileString;
                if (fileCount == 1)
                    fileString = NSLocalizedString(@"1 file", "Inspector -> selected torrents");
                else
                    fileString = [NSString stringWithFormat: NSLocalizedString(@"%d files", "Inspector -> selected torrents"), fileCount];
                [fileStrings addObject: fileString];
            }
            if (magnetCount > 0)
            {
                NSString * magnetString;
                if (magnetCount == 1)
                    magnetString = NSLocalizedString(@"1 magnetized transfer", "Inspector -> selected torrents");
                else
                    magnetString = [NSString stringWithFormat: NSLocalizedString(@"%d magnetized transfers",
                                    "Inspector -> selected torrents"), magnetCount];
                [fileStrings addObject: magnetString];
            }
            
            NSString * fileString = [fileStrings componentsJoinedByString: @" + "];
            
            if (magnetCount < numberSelected)
            {
                [fBasicInfoField setStringValue: [NSString stringWithFormat: @"%@, %@", fileString,
                    [NSString stringWithFormat: NSLocalizedString(@"%@ total", "Inspector -> selected torrents"),
                        [NSString stringForFileSize: size]]]];
                [fBasicInfoField setToolTip: [NSString stringWithFormat: NSLocalizedString(@"%llu bytes", "Inspector -> selected torrents"),
                                                size]];
            }
            else
            {
                [fBasicInfoField setStringValue: fileString];
                [fBasicInfoField setToolTip: nil];
            }
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
            [fRatioField setStringValue: @""];
            
            //options fields
            [fUploadLimitCheck setEnabled: NO];
            [fUploadLimitCheck setState: NSOffState];
            [fUploadLimitField setEnabled: NO];
            [fUploadLimitLabel setEnabled: NO];
            [fUploadLimitField setStringValue: @""];
            
            [fDownloadLimitCheck setEnabled: NO];
            [fDownloadLimitCheck setState: NSOffState];
            [fDownloadLimitField setEnabled: NO];
            [fDownloadLimitLabel setEnabled: NO];
            [fDownloadLimitField setStringValue: @""];
            
            [fGlobalLimitCheck setEnabled: NO];
            [fGlobalLimitCheck setState: NSOffState];
            
            [fPriorityPopUp setEnabled: NO];
            [fPriorityPopUp selectItemAtIndex: -1];
            
            [fRatioPopUp setEnabled: NO];
            [fRatioPopUp selectItemAtIndex: -1];
            [fRatioLimitField setHidden: YES];
            [fRatioLimitField setStringValue: @""];
            
            [fPeersConnectField setEnabled: NO];
            [fPeersConnectField setStringValue: @""];
            [fPeersConnectLabel setEnabled: NO];
        }
        
        [fFileController setTorrent: nil];
        
        [fNameField setToolTip: nil];

        [fPiecesField setStringValue: @""];
        [fHashField setStringValue: @""];
        [fHashField setToolTip: nil];
        [fSecureField setStringValue: @""];
        [fCommentView setString: @""];
        
        [fCreatorField setStringValue: @""];
        [fDateCreatedField setStringValue: @""];
        
        [fDataLocationField setStringValue: @""];
        [fDataLocationField setToolTip: nil];
        
        [fRevealDataButton setHidden: YES];
        
        #warning remove after 1.8
        [fHashField setSelectable: NO];
        [fCreatorField setSelectable: NO];
        [fDataLocationField setSelectable: NO];
        
        [fStateField setStringValue: @""];
        [fProgressField setStringValue: @""];
        
        [fErrorMessageView setString: @""];
        
        [fConnectedPeersField setStringValue: @""];
        
        [fDateAddedField setStringValue: @""];
        [fDateCompletedField setStringValue: @""];
        
        [fPiecesControl setSelected: NO forSegment: PIECES_CONTROL_AVAILABLE];
        [fPiecesControl setSelected: NO forSegment: PIECES_CONTROL_PROGRESS];
        [fPiecesControl setEnabled: NO];
        [fPiecesView setTorrent: nil];
        
        [fPeers release];
        fPeers = nil;
        [fPeerTable reloadData];
        
        [fWebSeeds release];
        fWebSeeds = nil;
        [fWebSeedTable reloadData];
        [self setWebSeedTableHidden: YES animate: YES];
        
        [fTrackerTable setTorrent: nil];
        
        [fTrackers release];
        fTrackers = nil;
        
        [fTrackerTable setTrackers: fTrackers];
        [fTrackerTable reloadData];
        
        [fTrackerAddRemoveControl setEnabled: NO forSegment: TRACKER_ADD_TAG];
        [fTrackerAddRemoveControl setEnabled: NO forSegment: TRACKER_REMOVE_TAG];
        
        [fFileFilterField setEnabled: NO];
    }
    else
    {
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
        [fFileController setTorrent: torrent];
        
        if ([NSApp isOnSnowLeopardOrBetter])
            [fImageView setImage: [torrent icon]];
        else
        {
            NSImage * icon = [[torrent icon] copy];
            [icon setFlipped: NO];
            [fImageView setImage: icon];
            [icon release];
        }
        
        NSString * name = [torrent name];
        [fNameField setStringValue: name];
        [fNameField setToolTip: name];
        
        if (![torrent isMagnet])
        {
            NSString * basicString = [NSString stringForFileSize: [torrent size]];
            if ([torrent isFolder])
            {
                NSString * fileString;
                NSInteger fileCount = [torrent fileCount];
                if (fileCount == 1)
                    fileString = NSLocalizedString(@"1 file", "Inspector -> selected torrents");
                else
                    fileString= [NSString stringWithFormat: NSLocalizedString(@"%d files", "Inspector -> selected torrents"), fileCount];
                basicString = [NSString stringWithFormat: @"%@, %@", fileString, basicString];
            }
            [fBasicInfoField setStringValue: basicString];
            [fBasicInfoField setToolTip: [NSString stringWithFormat: NSLocalizedString(@"%llu bytes", "Inspector -> selected torrents"),
                                            [torrent size]]];
        }
        else
        {
            [fBasicInfoField setStringValue: NSLocalizedString(@"Magnetized transfer", "Inspector -> selected torrents")];
            [fBasicInfoField setToolTip: nil];
        }
        
        NSString * piecesString = ![torrent isMagnet] ? [NSString stringWithFormat: @"%d, %@", [torrent pieceCount],
                                        [NSString stringForFileSize: [torrent pieceSize]]] : @"";
        [fPiecesField setStringValue: piecesString];
                                        
        NSString * hashString = [torrent hashString];
        [fHashField setStringValue: hashString];
        [fHashField setToolTip: hashString];
        [fSecureField setStringValue: [torrent privateTorrent]
                        ? NSLocalizedString(@"Private Torrent, PEX and DHT automatically disabled", "Inspector -> private torrent")
                        : NSLocalizedString(@"Public Torrent", "Inspector -> private torrent")];
        
        NSString * commentString = [torrent comment];
        [fCommentView setString: commentString];
        
        NSString * creatorString = [torrent creator];
        [fCreatorField setStringValue: creatorString];
        [fDateCreatedField setObjectValue: [torrent dateCreated]];
        
        [fDateAddedField setObjectValue: [torrent dateAdded]];
        
        #warning remove after 1.8
        [fHashField setSelectable: YES];
        [fCommentView setSelectable: ![commentString isEqualToString: @""]];
        [fDataLocationField setSelectable: YES];
        
        //set pieces view
        BOOL piecesAvailableSegment = [[NSUserDefaults standardUserDefaults] boolForKey: @"PiecesViewShowAvailability"];
        [fPiecesControl setSelected: piecesAvailableSegment forSegment: PIECES_CONTROL_AVAILABLE];
        [fPiecesControl setSelected: !piecesAvailableSegment forSegment: PIECES_CONTROL_PROGRESS];
        [fPiecesControl setEnabled: YES];
        [fPiecesView setTorrent: torrent];
        
        //get webseeds for table - if no webseeds for this torrent, clear the table
        BOOL hasWebSeeds = [torrent webSeedCount] > 0;
        [self setWebSeedTableHidden: !hasWebSeeds animate: YES];
        if (!hasWebSeeds)
        {
            [fWebSeeds release];
            fWebSeeds = nil;
            [fWebSeedTable reloadData];
        }
        
        [fTrackerTable setTorrent: torrent];
        [fTrackerTable deselectAll: self];
        [fTrackerAddRemoveControl setEnabled: YES forSegment: TRACKER_ADD_TAG];
        [fTrackerAddRemoveControl setEnabled: NO forSegment: TRACKER_REMOVE_TAG];

        [fFileFilterField setEnabled: [torrent isFolder]];
    }
    
    [fFileFilterField setStringValue: @""];
    
    //update stats and settings
    [self updateInfoStats];
    [self updateOptions];
}

- (void) resetInfoForTorrent: (NSNotification *) notification
{
    if (fTorrents && [fTorrents containsObject: [notification object]])
        [self resetInfo];
}

- (void) updateInfoGeneral
{   
    if ([fTorrents count] != 1)
        return;
    
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    
    NSString * location = [torrent dataLocation];
    [fDataLocationField setStringValue: location ? [location stringByAbbreviatingWithTildeInPath] : @""];
    [fDataLocationField setToolTip: location ? location : @""];
    
    [fRevealDataButton setHidden: !location];
}

- (void) updateInfoActivity
{
    NSInteger numberSelected = [fTorrents count];
    if (numberSelected == 0)
        return;
    
    uint64_t have = 0, haveVerified = 0, downloadedTotal = 0, uploadedTotal = 0, failedHash = 0;
    NSDate * lastActivity = nil;
    for (Torrent * torrent in fTorrents)
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
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
        [fStateField setStringValue: [torrent stateString]];
        
        if ([torrent isFolder])
            [fProgressField setStringValue: [NSString localizedStringWithFormat: NSLocalizedString(@"%.2f%% (%.2f%% selected)",
                "Inspector -> Activity tab -> progress"), 100.0 * [torrent progress], 100.0 * [torrent progressDone]]];
        else
            [fProgressField setStringValue: [NSString localizedStringWithFormat: @"%.2f%%", 100.0 * [torrent progress]]];
            
        [fRatioField setStringValue: [NSString stringForRatio: [torrent ratio]]];
        
        NSString * errorMessage = [torrent errorMessage];
        if (![errorMessage isEqualToString: [fErrorMessageView string]])
            [fErrorMessageView setString: errorMessage];
        
        [fDateCompletedField setObjectValue: [torrent dateCompleted]];
        
        [fPiecesView updateView];
    }
    else if (numberSelected > 1)
    {
        [fRatioField setStringValue: [NSString stringForRatio: tr_getRatio(uploadedTotal, downloadedTotal)]];
    }
    else;
}

- (void) updateInfoTracker
{
    if ([fTorrents count] != 1)
        return;
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    
    //get updated tracker stats
    if ([fTrackerTable editedRow] == -1)
    {
        [fTrackers release];
        fTrackers = [[torrent allTrackerStats] retain];
        
        [fTrackerTable setTrackers: fTrackers];
        [fTrackerTable reloadData];
    }
    else
    {
        if ([NSApp isOnSnowLeopardOrBetter])
        {
            NSIndexSet * addedIndexes = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange([fTrackers count]-2, 2)];
            NSArray * tierAndTrackerBeingAdded = [fTrackers objectsAtIndexes: addedIndexes];
            
            [fTrackers release];
            fTrackers = [[torrent allTrackerStats] retain];
            [fTrackers addObjectsFromArray: tierAndTrackerBeingAdded];
            
            [fTrackerTable setTrackers: fTrackers];
            
            NSIndexSet * updateIndexes = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTrackers count]-2)],
                    * columnIndexes = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [[fTrackerTable tableColumns] count])];
            [fTrackerTable reloadDataForRowIndexes: updateIndexes columnIndexes: columnIndexes];
        }
    }
}

- (void) updateInfoPeers
{
    if ([fTorrents count] != 1)
        return;
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    
    NSString * knownString = [NSString stringWithFormat: NSLocalizedString(@"%d known", "Inspector -> Peers tab -> peers"),
                                [torrent totalPeersKnown]];
    if ([torrent isActive])
    {
        const NSInteger total = [torrent totalPeersConnected];
        NSString * connectedText = [NSString stringWithFormat: NSLocalizedString(@"%d Connected", "Inspector -> Peers tab -> peers"),
                                    total];
        
        if (total > 0)
        {
            NSMutableArray * fromComponents = [NSMutableArray arrayWithCapacity: 5];
            NSInteger count;
            if ((count = [torrent totalPeersTracker]) > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d tracker", "Inspector -> Peers tab -> peers"), count]];
            if ((count = [torrent totalPeersIncoming]) > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d incoming", "Inspector -> Peers tab -> peers"), count]];
            if ((count = [torrent totalPeersCache]) > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d cache", "Inspector -> Peers tab -> peers"), count]];
            if ((count = [torrent totalPeersPex]) > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d PEX", "Inspector -> Peers tab -> peers"), count]];
            if ((count = [torrent totalPeersDHT]) > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d DHT", "Inspector -> Peers tab -> peers"), count]];
            if ((count = [torrent totalPeersLTEP]) > 0)
                [fromComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"%d LTEP", "Inspector -> Peers tab -> peers"), count]];
            
            NSMutableArray * upDownComponents = [NSMutableArray arrayWithCapacity: 3];
            if ((count = [torrent peersSendingToUs]) > 0)
                [upDownComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"DL from %d", "Inspector -> Peers tab -> peers"), count]];
            if ((count = [torrent peersGettingFromUs]) > 0)
                [upDownComponents addObject: [NSString stringWithFormat:
                                        NSLocalizedString(@"UL to %d", "Inspector -> Peers tab -> peers"), count]];
            [upDownComponents addObject: knownString];
            
            connectedText = [connectedText stringByAppendingFormat: @": %@\n%@", [fromComponents componentsJoinedByString: @", "],
                                [upDownComponents componentsJoinedByString: @", "]];
        }
        else
            connectedText = [connectedText stringByAppendingFormat: @"\n%@", knownString];
        
        [fConnectedPeersField setStringValue: connectedText];
    }
    else
    {
        NSString * connectedText = [NSString stringWithFormat: @"%@\n%@",
                                    NSLocalizedString(@"Not Connected", "Inspector -> Peers tab -> peers"), knownString];
        [fConnectedPeersField setStringValue: connectedText];
    }
    
    [fPeers release];
    fPeers = [[[torrent peers] sortedArrayUsingDescriptors: [self peerSortDescriptors]] retain];
    [fPeerTable reloadData];
    
    if ([torrent webSeedCount] > 0)
    {
        [fWebSeeds release];
        fWebSeeds = [[[torrent webSeeds] sortedArrayUsingDescriptors: [fWebSeedTable sortDescriptors]] retain];
        [fWebSeedTable reloadData];
    }
}

- (void) updateInfoFiles
{
    if ([fTorrents count] == 1)
        [fFileController reloadData];
}

- (NSView *) tabViewForTag: (NSInteger) tag
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
            NSAssert1(NO, @"Unknown tab view for tag: %d", tag);
            return nil;
    }
}

- (void) setWebSeedTableHidden: (BOOL) hide animate: (BOOL) animate
{
    if (fCurrentTabTag != TAB_PEERS_TAG || ![[self window] isVisible])
        animate = NO;
    
    if (fWebSeedTableAnimation)
    {
        [fWebSeedTableAnimation stopAnimation];
        [fWebSeedTableAnimation release];
        fWebSeedTableAnimation = nil;
    }
    
    NSRect webSeedFrame = [[fWebSeedTable enclosingScrollView] frame];
    NSRect peerFrame = [[fPeerTable enclosingScrollView] frame];
    
    if (hide)
    {
        CGFloat webSeedFrameMaxY = NSMaxY(webSeedFrame);
        webSeedFrame.size.height = 0;
        webSeedFrame.origin.y = webSeedFrameMaxY;
        
        peerFrame.size.height = webSeedFrameMaxY - peerFrame.origin.y;
    }
    else
    {
        webSeedFrame.origin.y -= fWebSeedTableHeight - webSeedFrame.size.height;
        webSeedFrame.size.height = fWebSeedTableHeight;
        
        peerFrame.size.height = (webSeedFrame.origin.y - fSpaceBetweenWebSeedAndPeer) - peerFrame.origin.y;
    }
    
    [[fWebSeedTable enclosingScrollView] setHidden: NO]; //this is needed for some reason
    
    //actually resize tables
    if (animate)
    {
        NSDictionary * webSeedDict = [NSDictionary dictionaryWithObjectsAndKeys:
                                    [fWebSeedTable enclosingScrollView], NSViewAnimationTargetKey,
                                    [NSValue valueWithRect: [[fWebSeedTable enclosingScrollView] frame]], NSViewAnimationStartFrameKey,
                                    [NSValue valueWithRect: webSeedFrame], NSViewAnimationEndFrameKey, nil],
                    * peerDict = [NSDictionary dictionaryWithObjectsAndKeys:
                                    [fPeerTable enclosingScrollView], NSViewAnimationTargetKey,
                                    [NSValue valueWithRect: [[fPeerTable enclosingScrollView] frame]], NSViewAnimationStartFrameKey,
                                    [NSValue valueWithRect: peerFrame], NSViewAnimationEndFrameKey, nil];
        
        fWebSeedTableAnimation = [[NSViewAnimation alloc] initWithViewAnimations:
                                        [NSArray arrayWithObjects: webSeedDict, peerDict, nil]];
        [fWebSeedTableAnimation setDuration: 0.125];
        [fWebSeedTableAnimation setAnimationBlockingMode: NSAnimationNonblocking];
        [fWebSeedTableAnimation setDelegate: self];
        
        [fWebSeedTableAnimation startAnimation];
    }
    else
    {
        [[fWebSeedTable enclosingScrollView] setFrame: webSeedFrame];
        [[fPeerTable enclosingScrollView] setFrame: peerFrame];
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
                                                                        selector: @selector(compareNumeric:)];
        [descriptors addObject: secondDescriptor];
        [secondDescriptor release];
    }
    
    return descriptors;
}

- (BOOL) canQuickLookFile: (FileListNode *) item
{
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    return ([item isFolder] || [torrent fileProgress: item] >= 1.0) && [torrent fileLocation: item];
}

#warning doesn't like blank addresses
- (void) addTrackers
{
    [[self window] makeKeyWindow];
    
    [fTrackers addObject: [NSNumber numberWithInt: [(TrackerNode *)[fTrackers lastObject] tier]+1]];
    [fTrackers addObject: @""];
    
    [fTrackerTable setTrackers: fTrackers];
    [fTrackerTable reloadData];
    [fTrackerTable selectRowIndexes: [NSIndexSet indexSetWithIndex: [fTrackers count]-1] byExtendingSelection: NO];
    [fTrackerTable editColumn: [fTrackerTable columnWithIdentifier: @"Tracker"] row: [fTrackers count]-1 withEvent: nil select: YES];
}

- (void) removeTrackers
{
    NSMutableIndexSet * removeIndexes = [NSMutableIndexSet indexSet];
    
    NSIndexSet * selectedIndexes = [fTrackerTable selectedRowIndexes];
    NSLog(@"%@", fTrackers);
    NSLog(@"selected: %@", selectedIndexes);
    BOOL groupSelected = NO;
    for (NSUInteger i = 0, trackerIndex = 0; i < [fTrackers count]; ++i)
    {
        if ([[fTrackers objectAtIndex: i] isKindOfClass: [NSNumber class]])
        {
            groupSelected = [selectedIndexes containsIndex: i];
            if (!groupSelected && i > [selectedIndexes lastIndex])
                break;
        }
        else
        {
            if (groupSelected || [selectedIndexes containsIndex: i])
            {
                [removeIndexes addIndex: trackerIndex];
                NSLog(@"adding for remove %d (%d): %@", trackerIndex, i, [fTrackers objectAtIndex: i]);
            }
            ++trackerIndex;
        }
    }
    
    NSLog(@"%@", removeIndexes);
    
    NSAssert([removeIndexes count] > 0, @"Trying to remove no trackers.");
    
    if ([[NSUserDefaults standardUserDefaults] boolForKey: @"WarningRemoveTrackers"])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        
        if ([removeIndexes count] > 1)
        {
            [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"Are you sure you want to remove %d trackers?",
                                                                "Remove trackers alert -> title"), [removeIndexes count]]];
            [alert setInformativeText: NSLocalizedString(@"Once removed, Transmission will no longer attempt to contact them."
                                        " This cannot be undone.", "Remove trackers alert -> message")];
        }
        else
        {
            [alert setMessageText: NSLocalizedString(@"Are you sure you want to remove this tracker?", "Remove trackers alert -> title")];
            [alert setInformativeText: NSLocalizedString(@"Once removed, Transmission will no longer attempt to contact it."
                                        " This cannot be undone.", "Remove trackers alert -> message")];
        }
        
        [alert addButtonWithTitle: NSLocalizedString(@"Remove", "Remove trackers alert -> button")];
        [alert addButtonWithTitle: NSLocalizedString(@"Cancel", "Remove trackers alert -> button")];
        
        [alert setShowsSuppressionButton: YES];

        NSInteger result = [alert runModal];
        if ([[alert suppressionButton] state] == NSOnState)
            [[NSUserDefaults standardUserDefaults] setBool: NO forKey: @"WarningRemoveTrackers"];
        [alert release];
        
        if (result != NSAlertFirstButtonReturn)
            return;
    }
    
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    [torrent removeTrackersAtIndexes: removeIndexes];
    
    //reset table with either new or old value
    [fTrackers release];
    fTrackers = [[torrent allTrackerStats] retain];
    
    [fTrackerTable setTrackers: fTrackers];
    [fTrackerTable reloadData];
    [fTrackerTable deselectAll: self];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil]; //incase sort by tracker
}

@end
