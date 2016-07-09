/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2012 Transmission authors and contributors
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
#import "InfoViewController.h"
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

@end

@implementation InfoWindowController

- (id) init
{
    self = [super initWithWindowNibName: @"InfoWindow"];
    return self;
}

- (void) awakeFromNib
{
    [fNoneSelectedField setStringValue: NSLocalizedString(@"No Torrents Selected", "Inspector -> selected torrents")];
    
    //window location and size
    NSPanel * window = (NSPanel *)[self window];
    
    [window setFloatingPanel: NO];
    
    const CGFloat windowHeight = NSHeight([window frame]);
    fMinWindowWidth = [window minSize].width;
    
    [window setFrameAutosaveName: @"InspectorWindow"];
    [window setFrameUsingName: @"InspectorWindow"];
    
    NSRect windowRect = [window frame];
    windowRect.origin.y -= windowHeight - NSHeight(windowRect);
    windowRect.size.height = windowHeight;
    [window setFrame: windowRect display: NO];
    
    [window setBecomesKeyOnlyIfNeeded: YES];
    
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
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    if ([fViewController respondsToSelector: @selector(saveViewSize)])
        [fViewController saveViewSize];
    
    [fGeneralViewController release];
    [fActivityViewController release];
    [fTrackersViewController release];
    [fPeersViewController release];
    [fFileViewController release];
    [fOptionsViewController release];
    
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

- (NSRect) windowWillUseStandardFrame: (NSWindow *) window defaultFrame: (NSRect) defaultFrame
{
    NSRect windowRect = [window frame];
    windowRect.size.width = [window minSize].width;
    return windowRect;
}

- (void) windowWillClose: (NSNotification *) notification
{
    if (fCurrentTabTag == TAB_FILE_TAG && ([QLPreviewPanel sharedPreviewPanelExists] && [[QLPreviewPanel sharedPreviewPanel] isVisible]))
        [[QLPreviewPanel sharedPreviewPanel] reloadData];
}

- (void) setTab: (id) sender
{
    const NSInteger oldTabTag = fCurrentTabTag;
    fCurrentTabTag = [fTabMatrix selectedTag];
    if (fCurrentTabTag == oldTabTag)
        return;
    
    //take care of old view
    CGFloat oldHeight = 0;
    if (oldTabTag != INVALID)
    {
        //deselect old tab item
        [(InfoTabButtonCell *)[fTabMatrix cellWithTag: oldTabTag] setSelectedTab: NO];
        
        if ([fViewController respondsToSelector: @selector(saveViewSize)])
            [fViewController saveViewSize];
        
        if ([fViewController respondsToSelector: @selector(clearView)])
            [fViewController clearView];
        
        NSView * oldView = [fViewController view];
        oldHeight = NSHeight([oldView frame]);
        
        //remove old view
        [oldView removeFromSuperview];
    }
    
    //set new tab item
    NSString * identifier;
    switch (fCurrentTabTag)
    {
        case TAB_GENERAL_TAG:
            if (!fGeneralViewController)
            {
                fGeneralViewController = [[InfoGeneralViewController alloc] init];
                [fGeneralViewController setInfoForTorrents: fTorrents];
            }
            
            fViewController = fGeneralViewController;
            identifier = TAB_INFO_IDENT;
            break;
        case TAB_ACTIVITY_TAG:
            if (!fActivityViewController)
            {
                fActivityViewController = [[InfoActivityViewController alloc] init];
                [fActivityViewController setInfoForTorrents: fTorrents];
            }
            
            fViewController = fActivityViewController;
            identifier = TAB_ACTIVITY_IDENT;
            break;
        case TAB_TRACKERS_TAG:
            if (!fTrackersViewController)
            {
                fTrackersViewController = [[InfoTrackersViewController alloc] init];
                [fTrackersViewController setInfoForTorrents: fTorrents];
            }
            
            fViewController = fTrackersViewController;
            identifier = TAB_TRACKER_IDENT;
            break;
        case TAB_PEERS_TAG:
            if (!fPeersViewController)
            {
                fPeersViewController = [[InfoPeersViewController alloc] init];
                [fPeersViewController setInfoForTorrents: fTorrents];
            }
            
            fViewController = fPeersViewController;
            identifier = TAB_PEERS_IDENT;
            break;
        case TAB_FILE_TAG:
            if (!fFileViewController)
            {
                fFileViewController = [[InfoFileViewController alloc] init];
                [fFileViewController setInfoForTorrents: fTorrents];
            }
            
            fViewController = fFileViewController;
            identifier = TAB_FILES_IDENT;
            break;
        case TAB_OPTIONS_TAG:
            if (!fOptionsViewController)
            {
                fOptionsViewController = [[InfoOptionsViewController alloc] init];
                [fOptionsViewController setInfoForTorrents: fTorrents];
            }
            
            fViewController = fOptionsViewController;
            identifier = TAB_OPTIONS_IDENT;
            break;
        default:
            NSAssert1(NO, @"Unknown info tab selected: %ld", fCurrentTabTag);
            return;
    }
    
    [[NSUserDefaults standardUserDefaults] setObject: identifier forKey: @"InspectorSelected"];
    
    NSWindow * window = [self window];
    
    [window setTitle: [NSString stringWithFormat: @"%@ - %@", [fViewController title],
                        NSLocalizedString(@"Torrent Inspector", "Inspector -> title")]];
    
    //selected tab item
    [(InfoTabButtonCell *)[fTabMatrix selectedCell] setSelectedTab: YES];
    
    NSView * view = [fViewController view];
    
    [fViewController updateInfo];
    
    NSRect windowRect = [window frame], viewRect = [view frame];
    
    const CGFloat difference = NSHeight(viewRect) - oldHeight;
    windowRect.origin.y -= difference;
    windowRect.size.height += difference;
    
    const CGFloat minWindowWidth = MAX(fMinWindowWidth, [view fittingSize].width);
    windowRect.size.width = MAX(NSWidth(windowRect), minWindowWidth);
    
    if ([fViewController respondsToSelector: @selector(saveViewSize)]) //a little bit hacky, but avoids requiring an extra method
    {
        if ([window screen])
        {
            const CGFloat screenHeight = NSHeight([[window screen] visibleFrame]);
            if (NSHeight(windowRect) > screenHeight)
            {
                const CGFloat difference = screenHeight - NSHeight(windowRect);
                windowRect.origin.y -= difference;
                windowRect.size.height += difference;
                
                viewRect.size.height += difference;
            }
        }
        
        [window setMinSize: NSMakeSize(minWindowWidth, NSHeight(windowRect) - NSHeight(viewRect) + TAB_MIN_HEIGHT)];
        [window setMaxSize: NSMakeSize(FLT_MAX, FLT_MAX)];
    }
    else
    {
        [window setMinSize: NSMakeSize(minWindowWidth, NSHeight(windowRect))];
        [window setMaxSize: NSMakeSize(FLT_MAX, NSHeight(windowRect))];
    }
    
    viewRect.size.width = NSWidth(windowRect);
    [view setFrame: viewRect];
    
    [window setFrame: windowRect display: YES animate: oldTabTag != INVALID];
    [[window contentView] addSubview: view];
    
    [[window contentView] addConstraints: [NSLayoutConstraint constraintsWithVisualFormat: @"H:|-0-[view]-0-|"
                                                                                  options: 0
                                                                                  metrics: nil
                                                                                    views: @{ @"view": view }]];
    [[window contentView] addConstraints: [NSLayoutConstraint constraintsWithVisualFormat: @"V:[tabs]-0-[view]-0-|"
                                                                                  options: 0
                                                                                  metrics: nil
                                                                                    views: @{ @"tabs": fTabMatrix, @"view": view }]];
    
    if ((fCurrentTabTag == TAB_FILE_TAG || oldTabTag == TAB_FILE_TAG)
        && ([QLPreviewPanel sharedPreviewPanelExists] && [[QLPreviewPanel sharedPreviewPanel] isVisible]))
        [[QLPreviewPanel sharedPreviewPanel] reloadData];
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

- (void) swipeWithEvent: (NSEvent *) event
{
    if ([event deltaX] < 0.0)
        [self setNextTab];
    else if ([event deltaX] > 0.0)
        [self setPreviousTab];
}

- (void) updateInfoStats
{
    [fViewController updateInfo];
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
    if (fCurrentTabTag != TAB_FILE_TAG || ![[self window] isVisible])
        return NO;
    
    return [fFileViewController canQuickLook];
}

- (NSRect) quickLookSourceFrameForPreviewItem: (id <QLPreviewItem>) item
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
            
            [fNameField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ Torrents Selected",
                                            "Inspector -> selected torrents"),
                                            [NSString formattedUInteger: numberSelected]]];
            [fNameField setHidden: NO];
        
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
                    fileString = [NSString stringWithFormat: NSLocalizedString(@"%@ files", "Inspector -> selected torrents"),
                                    [NSString formattedUInteger: fileCount]];
                [fileStrings addObject: fileString];
            }
            if (magnetCount > 0)
            {
                NSString * magnetString;
                if (magnetCount == 1)
                    magnetString = NSLocalizedString(@"1 magnetized transfer", "Inspector -> selected torrents");
                else
                    magnetString = [NSString stringWithFormat: NSLocalizedString(@"%@ magnetized transfers",
                                    "Inspector -> selected torrents"), [NSString formattedUInteger: magnetCount]];
                [fileStrings addObject: magnetString];
            }
            
            NSString * fileString = [fileStrings componentsJoinedByString: @" + "];
            
            if (magnetCount < numberSelected)
            {
                [fBasicInfoField setStringValue: [NSString stringWithFormat: @"%@, %@", fileString,
                    [NSString stringWithFormat: NSLocalizedString(@"%@ total", "Inspector -> selected torrents"),
                        [NSString stringForFileSize: size]]]];
                
                NSString * byteString;
                if ([NSApp isOnMountainLionOrBetter])
                {
                    NSByteCountFormatter * formatter = [[NSByteCountFormatterMtLion alloc] init];
                    [formatter setAllowedUnits: NSByteCountFormatterUseBytes];
                    byteString = [formatter stringFromByteCount: size];
                    [formatter release];
                }
                else
                    byteString = [NSString stringWithFormat: NSLocalizedString(@"%@ bytes", "Inspector -> selected torrents"), [NSString formattedUInteger: size]];
                [fBasicInfoField setToolTip: byteString];
            }
            else
            {
                [fBasicInfoField setStringValue: fileString];
                [fBasicInfoField setToolTip: nil];
            }
            [fBasicInfoField setHidden: NO];
            
            [fNoneSelectedField setHidden: YES];
        }
        else
        {
            [fImageView setImage: [NSImage imageNamed: NSImageNameApplicationIcon]];
            [fNoneSelectedField setHidden: NO];
            
            [fNameField setHidden: YES];
            [fBasicInfoField setHidden: YES];
        }
        
        [fNameField setToolTip: nil];
    }
    else
    {
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
        [fImageView setImage: [torrent icon]];
        
        NSString * name = [torrent name];
        [fNameField setStringValue: name];
        [fNameField setToolTip: name];
        [fNameField setHidden: NO];
        
        if (![torrent isMagnet])
        {
            NSString * basicString = [NSString stringForFileSize: [torrent size]];
            if ([torrent isFolder])
            {
                NSString * fileString;
                const NSUInteger fileCount = [torrent fileCount];
                if (fileCount == 1)
                    fileString = NSLocalizedString(@"1 file", "Inspector -> selected torrents");
                else
                    fileString= [NSString stringWithFormat: NSLocalizedString(@"%@ files", "Inspector -> selected torrents"),
                                    [NSString formattedUInteger: fileCount]];
                basicString = [NSString stringWithFormat: @"%@, %@", fileString, basicString];
            }
            [fBasicInfoField setStringValue: basicString];
            
            NSString * byteString;
            if ([NSApp isOnMountainLionOrBetter])
            {
                NSByteCountFormatter * formatter = [[NSByteCountFormatterMtLion alloc] init];
                [formatter setAllowedUnits: NSByteCountFormatterUseBytes];
                byteString = [formatter stringFromByteCount: [torrent size]];
                [formatter release];
            }
            else
                byteString = [NSString stringWithFormat: NSLocalizedString(@"%@ bytes", "Inspector -> selected torrents"), [NSString formattedUInteger: [torrent size]]];
            [fBasicInfoField setToolTip: byteString];
        }
        else
        {
            [fBasicInfoField setStringValue: NSLocalizedString(@"Magnetized transfer", "Inspector -> selected torrents")];
            [fBasicInfoField setToolTip: nil];
        }
        [fBasicInfoField setHidden: NO];
        
        [fNoneSelectedField setHidden: YES];
    }
    
    [fGeneralViewController setInfoForTorrents: fTorrents];
    [fActivityViewController setInfoForTorrents: fTorrents];
    [fTrackersViewController setInfoForTorrents: fTorrents];
    [fPeersViewController setInfoForTorrents: fTorrents];
    [fFileViewController setInfoForTorrents: fTorrents];
    [fOptionsViewController setInfoForTorrents: fTorrents];
    
    [fViewController updateInfo];
}

- (void) resetInfoForTorrent: (NSNotification *) notification
{
    Torrent * torrent = [[notification userInfo] objectForKey: @"Torrent"];
    if (fTorrents && (!torrent || [fTorrents containsObject: torrent]))
        [self resetInfo];
}

@end
