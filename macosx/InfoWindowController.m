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
#import "InfoGeneralViewController.h"
#import "InfoActivityViewController.h"
#import "InfoTrackersViewController.h"
#import "InfoPeersViewController.h"
#import "InfoFileViewController.h"
#import "InfoOptionsViewController.h"
#import "InfoTabButtonCell.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

#define TAB_INFO_IDENT @"Info"
#define TAB_ACTIVITY_IDENT @"Activity"
#define TAB_TRACKER_IDENT @"Tracker"
#define TAB_PEERS_IDENT @"Peers"
#define TAB_FILES_IDENT @"Files"
#define TAB_OPTIONS_IDENT @"Options"

#define TAB_MIN_HEIGHT 250

#define INVALID -99

typedef enum
{
    TAB_GENERAL_TAG = 0,
    TAB_ACTIVITY_TAG = 1,
    TAB_TRACKERS_TAG = 2,
    TAB_PEERS_TAG = 3,
    TAB_FILE_TAG = 4,
    TAB_OPTIONS_TAG = 5
} tabTag;

@interface InfoWindowController (Private)

- (void) resetInfo;
- (void) resetInfoForTorrent: (NSNotification *) notification;

- (NSView *) tabViewForTag: (NSInteger) tag;

@end

@implementation InfoWindowController

- (id) init
{
    self = [super initWithWindowNibName: @"InfoWindow"];
    return self;
}

- (void) awakeFromNib
{
    //window location and size
    NSPanel * window = (NSPanel *)[self window];
    
    const CGFloat windowHeight = NSHeight([window frame]);
    
    #warning check if this is still needed
    [window setFrameAutosaveName: @"InspectorWindow"];
    [window setFrameUsingName: @"InspectorWindow"];
    
    NSRect windowRect = [window frame];
    windowRect.origin.y -= windowHeight - NSHeight(windowRect);
    windowRect.size.height = windowHeight;
    [window setFrame: windowRect display: NO];
    
    [window setBecomesKeyOnlyIfNeeded: YES];
    
    //set tab images
    [[fTabMatrix cellWithTag: TAB_GENERAL_TAG] setIcon: [NSImage imageNamed: @"InfoGeneral.png"]];
    [[fTabMatrix cellWithTag: TAB_ACTIVITY_TAG] setIcon: [NSImage imageNamed: @"InfoActivity.png"]];
    [[fTabMatrix cellWithTag: TAB_TRACKERS_TAG] setIcon: [NSImage imageNamed: @"InfoTracker.png"]];
    [[fTabMatrix cellWithTag: TAB_PEERS_TAG] setIcon: [NSImage imageNamed: @"InfoPeers.png"]];
    [[fTabMatrix cellWithTag: TAB_FILE_TAG] setIcon: [NSImage imageNamed: @"InfoFiles.png"]];
    [[fTabMatrix cellWithTag: TAB_OPTIONS_TAG] setIcon: [NSImage imageNamed: @"InfoOptions.png"]];
    
    //set tab tooltips
    [fTabMatrix setToolTip: NSLocalizedString(@"General Info", "Inspector -> tab") forCell: [fTabMatrix cellWithTag: TAB_GENERAL_TAG]];
    [fTabMatrix setToolTip: NSLocalizedString(@"Activity", "Inspector -> tab") forCell: [fTabMatrix cellWithTag: TAB_ACTIVITY_TAG]];
    [fTabMatrix setToolTip: NSLocalizedString(@"Trackers", "Inspector -> tab") forCell: [fTabMatrix cellWithTag: TAB_TRACKERS_TAG]];
    [fTabMatrix setToolTip: NSLocalizedString(@"Peers", "Inspector -> tab") forCell: [fTabMatrix cellWithTag: TAB_PEERS_TAG]];
    [fTabMatrix setToolTip: NSLocalizedString(@"Files", "Inspector -> tab") forCell: [fTabMatrix cellWithTag: TAB_FILE_TAG]];
    [fTabMatrix setToolTip: NSLocalizedString(@"Options", "Inspector -> tab") forCell: [fTabMatrix cellWithTag: TAB_OPTIONS_TAG]];
    
    //set selected tab
    fCurrentTabTag = INVALID;
    NSString * identifier = [[NSUserDefaults standardUserDefaults] stringForKey: @"InspectorSelected"];
    NSInteger tag;
    if ([identifier isEqualToString: TAB_INFO_IDENT])
        tag = TAB_GENERAL_TAG;
    else if ([identifier isEqualToString: TAB_ACTIVITY_IDENT])
        tag = TAB_ACTIVITY_TAG;
    else if ([identifier isEqualToString: TAB_TRACKER_IDENT])
        tag = TAB_TRACKERS_TAG;
    else if ([identifier isEqualToString: TAB_PEERS_IDENT])
        tag = TAB_PEERS_TAG;
    else if ([identifier isEqualToString: TAB_FILES_IDENT])
        tag = TAB_FILE_TAG;
    else if ([identifier isEqualToString: TAB_OPTIONS_IDENT])
        tag = TAB_OPTIONS_TAG;
    else //safety
    {
        [[NSUserDefaults standardUserDefaults] setObject: TAB_INFO_IDENT forKey: @"InspectorSelected"];
        tag = TAB_GENERAL_TAG;
    }
    [fTabMatrix selectCellWithTag: tag];
    [self setTab: nil];
    
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
        case TAB_TRACKERS_TAG:
            resizeSaveKey = @"InspectorContentHeightTracker";
            break;
        case TAB_PEERS_TAG:
            resizeSaveKey = @"InspectorContentHeightPeers";
            break;
        case TAB_FILE_TAG:
            resizeSaveKey = @"InspectorContentHeightFiles";
            break;
    }
    if (resizeSaveKey)
        [[NSUserDefaults standardUserDefaults] setFloat: [[self tabViewForTag: fCurrentTabTag] frame].size.height forKey: resizeSaveKey];
    
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fTorrents release];
    
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

#warning simplify?
- (void) updateInfoStats
{
    switch ([fTabMatrix selectedTag])
    {
        case TAB_GENERAL_TAG:
            [fGeneralViewController updateInfo];
            break;
        case TAB_ACTIVITY_TAG:
            [fActivityViewController updateInfo];
            break;
        case TAB_TRACKERS_TAG:
            [fTrackersViewController updateInfo];
            break;
        case TAB_PEERS_TAG:
            [fPeersViewController updateInfo];
            break;
        case TAB_FILE_TAG:
            [fFileViewController updateInfo];
            break;
        case TAB_OPTIONS_TAG:
            [fOptionsViewController updateInfo];
            break;
    }
}

- (NSRect) windowWillUseStandardFrame: (NSWindow *) window defaultFrame: (NSRect) defaultFrame
{
    NSRect windowRect = [window frame];
    windowRect.size.width = [window minSize].width;
    return windowRect;
}

- (NSSize) windowWillResize: (NSWindow *) window toSize: (NSSize) proposedFrameSize
{
    //this is an edge-case - just stop the animation
    [fPeersViewController stopWebSeedAnimation];
    
    return proposedFrameSize;
}

- (void) windowWillClose: (NSNotification *) notification
{
    if ([NSApp isOnSnowLeopardOrBetter] && fCurrentTabTag == TAB_FILE_TAG
        && ([QLPreviewPanelSL sharedPreviewPanelExists] && [[QLPreviewPanelSL sharedPreviewPanel] isVisible]))
        [[QLPreviewPanelSL sharedPreviewPanel] reloadData];
}

- (void) setTab: (id) sender
{
    const NSInteger oldTabTag = fCurrentTabTag;
    fCurrentTabTag = [fTabMatrix selectedTag];
    if (fCurrentTabTag == oldTabTag)
        return;
    
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
                [fActivityViewController clearPiecesView];
                break;
            
            case TAB_TRACKERS_TAG:
                [fTrackersViewController clearTrackers];
                
                oldResizeSaveKey = @"InspectorContentHeightTracker";
                break;
            
            case TAB_PEERS_TAG:
                [fPeersViewController clearPeers];
                
                oldResizeSaveKey = @"InspectorContentHeightPeers";
                break;
            
            case TAB_FILE_TAG:
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
    #warning get titles from view controller?
    NSString * resizeSaveKey = nil;
    NSString * identifier, * title;
    switch (fCurrentTabTag)
    {
        case TAB_GENERAL_TAG:
            if (!fGeneralViewController)
            {
                fGeneralViewController = [[InfoGeneralViewController alloc] init];
                [fGeneralViewController setInfoForTorrents: fTorrents];
            }
            
            identifier = TAB_INFO_IDENT;
            title = NSLocalizedString(@"General Info", "Inspector -> title");
            break;
        case TAB_ACTIVITY_TAG:
            if (!fActivityViewController)
            {
                fActivityViewController = [[InfoActivityViewController alloc] init];
                [fActivityViewController setInfoForTorrents: fTorrents];
            }
            
            identifier = TAB_ACTIVITY_IDENT;
            title = NSLocalizedString(@"Activity", "Inspector -> title");
            break;
        case TAB_TRACKERS_TAG:
            if (!fTrackersViewController)
            {
                fTrackersViewController = [[InfoTrackersViewController alloc] init];
                [fTrackersViewController setInfoForTorrents: fTorrents];
            }
            
            identifier = TAB_TRACKER_IDENT;
            title = NSLocalizedString(@"Trackers", "Inspector -> title");
            resizeSaveKey = @"InspectorContentHeightTracker";
            break;
        case TAB_PEERS_TAG:
            if (!fPeersViewController)
            {
                fPeersViewController = [[InfoPeersViewController alloc] init];
                [fPeersViewController setInfoForTorrents: fTorrents];
            }
            
            identifier = TAB_PEERS_IDENT;
            title = NSLocalizedString(@"Peers", "Inspector -> title");
            resizeSaveKey = @"InspectorContentHeightPeers";
            break;
        case TAB_FILE_TAG:
            if (!fFileViewController)
            {
                fFileViewController = [[InfoFileViewController alloc] init];
                [fFileViewController setInfoForTorrents: fTorrents];
            }
            
            identifier = TAB_FILES_IDENT;
            title = NSLocalizedString(@"Files", "Inspector -> title");
            resizeSaveKey = @"InspectorContentHeightFiles";
            break;
        case TAB_OPTIONS_TAG:
            if (!fOptionsViewController)
            {
                fOptionsViewController = [[InfoOptionsViewController alloc] init];
                [fOptionsViewController setInfoForTorrents: fTorrents];
            }
            
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
    
    NSView * view = [self tabViewForTag: fCurrentTabTag];
    
    [self updateInfoStats];
    
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
    
    if ([NSApp isOnSnowLeopardOrBetter] && (fCurrentTabTag == TAB_FILE_TAG || oldTabTag == TAB_FILE_TAG)
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

- (void) updateOptions
{
    [fOptionsViewController updateOptions];
}

- (NSArray *) quickLookURLs
{
    return [fFileViewController quickLookURLs];
}

- (BOOL) canQuickLook
{
    if (fCurrentTabTag != TAB_FILE_TAG || ![[self window] isVisible] || ![NSApp isOnSnowLeopardOrBetter])
        return NO;
    
    return [fFileViewController canQuickLook];
}

#warning uncomment (in header too)
- (NSRect) quickLookSourceFrameForPreviewItem: (id /*<QLPreviewItem>*/) item
{
    return [fFileViewController quickLookSourceFrameForPreviewItem: item];
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
                [fBasicInfoField setToolTip: [NSString stringWithFormat: NSLocalizedString(@"%llu bytes",
                                                "Inspector -> selected torrents"), size]];
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
        }
        
        [fNameField setToolTip: nil];
    }
    else
    {
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
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
                const NSInteger fileCount = [torrent fileCount];
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
    }
    
    [fGeneralViewController setInfoForTorrents: fTorrents];
    [fActivityViewController setInfoForTorrents: fTorrents];
    [fTrackersViewController setInfoForTorrents: fTorrents];
    [fPeersViewController setInfoForTorrents: fTorrents];
    [fFileViewController setInfoForTorrents: fTorrents];
    [fOptionsViewController setInfoForTorrents: fTorrents];
    
    [self updateInfoStats];
}

- (void) resetInfoForTorrent: (NSNotification *) notification
{
    if (fTorrents && [fTorrents containsObject: [notification object]])
        [self resetInfo];
}

#warning should we use the view controllers directly
- (NSView *) tabViewForTag: (NSInteger) tag
{
    switch (tag)
    {
        case TAB_GENERAL_TAG:
            return [fGeneralViewController view];
        case TAB_ACTIVITY_TAG:
            return [fActivityViewController view];
        case TAB_TRACKERS_TAG:
            return [fTrackersViewController view];
        case TAB_PEERS_TAG:
            return [fPeersViewController view];
        case TAB_FILE_TAG:
            return [fFileViewController view];
        case TAB_OPTIONS_TAG:
            return [fOptionsViewController view];
        default:
            NSAssert1(NO, @"Unknown tab view for tag: %d", tag);
            return nil;
    }
}

@end
