/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#import <IOKit/IOMessage.h>

#import "Controller.h"
#import "Torrent.h"
#import "TorrentCell.h"
#import "TorrentTableView.h"
#import "StringAdditions.h"

#import <Sparkle/Sparkle.h>

#define TOOLBAR_OPEN            @"Toolbar Open"
#define TOOLBAR_REMOVE          @"Toolbar Remove"
#define TOOLBAR_INFO            @"Toolbar Info"
#define TOOLBAR_PAUSE_ALL       @"Toolbar Pause All"
#define TOOLBAR_RESUME_ALL      @"Toolbar Resume All"
#define TOOLBAR_PAUSE_SELECTED  @"Toolbar Pause Selected"
#define TOOLBAR_RESUME_SELECTED @"Toolbar Resume Selected"
#define TOOLBAR_FILTER          @"Toolbar Toggle Filter"

#define GROWL_DOWNLOAD_COMPLETE @"Download Complete"
#define GROWL_SEEDING_COMPLETE  @"Seeding Complete"
#define GROWL_AUTO_ADD          @"Torrent Auto Added"
#define GROWL_AUTO_SPEED_LIMIT  @"Speed Limit Auto Changed"

#define TORRENT_TABLE_VIEW_DATA_TYPE    @"TorrentTableViewDataType"

#define ROW_HEIGHT_REGULAR      65.0
#define ROW_HEIGHT_SMALL        40.0
#define WINDOW_REGULAR_WIDTH    468.0

#define UPDATE_UI_SECONDS           1.0
#define AUTO_IMPORT_SECONDS         15.0
#define AUTO_SPEED_LIMIT_SECONDS    10.0

#define WEBSITE_URL @"http://transmission.m0k.org/"
#define FORUM_URL   @"http://transmission.m0k.org/forum/"

static void sleepCallBack(void * controller, io_service_t y, natural_t messageType, void * messageArgument)
{
    Controller * c = controller;
    [c sleepCallBack: messageType argument: messageArgument];
}

@implementation Controller

- (id) init
{
    if ((self = [super init]))
    {
        fLib = tr_init();
        
        fTorrents = [[NSMutableArray alloc] initWithCapacity: 10];
        fDisplayedTorrents = [[NSMutableArray alloc] initWithCapacity: 10];
        
        fDefaults = [NSUserDefaults standardUserDefaults];
        fInfoController = [[InfoWindowController alloc] initWithWindowNibName: @"InfoWindow"];
        fPrefsController = [[PrefsController alloc] initWithWindowNibName: @"PrefsWindow" handle: fLib];
        fBadger = [[Badger alloc] init];
        
        fAutoImportedNames = [[NSMutableArray alloc] init];
        
        [GrowlApplicationBridge setGrowlDelegate: self];
    }
    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];

    [fTorrents release];
    [fDisplayedTorrents release];
    
    [fToolbar release];
    [fInfoController release];
    [fPrefsController release];
    [fBadger release];
    
    [fSortType release];
    [fFilterType release];
    [fAutoImportedNames release];
    
    tr_close(fLib);
    [super dealloc];
}

- (void) awakeFromNib
{
    [fStatusBar setBackgroundImage: [NSImage imageNamed: @"StatusBarBackground.png"]];
    [fFilterBar setBackgroundImage: [NSImage imageNamed: @"FilterBarBackground.png"]];
    
    [fWindow setAcceptsMouseMovedEvents: YES]; //ensure filter buttons display correctly
    
    [fAdvancedBarItem setState: [fDefaults boolForKey: @"UseAdvancedBar"]];

    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Transmission Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: YES];
    [fToolbar setAutosavesConfiguration: YES];
    [fWindow setToolbar: fToolbar];
    [fWindow setDelegate: self];
    
    [fWindow makeFirstResponder: fTableView];
    [fWindow setExcludedFromWindowsMenu: YES];
    
    //set table size
    if ([fDefaults boolForKey: @"SmallView"])
    {
        [fTableView setRowHeight: ROW_HEIGHT_SMALL];
        [fSmallViewItem setState: NSOnState];
    }
    
    //window min height
    NSSize contentMinSize = [fWindow contentMinSize];
    contentMinSize.height = [[fWindow contentView] frame].size.height - [fScrollView frame].size.height
                                + [fTableView rowHeight] + [fTableView intercellSpacing].height;
    [fWindow setContentMinSize: contentMinSize];
    
    //set info keyboard shortcuts
    unichar ch = NSRightArrowFunctionKey;
    [fNextInfoTabItem setKeyEquivalent: [NSString stringWithCharacters: & ch length: 1]];
    ch = NSLeftArrowFunctionKey;
    [fPrevInfoTabItem setKeyEquivalent: [NSString stringWithCharacters: & ch length: 1]];
    
    //set up filter bar
    NSView * contentView = [fWindow contentView];
    [fFilterBar setHidden: YES];
    
    NSRect filterBarFrame = [fFilterBar frame];
    filterBarFrame.size.width = [fWindow frame].size.width;
    [fFilterBar setFrame: filterBarFrame];
    
    [contentView addSubview: fFilterBar];
    [fFilterBar setFrameOrigin: NSMakePoint(0, NSMaxY([contentView frame]))];
    
    [self showFilterBar: [fDefaults boolForKey: @"FilterBar"] animate: NO];
    
    //set up status bar
    [fStatusBar setHidden: YES];
    
    NSRect statusBarFrame = [fStatusBar frame];
    statusBarFrame.size.width = [fWindow frame].size.width;
    [fStatusBar setFrame: statusBarFrame];
    
    [contentView addSubview: fStatusBar];
    [fStatusBar setFrameOrigin: NSMakePoint(0, NSMaxY([contentView frame]))];
    [self showStatusBar: [fDefaults boolForKey: @"StatusBar"] animate: NO];
    
    //set speed limit
    fSpeedLimitNormalImage = [fSpeedLimitButton image];
    fSpeedLimitBlueImage = [NSImage imageNamed: @"SpeedLimitButtonBlue.png"];
    fSpeedLimitGraphiteImage = [NSImage imageNamed: @"SpeedLimitButtonGraphite.png"];
    
    [self updateControlTint: nil];
    
    if ((fSpeedLimitEnabled = [fDefaults boolForKey: @"SpeedLimit"]))
    {
        [fSpeedLimitItem setState: NSOnState];
        [fSpeedLimitDockItem setState: NSOnState];
        
        [fSpeedLimitButton setImage: [NSColor currentControlTint] == NSBlueControlTint
                                        ? fSpeedLimitBlueImage : fSpeedLimitGraphiteImage];
    }

    [fActionButton setToolTip: @"Shortcuts for changing global settings."];
    [fSpeedLimitButton setToolTip: @"Speed Limit overrides the total bandwidth limits with its own limits."];

    [fTableView setTorrents: fDisplayedTorrents];
    [[fTableView tableColumnWithIdentifier: @"Torrent"] setDataCell: [[TorrentCell alloc] init]];

    [fTableView registerForDraggedTypes: [NSArray arrayWithObjects: NSFilenamesPboardType,
                                                        TORRENT_TABLE_VIEW_DATA_TYPE, nil]];

    //register for sleep notifications
    IONotificationPortRef notify;
    io_object_t iterator;
    if (fRootPort = IORegisterForSystemPower(self, & notify, sleepCallBack, & iterator))
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notify),
                            kCFRunLoopCommonModes);
    else
        NSLog(@"Could not IORegisterForSystemPower");

    //load torrents from history
    Torrent * torrent;
    NSDictionary * historyItem;
    NSEnumerator * enumerator = [[fDefaults arrayForKey: @"History"] objectEnumerator];
    while ((historyItem = [enumerator nextObject]))
        if ((torrent = [[Torrent alloc] initWithHistory: historyItem lib: fLib]))
        {
            [fTorrents addObject: torrent];
            [torrent release];
        }
    
    //set sort
    fSortType = [[fDefaults stringForKey: @"Sort"] retain];
    
    NSMenuItem * currentSortItem, * currentSortActionItem;
    if ([fSortType isEqualToString: @"Name"])
    {
        currentSortItem = fNameSortItem;
        currentSortActionItem = fNameSortActionItem;
    }
    else if ([fSortType isEqualToString: @"State"])
    {
        currentSortItem = fStateSortItem;
        currentSortActionItem = fStateSortActionItem;
    }
    else if ([fSortType isEqualToString: @"Progress"])
    {
        currentSortItem = fProgressSortItem;
        currentSortActionItem = fProgressSortActionItem;
    }
    else if ([fSortType isEqualToString: @"Date"])
    {
        currentSortItem = fDateSortItem;
        currentSortActionItem = fDateSortActionItem;
    }
    else
    {
        currentSortItem = fOrderSortItem;
        currentSortActionItem = fOrderSortActionItem;
    }
    [currentSortItem setState: NSOnState];
    [currentSortActionItem setState: NSOnState];
    
    //set filter
    fFilterType = [[fDefaults stringForKey: @"Filter"] retain];

    BarButton * currentFilterButton;
    if ([fFilterType isEqualToString: @"Pause"])
        currentFilterButton = fPauseFilterButton;
    else if ([fFilterType isEqualToString: @"Seed"])
        currentFilterButton = fSeedFilterButton;
    else if ([fFilterType isEqualToString: @"Download"])
        currentFilterButton = fDownloadFilterButton;
    else
        currentFilterButton = fNoFilterButton;

    [currentFilterButton setEnabled: YES];
    
    //set upload limit action button
    [fUploadLimitItem setTitle: [NSString stringWithFormat: @"Limit (%d KB/s)",
                    [fDefaults integerForKey: @"UploadLimit"]]];
    if ([fDefaults boolForKey: @"CheckUpload"])
        [fUploadLimitItem setState: NSOnState];
    else
        [fUploadNoLimitItem setState: NSOnState];

	//set download limit action menu
    [fDownloadLimitItem setTitle: [NSString stringWithFormat: @"Limit (%d KB/s)",
                    [fDefaults integerForKey: @"DownloadLimit"]]];
    if ([fDefaults boolForKey: @"CheckDownload"])
        [fDownloadLimitItem setState: NSOnState];
    else
        [fDownloadNoLimitItem setState: NSOnState];
    
    //set ratio action menu
    [fRatioSetItem setTitle: [NSString stringWithFormat: @"Stop at Ratio (%.2f)",
                                [fDefaults floatForKey: @"RatioLimit"]]];
    if ([fDefaults boolForKey: @"RatioCheck"])
        [fRatioSetItem setState: NSOnState];
    else
        [fRatioNotSetItem setState: NSOnState];
    
    //observe notifications
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
    
    [nc addObserver: self selector: @selector(torrentFinishedDownloading:)
                    name: @"TorrentFinishedDownloading" object: nil];
    
    [nc addObserver: self selector: @selector(updateControlTint:)
                    name: NSControlTintDidChangeNotification object: nil];
    
    [nc addObserver: self selector: @selector(prepareForUpdate:)
                    name: SUUpdaterWillRestartNotification object: nil];
    fUpdateInProgress = NO;
    
    [nc addObserver: self selector: @selector(limitGlobalChange:)
                    name: @"LimitGlobalChange" object: nil];
    
    [nc addObserver: self selector: @selector(ratioGlobalChange:)
                    name: @"RatioGlobalChange" object: nil];
    
    [nc addObserver: self selector: @selector(autoSpeedLimitChange:)
                    name: @"AutoSpeedLimitChange" object: nil];
    
    [nc addObserver: self selector: @selector(autoImportChange:)
                    name: @"AutoImportSettingChange" object: nil];
    
    [nc addObserver: self selector: @selector(setWindowSizeToFit)
                    name: @"AutoSizeSettingChange" object: nil];
    
    //check to start another because of stopped torrent
    [nc addObserver: self selector: @selector(checkWaitingForStopped:)
                    name: @"StoppedDownloading" object: nil];
    
    //check all torrents for starting
    [nc addObserver: self selector: @selector(globalStartSettingChange:)
                    name: @"GlobalStartSettingChange" object: nil];

    //check if torrent should now start
    [nc addObserver: self selector: @selector(torrentStartSettingChange:)
                    name: @"TorrentStartSettingChange" object: nil];
    
    //check if torrent should now start
    [nc addObserver: self selector: @selector(torrentStoppedForRatio:)
                    name: @"TorrentStoppedForRatio" object: nil];
    
    //change that just impacts the inspector
    [nc addObserver: self selector: @selector(reloadInspectorSettings:)
                    name: @"TorrentSettingChange" object: nil];
    
    //change that just impacts the dock badge
    [nc addObserver: self selector: @selector(resetDockBadge:)
                    name: @"DockBadgeChange" object: nil];

    //timer to update the interface every second
    fCompleted = 0;
    [self updateUI: nil];
    fTimer = [NSTimer scheduledTimerWithTimeInterval: UPDATE_UI_SECONDS target: self
        selector: @selector(updateUI:) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSEventTrackingRunLoopMode];
    
    [self applyFilter: nil];
    
    [fWindow makeKeyAndOrderFront: nil];

    if ([fDefaults boolForKey: @"InfoVisible"])
        [self showInfo: nil];
    
    //timer to auto toggle speed limit
    [self autoSpeedLimitChange: nil];
    fSpeedLimitTimer = [NSTimer scheduledTimerWithTimeInterval: AUTO_SPEED_LIMIT_SECONDS target: self 
        selector: @selector(autoSpeedLimit:) userInfo: nil repeats: YES];
    
    //timer to check for auto import every 15 seconds, must do after everything else is set up
    fAutoImportTimer = [NSTimer scheduledTimerWithTimeInterval: AUTO_IMPORT_SECONDS target: self 
        selector: @selector(checkAutoImportDirectory:) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fAutoImportTimer forMode: NSDefaultRunLoopMode];
}

- (BOOL) applicationShouldHandleReopen: (NSApplication *) app hasVisibleWindows: (BOOL) visibleWindows
{
    if (![fWindow isVisible] && ![[fPrefsController window] isVisible])
        [fWindow makeKeyAndOrderFront: nil];
    return NO;
}

- (NSApplicationTerminateReply) applicationShouldTerminate: (NSApplication *) sender
{
    if (!fUpdateInProgress && [fDefaults boolForKey: @"CheckQuit"])
    {
        int active = 0, downloading = 0;
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive])
            {
                active++;
                if (![torrent isSeeding])
                    downloading++;
            }

        if ([fDefaults boolForKey: @"CheckQuitDownloading"] ? downloading > 0 : active > 0)
        {
            NSString * message = active == 1
                ? @"There is an active transfer. Do you really want to quit?"
                : [NSString stringWithFormat:
                    @"There are %d active transfers. Do you really want to quit?", active];

            NSBeginAlertSheet(@"Confirm Quit", @"Quit", @"Cancel", nil, fWindow, self,
                                @selector(quitSheetDidEnd:returnCode:contextInfo:), nil, nil, message);
            return NSTerminateLater;
        }
    }

    return NSTerminateNow;
}

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode contextInfo: (void *) contextInfo
{
    [NSApp replyToApplicationShouldTerminate: returnCode == NSAlertDefaultReturn];
}

- (void) applicationWillTerminate: (NSNotification *) notification
{
    //stop timers
    [fAutoImportTimer invalidate];
    [fSpeedLimitTimer invalidate];
    [fTimer invalidate];
    
    //save history and stop running torrents
    [self updateTorrentHistory];
    [fTorrents makeObjectsPerformSelector: @selector(stopTransferForQuit)];
    
    //remember window states and close all windows
    [fDefaults setBool: [[fInfoController window] isVisible] forKey: @"InfoVisible"];
    [[NSApp windows] makeObjectsPerformSelector: @selector(close)];
    [self showStatusBar: NO animate: NO];
    [self showFilterBar: NO animate: NO];
    
    //clear badge
    [fBadger clearBadge];

    //end quickly if the app is updating
    if (fUpdateInProgress)
        return;

    //wait for running transfers to stop (5 second timeout)
    NSDate * start = [NSDate date];
    BOOL timeUp = NO;
    
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while (!timeUp && (torrent = [enumerator nextObject]))
        while (![torrent isPaused] && !(timeUp = [start timeIntervalSinceNow] < -5.0))
        {
            usleep(100000);
            [torrent update];
        }
}

- (NSArray *) torrentsAtIndexes: (NSIndexSet *) indexSet
{
    if ([fDisplayedTorrents respondsToSelector: @selector(objectsAtIndexes:)])
        return [fDisplayedTorrents objectsAtIndexes: indexSet];
    else
    {
        NSMutableArray * torrents = [NSMutableArray arrayWithCapacity: [indexSet count]];
        unsigned int i;
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            [torrents addObject: [fDisplayedTorrents objectAtIndex: i]];

        return torrents;
    }
}

- (void) application: (NSApplication *) sender openFiles: (NSArray *) filenames
{
    [self openFiles: filenames ignoreDownloadFolder: NO];
}

- (void) openFiles: (NSArray *) filenames ignoreDownloadFolder: (BOOL) ignore
{
    NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"], * torrentPath;
    
    if (ignore || [downloadChoice isEqualToString: @"Ask"])
    {
        [self openFilesAsk: [filenames mutableCopy]];
        return;
    }
    
    Torrent * torrent;
    NSEnumerator * enumerator = [filenames objectEnumerator];
    while ((torrentPath = [enumerator nextObject]))
    {
        if (!(torrent = [[Torrent alloc] initWithPath: torrentPath lib: fLib]))
            continue;

        //add it to the "File > Open Recent" menu
        [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL: [NSURL fileURLWithPath: torrentPath]];

        NSString * folder = [downloadChoice isEqualToString: @"Constant"]
            ? [[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath]
            : [torrentPath stringByDeletingLastPathComponent];
        
        [torrent setDownloadFolder: folder];
        [torrent update];
        [self attemptToStartAuto: torrent];
        
        [fTorrents addObject: torrent];
        [torrent release];
    }

    [self updateUI: nil];
    [self applyFilter: nil];
    
    [self updateTorrentHistory];
}

//called by the main open method to show sheet for choosing download location
- (void) openFilesAsk: (NSMutableArray *) files
{
    NSString * torrentPath;
    Torrent * torrent;
    do
    {
        if ([files count] == 0) //recursive base case
        {
            [files release];
            
            [self updateTorrentHistory];
            return;
        }
    
        torrentPath = [files objectAtIndex: 0];
        torrent = [[Torrent alloc] initWithPath: torrentPath lib: fLib];
        
        [files removeObjectAtIndex: 0];
    } while (!torrent);

    //add it to the "File > Open Recent" menu
    [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL: [NSURL fileURLWithPath: torrentPath]];

    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: @"Select"];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];

    [panel setMessage: [NSString stringWithFormat: @"Select the download folder for \"%@\"", [torrent name]]];
    
    NSDictionary * dictionary = [[NSDictionary alloc] initWithObjectsAndKeys:
                                    torrent, @"Torrent", files, @"Files", nil];

    [panel beginSheetForDirectory: nil file: nil types: nil modalForWindow: fWindow modalDelegate: self
            didEndSelector: @selector(folderChoiceClosed:returnCode:contextInfo:) contextInfo: dictionary];
}

- (void) folderChoiceClosed: (NSOpenPanel *) openPanel returnCode: (int) code
    contextInfo: (NSDictionary *) dictionary
{
    Torrent * torrent = [dictionary objectForKey: @"Torrent"];

    if (code == NSOKButton)
    {
        [torrent setDownloadFolder: [[openPanel filenames] objectAtIndex: 0]];
        [torrent update];
        [self attemptToStartAuto: torrent];
        
        [fTorrents addObject: torrent];
        
        [self updateUI: nil];
        [self applyFilter: nil];
    }
    
    [self performSelectorOnMainThread: @selector(openFilesAsk:) withObject: [dictionary objectForKey: @"Files"]
                        waitUntilDone: NO];
    
    [torrent release];
    [dictionary release];
}

//called on by applescript
- (void) open: (NSArray *) files
{
    [self performSelectorOnMainThread: @selector(openFiles:) withObject: files waitUntilDone: NO];
}

- (void) openFiles: (NSArray *) filenames
{
    [self openFiles: filenames ignoreDownloadFolder: NO];
}

- (void) openShowSheet: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setAllowsMultipleSelection: YES];
    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: NO];

    [panel beginSheetForDirectory: nil file: nil types: [NSArray arrayWithObject: @"torrent"]
        modalForWindow: fWindow modalDelegate: self didEndSelector: @selector(openSheetClosed:returnCode:contextInfo:)
        contextInfo: [NSNumber numberWithBool: sender == fOpenIgnoreDownloadFolder]];
}

- (void) openSheetClosed: (NSOpenPanel *) panel returnCode: (int) code contextInfo: (NSNumber *) ignore
{
    if (code == NSOKButton)
    {
        NSDictionary * dictionary = [[NSDictionary alloc] initWithObjectsAndKeys:
                                        [panel filenames], @"Files", ignore, @"Ignore", nil];
        [self performSelectorOnMainThread: @selector(openFromSheet:) withObject: dictionary waitUntilDone: NO];
    }
}

- (void) openFromSheet: (NSDictionary *) dictionary
{
    [self openFiles: [dictionary objectForKey: @"Files"]
        ignoreDownloadFolder: [[dictionary objectForKey: @"Ignore"] boolValue]];
    
    [dictionary release];
}

- (void) resumeSelectedTorrents: (id) sender
{
    [self resumeTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]];
}

- (void) resumeAllTorrents: (id) sender
{
    [self resumeTorrents: fTorrents];
}

- (void) resumeWaitingTorrents: (id) sender
{
    NSMutableArray * torrents = [NSMutableArray arrayWithCapacity: [fTorrents count]];
    
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        if ([torrent waitingToStart])
            [torrents addObject: torrent];
    
    [self resumeTorrents: torrents];
}

- (void) resumeTorrents: (NSArray *) torrents
{
    [torrents makeObjectsPerformSelector: @selector(startTransfer)];
    
    [self updateUI: nil];
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    [self updateTorrentHistory];
}

- (void) stopSelectedTorrents: (id) sender
{
    [self stopTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]];
}

- (void) stopAllTorrents: (id) sender
{
    [self stopTorrents: fTorrents];
}

- (void) stopTorrents: (NSArray *) torrents
{
    //don't want any of these starting then stopping
    NSEnumerator * enumerator = [torrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent setWaitToStart: NO];

    [torrents makeObjectsPerformSelector: @selector(stopTransfer)];
    
    [self updateUI: nil];
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    [self updateTorrentHistory];
}

- (void) removeTorrents: (NSArray *) torrents
        deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent
{
    [torrents retain];
    int active = 0, downloading = 0;

    if ([fDefaults boolForKey: @"CheckRemove"])
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [torrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive])
            {
                active++;
                if (![torrent isSeeding])
                    downloading++;
            }

        if ([fDefaults boolForKey: @"CheckRemoveDownloading"] ? downloading > 0 : active > 0)
        {
            NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys:
                torrents, @"Torrents",
                [NSNumber numberWithBool: deleteData], @"DeleteData",
                [NSNumber numberWithBool: deleteTorrent], @"DeleteTorrent", nil];

            NSString * title, * message;
            
            int selected = [fTableView numberOfSelectedRows];
            if (selected == 1)
            {
                title = [NSString stringWithFormat: @"Comfirm Removal of \"%@\"",
                            [[fDisplayedTorrents objectAtIndex: [fTableView selectedRow]] name]];
                message = @"This transfer is active."
                            " Once removed, continuing the transfer will require the torrent file."
                            " Do you really want to remove it?";
            }
            else
            {
                title = [NSString stringWithFormat: @"Comfirm Removal of %d Transfers", selected];
                if (selected == active)
                    message = [NSString stringWithFormat: @"There are %d active transfers.", active];
                else
                    message = [NSString stringWithFormat: @"There are %d transfers (%d active).", selected, active];
                message = [message stringByAppendingString:
                    @" Once removed, continuing the transfers will require the torrent files."
                    " Do you really want to remove them?"];
            }

            NSBeginAlertSheet(title, @"Remove", @"Cancel", nil, fWindow, self,
                nil, @selector(removeSheetDidEnd:returnCode:contextInfo:), dict, message);
            return;
        }
    }
    
    [self confirmRemoveTorrents: torrents deleteData: deleteData deleteTorrent: deleteTorrent];
}

- (void) removeSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode contextInfo: (NSDictionary *) dict
{
    NSArray * torrents = [dict objectForKey: @"Torrents"];
    BOOL deleteData = [[dict objectForKey: @"DeleteData"] boolValue],
        deleteTorrent = [[dict objectForKey: @"DeleteTorrent"] boolValue];
    [dict release];
    
    if (returnCode == NSAlertDefaultReturn)
        [self confirmRemoveTorrents: torrents deleteData: deleteData deleteTorrent: deleteTorrent];
    else
        [torrents release];
}

- (void) confirmRemoveTorrents: (NSArray *) torrents deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent
{
    //don't want any of these starting then stopping
    NSEnumerator * enumerator = [torrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent setWaitToStart: NO];

    NSNumber * lowestOrderValue = [NSNumber numberWithInt: [torrents count]], * currentOrderValue;

    enumerator = [torrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        [torrent stopTransfer];

        if (deleteData)
            [torrent trashData];
        if (deleteTorrent)
            [torrent trashTorrent];
        
        //determine lowest order value
        currentOrderValue = [torrent orderValue];
        if ([lowestOrderValue compare: currentOrderValue] == NSOrderedDescending)
            lowestOrderValue = currentOrderValue;

        [torrent removeForever];
        
        [fTorrents removeObject: torrent];
        [fDisplayedTorrents removeObject: torrent];
    }
    [torrents release];

    //reset the order values if necessary
    if ([lowestOrderValue intValue] < [fTorrents count])
    {
        NSSortDescriptor * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"orderValue" ascending: YES] autorelease];
        NSArray * descriptors = [[NSArray alloc] initWithObjects: orderDescriptor, nil];

        NSArray * tempTorrents = [fTorrents sortedArrayUsingDescriptors: descriptors];
        [descriptors release];

        int i;
        for (i = [lowestOrderValue intValue]; i < [tempTorrents count]; i++)
            [[tempTorrents objectAtIndex: i] setOrderValue: i];
    }
    
    [fTableView deselectAll: nil];
    
    [self updateUI: nil];
    [self applyFilter: nil];
    
    [self updateTorrentHistory];
}

- (void) removeNoDelete: (id) sender
{
    [self removeTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]
                deleteData: NO deleteTorrent: NO];
}

- (void) removeDeleteData: (id) sender
{
    [self removeTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]
                deleteData: YES deleteTorrent: NO];
}

- (void) removeDeleteTorrent: (id) sender
{
    [self removeTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]
                deleteData: NO deleteTorrent: YES];
}

- (void) removeDeleteDataAndTorrent: (id) sender
{
    [self removeTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]
                deleteData: YES deleteTorrent: YES];
}

- (void) copyTorrentFile: (id) sender
{
    [self copyTorrentFileForTorrents: [[NSMutableArray alloc] initWithArray:
            [self torrentsAtIndexes: [fTableView selectedRowIndexes]]]];
}

- (void) copyTorrentFileForTorrents: (NSMutableArray *) torrents
{
    if ([torrents count] == 0)
    {
        [torrents release];
        return;
    }

    Torrent * torrent = [torrents objectAtIndex: 0];

    //warn user if torrent file can't be found
    if (![[NSFileManager defaultManager] fileExistsAtPath: [torrent torrentLocation]])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle: @"OK"];
        [alert setMessageText: [NSString stringWithFormat: 
                @"Copy of \"%@\" Cannot Be Created", [torrent name]]];
        [alert setInformativeText: [NSString stringWithFormat: 
                @"The torrent file (%@) cannot be found.", [torrent torrentLocation]]];
        [alert setAlertStyle: NSWarningAlertStyle];
        
        [alert runModal];
        
        [torrents removeObjectAtIndex: 0];
        [self copyTorrentFileForTorrents: torrents];
    }
    else
    {
        NSSavePanel * panel = [NSSavePanel savePanel];
        [panel setRequiredFileType: @"torrent"];
        [panel setCanSelectHiddenExtension: YES];
        
        [panel beginSheetForDirectory: nil file: [torrent name]
            modalForWindow: fWindow modalDelegate: self didEndSelector:
            @selector(saveTorrentCopySheetClosed:returnCode:contextInfo:) contextInfo: torrents];
    }
}

- (void) saveTorrentCopySheetClosed: (NSSavePanel *) panel returnCode: (int) code
    contextInfo: (NSMutableArray *) torrents
{
    //if save successful, copy torrent to new location with name of data file
    if (code == NSOKButton)
        [[NSFileManager defaultManager] copyPath: [[torrents objectAtIndex: 0] torrentLocation]
            toPath: [panel filename] handler: nil];
    
    [torrents removeObjectAtIndex: 0];
    [self performSelectorOnMainThread: @selector(copyTorrentFileForTorrents:)
                withObject: torrents waitUntilDone: NO];
}

- (void) revealFile: (id) sender
{
    NSIndexSet * indexSet = [fTableView selectedRowIndexes];
    unsigned int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [[fDisplayedTorrents objectAtIndex: i] revealData];
}

- (void) showPreferenceWindow: (id) sender
{
    NSWindow * window = [fPrefsController window];
    if (![window isVisible])
        [window center];

    [window makeKeyAndOrderFront: nil];
}

- (void) showInfo: (id) sender
{
    if ([[fInfoController window] isVisible])
        [fInfoController close];
    else
    {
        [fInfoController updateInfoStats];
        [[fInfoController window] orderFront: nil];
    }
}

- (void) setInfoTab: (id) sender
{
    if (sender == fNextInfoTabItem)
        [fInfoController setNextTab];
    else
        [fInfoController setPreviousTab];
}

- (void) updateControlTint: (NSNotification *) notification
{
    if (fSpeedLimitEnabled)
        [fSpeedLimitButton setImage: [NSColor currentControlTint] == NSBlueControlTint
                                        ? fSpeedLimitBlueImage : fSpeedLimitGraphiteImage];
}

- (void) updateUI: (NSTimer *) t
{
    [fTorrents makeObjectsPerformSelector: @selector(update)];

    //resort if necessary or just update the table
    if ([fSortType isEqualToString: @"Progress"] || [fSortType isEqualToString: @"State"])
        [self sortTorrents];
    else
        [fTableView reloadData];
    
    //update the global DL/UL rates
    float downloadRate, uploadRate;
    tr_torrentRates(fLib, & downloadRate, & uploadRate);
    if (![fStatusBar isHidden])
    {
        [fTotalDLField setStringValue: [@"Total DL: " stringByAppendingString: [NSString stringForSpeed: downloadRate]]];
        [fTotalULField setStringValue: [@"Total UL: " stringByAppendingString: [NSString stringForSpeed: uploadRate]]];
    }

    //update non-constant parts of info window
    if ([[fInfoController window] isVisible])
        [fInfoController updateInfoStats];

    //badge dock
    [fBadger updateBadgeWithCompleted: fCompleted uploadRate: uploadRate downloadRate: downloadRate];
}

- (void) torrentFinishedDownloading: (NSNotification *) notification
{
    Torrent * torrent = [notification object];

    [self applyFilter: nil];
    [self checkToStartWaiting: torrent];

    if ([fDefaults boolForKey: @"PlayDownloadSound"])
    {
        NSSound * sound;
        if ((sound = [NSSound soundNamed: [fDefaults stringForKey: @"DownloadSound"]]))
            [sound play];
    }

    [GrowlApplicationBridge notifyWithTitle: @"Download Complete" description: [torrent name]
        notificationName: GROWL_DOWNLOAD_COMPLETE iconData: nil priority: 0 isSticky: NO clickContext: nil];

    if (![fWindow isKeyWindow])
        fCompleted++;
}

- (void) updateTorrentHistory
{
    NSMutableArray * history = [NSMutableArray arrayWithCapacity: [fTorrents count]];

    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [history addObject: [torrent history]];

    [fDefaults setObject: history forKey: @"History"];
    [fDefaults synchronize];
}

- (void) sortTorrents
{
    //remember selected rows if needed
    NSArray * selectedTorrents = nil;
    int numSelected = [fTableView numberOfSelectedRows];
    if (numSelected > 0 && numSelected < [fDisplayedTorrents count])
        selectedTorrents = [self torrentsAtIndexes: [fTableView selectedRowIndexes]];

    [self sortTorrentsIgnoreSelected]; //actually sort
    
    //set selected rows if needed
    if (selectedTorrents)
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [selectedTorrents objectEnumerator];
        NSMutableIndexSet * indexSet = [[NSMutableIndexSet alloc] init];
        while ((torrent = [enumerator nextObject]))
            [indexSet addIndex: [fDisplayedTorrents indexOfObject: torrent]];
        
        [fTableView selectRowIndexes: indexSet byExtendingSelection: NO];
        [indexSet release];
    }
}

//doesn't remember selected rows
- (void) sortTorrentsIgnoreSelected
{
    NSSortDescriptor * nameDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"name"
                            ascending: YES selector: @selector(caseInsensitiveCompare:)] autorelease],
                    * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"orderValue"
                                            ascending: YES] autorelease];

    NSArray * descriptors;
    if ([fSortType isEqualToString: @"Name"])
        descriptors = [[NSArray alloc] initWithObjects: nameDescriptor, orderDescriptor, nil];
    else if ([fSortType isEqualToString: @"State"])
    {
        NSSortDescriptor * stateDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"stateSortKey" ascending: NO] autorelease],
                        * progressDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"progressSortKey" ascending: NO] autorelease];
        
        descriptors = [[NSArray alloc] initWithObjects: stateDescriptor, progressDescriptor,
                                                            nameDescriptor, orderDescriptor, nil];
    }
    else if ([fSortType isEqualToString: @"Progress"])
    {
        NSSortDescriptor * progressDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"progressSortKey" ascending: YES] autorelease];
        
        descriptors = [[NSArray alloc] initWithObjects: progressDescriptor, nameDescriptor, orderDescriptor, nil];
    }
    else if ([fSortType isEqualToString: @"Date"])
    {
        NSSortDescriptor * dateDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"date" ascending: YES] autorelease];
    
        descriptors = [[NSArray alloc] initWithObjects: dateDescriptor, orderDescriptor, nil];
    }
    else
        descriptors = [[NSArray alloc] initWithObjects: orderDescriptor, nil];

    [fDisplayedTorrents sortUsingDescriptors: descriptors];
    [descriptors release];
    
    [fTableView reloadData];
}

- (void) setSort: (id) sender
{
    //get checked items
    NSMenuItem * prevSortItem, * prevSortActionItem;
    if ([fSortType isEqualToString: @"Name"])
    {
        prevSortItem = fNameSortItem;
        prevSortActionItem = fNameSortActionItem;
    }
    else if ([fSortType isEqualToString: @"State"])
    {
        prevSortItem = fStateSortItem;
        prevSortActionItem = fStateSortActionItem;
    }
    else if ([fSortType isEqualToString: @"Progress"])
    {
        prevSortItem = fProgressSortItem;
        prevSortActionItem = fProgressSortActionItem;
    }
    else if ([fSortType isEqualToString: @"Date"])
    {
        prevSortItem = fDateSortItem;
        prevSortActionItem = fDateSortActionItem;
    }
    else
    {
        prevSortItem = fOrderSortItem;
        prevSortActionItem = fOrderSortActionItem;
    }
    
    if (sender != prevSortItem && sender != prevSortActionItem)
    {
        [fSortType release];
        
        //get new items to check
        NSMenuItem * currentSortItem, * currentSortActionItem;
        if (sender == fNameSortItem || sender == fNameSortActionItem)
        {
            currentSortItem = fNameSortItem;
            currentSortActionItem = fNameSortActionItem;
            fSortType = [[NSString alloc] initWithString: @"Name"];
        }
        else if (sender == fStateSortItem || sender == fStateSortActionItem)
        {
            currentSortItem = fStateSortItem;
            currentSortActionItem = fStateSortActionItem;
            fSortType = [[NSString alloc] initWithString: @"State"];
        }
        else if (sender == fProgressSortItem || sender == fProgressSortActionItem)
        {
            currentSortItem = fProgressSortItem;
            currentSortActionItem = fProgressSortActionItem;
            fSortType = [[NSString alloc] initWithString: @"Progress"];
        }
        else if (sender == fDateSortItem || sender == fDateSortActionItem)
        {
            currentSortItem = fDateSortItem;
            currentSortActionItem = fDateSortActionItem;
            fSortType = [[NSString alloc] initWithString: @"Date"];
        }
        else
        {
            currentSortItem = fOrderSortItem;
            currentSortActionItem = fOrderSortActionItem;
            fSortType = [[NSString alloc] initWithString: @"Order"];
        }
    
        [prevSortItem setState: NSOffState];
        [prevSortActionItem setState: NSOffState];
        [currentSortItem setState: NSOnState];
        [currentSortActionItem setState: NSOnState];
        
        [fDefaults setObject: fSortType forKey: @"Sort"];
    }

    [self sortTorrents];
}

- (void) applyFilter: (id) sender
{
    //remember selected rows if needed
    NSArray * selectedTorrents = [fTableView numberOfSelectedRows] > 0
                ? [self torrentsAtIndexes: [fTableView selectedRowIndexes]] : nil;

    NSMutableArray * tempTorrents = [[NSMutableArray alloc] initWithCapacity: [fTorrents count]];

    BOOL filtering = YES;
    if ([fFilterType isEqualToString: @"Pause"])
    {
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if (![torrent isActive])
                [tempTorrents addObject: torrent];
    }
    else if ([fFilterType isEqualToString: @"Seed"])
    {
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive] && [torrent progress] >= 1.0)
                [tempTorrents addObject: torrent];
    }
    else if ([fFilterType isEqualToString: @"Download"])
    {
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive] && [torrent progress] < 1.0)
                [tempTorrents addObject: torrent];
    }
    else
    {
        filtering = NO;
        [tempTorrents setArray: fTorrents];
    }
    
    NSString * searchString = [fSearchFilterField stringValue];
    if ([searchString length] > 0)
    {
        filtering = YES;
        
        int i;
        for (i = [tempTorrents count] - 1; i >= 0; i--)
            if ([[[tempTorrents objectAtIndex: i] name] rangeOfString: searchString
                                        options: NSCaseInsensitiveSearch].location == NSNotFound)
                [tempTorrents removeObjectAtIndex: i];
    }
    
    [fDisplayedTorrents setArray: tempTorrents];
    [tempTorrents release];
    
    [self sortTorrentsIgnoreSelected];
    
    //set selected rows if needed
    if (selectedTorrents)
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [selectedTorrents objectEnumerator];
        NSMutableIndexSet * indexSet = [[NSMutableIndexSet alloc] init];
        unsigned index;
        while ((torrent = [enumerator nextObject]))
            if ((index = [fDisplayedTorrents indexOfObject: torrent]) != NSNotFound)
                [indexSet addIndex: index];
        
        [fTableView selectRowIndexes: indexSet byExtendingSelection: NO];
        [indexSet release];
    }
    
    //set status bar torrent count text
    NSMutableString * totalTorrentsString = [NSMutableString stringWithString: @""];
    if (filtering)
        [totalTorrentsString appendFormat: @"%d/", [fDisplayedTorrents count]];
    
    int totalCount = [fTorrents count];
    [totalTorrentsString appendFormat: @"%d Transfer%s", totalCount, totalCount == 1 ? "" : "s"];
    
    [fTotalTorrentsField setStringValue: totalTorrentsString];

    [self setWindowSizeToFit];
}

//resets filter and sorts torrents
- (void) setFilter: (id) sender
{
    BarButton * prevFilterButton;
    if ([fFilterType isEqualToString: @"Pause"])
        prevFilterButton = fPauseFilterButton;
    else if ([fFilterType isEqualToString: @"Seed"])
        prevFilterButton = fSeedFilterButton;
    else if ([fFilterType isEqualToString: @"Download"])
        prevFilterButton = fDownloadFilterButton;
    else
        prevFilterButton = fNoFilterButton;
    
    if (sender != prevFilterButton)
    {
        [prevFilterButton setEnabled: NO];
        [sender setEnabled: YES];

        [fFilterType release];
        if (sender == fDownloadFilterButton)
            fFilterType = [[NSString alloc] initWithString: @"Download"];
        else if (sender == fPauseFilterButton)
            fFilterType = [[NSString alloc] initWithString: @"Pause"];
        else if (sender == fSeedFilterButton)
            fFilterType = [[NSString alloc] initWithString: @"Seed"];
        else
            fFilterType = [[NSString alloc] initWithString: @"None"];

        [fDefaults setObject: fFilterType forKey: @"Filter"];
    }

    [self applyFilter: nil];
}

- (void) toggleSpeedLimit: (id) sender
{
    fSpeedLimitEnabled = !fSpeedLimitEnabled;
    int state = fSpeedLimitEnabled ? NSOnState : NSOffState;

    [fSpeedLimitItem setState: state];
    [fSpeedLimitDockItem setState: state];
    
    [fSpeedLimitButton setImage: !fSpeedLimitEnabled ? fSpeedLimitNormalImage
        : ([NSColor currentControlTint] == NSBlueControlTint ? fSpeedLimitBlueImage : fSpeedLimitGraphiteImage)];
    
    [fPrefsController enableSpeedLimit: fSpeedLimitEnabled];
}

- (void) autoSpeedLimitChange: (NSNotification *) notification
{
    if (![fDefaults boolForKey: @"SpeedLimitAuto"])
        return;
 
    int onHour = [fDefaults integerForKey: @"SpeedLimitAutoOnHour"],
        offHour = [fDefaults integerForKey: @"SpeedLimitAutoOffHour"];
    
    //check if should be on if within range
    BOOL shouldBeOn;
    int hour = [[NSCalendarDate calendarDate] hourOfDay];
    if (onHour < offHour)
        shouldBeOn = hour >= onHour && hour < offHour;
    else
        shouldBeOn = hour < offHour || hour >= onHour;
    
    if (fSpeedLimitEnabled != shouldBeOn)
        [self toggleSpeedLimit: nil];
}

- (void) autoSpeedLimit: (NSTimer *) timer
{
    if (![fDefaults boolForKey: @"SpeedLimitAuto"])
        return;
 
    //toggle if within first few seconds of hour
    NSCalendarDate * currentDate = [NSCalendarDate calendarDate];
    if ([currentDate minuteOfHour] == 0 && [currentDate secondOfMinute] < AUTO_SPEED_LIMIT_SECONDS
            && [currentDate hourOfDay] == (fSpeedLimitEnabled ? [fDefaults integerForKey: @"SpeedLimitAutoOffHour"]
                                            : [fDefaults integerForKey: @"SpeedLimitAutoOnHour"]))
    {
        [self toggleSpeedLimit: nil];
        
        [GrowlApplicationBridge notifyWithTitle: [@"Speed Limit Auto " stringByAppendingString: 
            fSpeedLimitEnabled ? @"Enabled" : @"Disabled"] description: @"Bandwidth settings changed"
            notificationName: GROWL_AUTO_SPEED_LIMIT iconData: nil priority: 0 isSticky: NO clickContext: nil];
    }
}

- (void) setLimitGlobalEnabled: (id) sender
{
    [fPrefsController setLimitEnabled: (sender == fUploadLimitItem || sender == fDownloadLimitItem)
        type: (sender == fUploadLimitItem || sender == fUploadNoLimitItem) ? @"Upload" : @"Download"];
}

- (void) setQuickLimitGlobal: (id) sender
{
    [fPrefsController setQuickLimit: [[sender title] intValue]
        type: [sender menu] == fUploadMenu ? @"Upload" : @"Download"];
}

- (void) limitGlobalChange: (NSNotification *) notification
{
    NSDictionary * dict = [notification object];
    
    NSMenuItem * limitItem, * noLimitItem;
    if ([[dict objectForKey: @"Type"] isEqualToString: @"Upload"])
    {
        limitItem = fUploadLimitItem;
        noLimitItem = fUploadNoLimitItem;
    }
    else
    {
        limitItem = fDownloadLimitItem;
        noLimitItem = fDownloadNoLimitItem;
    }
    
    BOOL enable = [[dict objectForKey: @"Enable"] boolValue];
    [limitItem setState: enable ? NSOnState : NSOffState];
    [noLimitItem setState: !enable ? NSOnState : NSOffState];
    
    [limitItem setTitle: [NSString stringWithFormat: @"Limit (%d KB/s)",
                            [[dict objectForKey: @"Limit"] intValue]]];

    [dict release];
}

- (void) setRatioGlobalEnabled: (id) sender
{
    [fPrefsController setRatioEnabled: sender == fRatioSetItem];
}

- (void) setQuickRatioGlobal: (id) sender
{
    [fPrefsController setQuickRatio: [[sender title] floatValue]];
}

- (void) ratioGlobalChange: (NSNotification *) notification
{
    NSDictionary * dict = [notification object];
    
    BOOL enable = [[dict objectForKey: @"Enable"] boolValue];
    [fRatioSetItem setState: enable ? NSOnState : NSOffState];
    [fRatioNotSetItem setState: !enable ? NSOnState : NSOffState];
    
    [fRatioSetItem setTitle: [NSString stringWithFormat: @"Stop at Ratio (%.2f)",
                            [[dict objectForKey: @"Ratio"] floatValue]]];

    [dict release];
}

- (void) checkWaitingForStopped: (NSNotification *) notification
{
    [self checkToStartWaiting: [notification object]];
    
    [self updateUI: nil];
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    [self updateTorrentHistory];
}

- (void) checkToStartWaiting: (Torrent *) finishedTorrent
{
    //don't try to start a transfer if there should be none waiting
    if (![[fDefaults stringForKey: @"StartSetting"] isEqualToString: @"Wait"])
        return;

    int desiredActive = [fDefaults integerForKey: @"WaitToStartNumber"];
    
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent, * torrentToStart = nil;
    while ((torrent = [enumerator nextObject]))
    {
        //ignore the torrent just stopped
        if (torrent == finishedTorrent)
            continue;
    
        if ([torrent isActive])
        {
            if (![torrent isSeeding])
            {
                desiredActive--;
                if (desiredActive <= 0)
                    return;
            }
        }
        else
        {
            //use as next if it is waiting to start and either no previous or order value is lower
            if ([torrent waitingToStart] && (!torrentToStart
                || [[torrentToStart orderValue] compare: [torrent orderValue]] == NSOrderedDescending))
                torrentToStart = torrent;
        }
    }
    
    //since it hasn't returned, the queue amount has not been met
    if (torrentToStart)
    {
        [torrentToStart startTransfer];
        
        [self updateUI: nil];
        [self applyFilter: nil];
        [fInfoController updateInfoStatsAndSettings];
        [self updateTorrentHistory];
    }
}

- (void) torrentStartSettingChange: (NSNotification *) notification
{
    [self attemptToStartMultipleAuto: [notification object]];

    [self updateUI: nil];
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    [self updateTorrentHistory];
}

- (void) globalStartSettingChange: (NSNotification *) notification
{
    [self attemptToStartMultipleAuto: fTorrents];
    
    [self updateUI: nil];
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    [self updateTorrentHistory];
}

- (void) torrentStoppedForRatio: (NSNotification *) notification
{
    [self applyFilter: nil];
    [fInfoController updateInfoStatsAndSettings];
    
    if ([fDefaults boolForKey: @"PlaySeedingSound"])
    {
        NSSound * sound;
        if ((sound = [NSSound soundNamed: [fDefaults stringForKey: @"SeedingSound"]]))
            [sound play];
    }
    
    [GrowlApplicationBridge notifyWithTitle: @"Seeding Complete" description: [[notification object] name]
        notificationName: GROWL_SEEDING_COMPLETE iconData: nil priority: 0 isSticky: NO clickContext: nil];
}

- (void) attemptToStartAuto: (Torrent *) torrent
{
    [self attemptToStartMultipleAuto: [NSArray arrayWithObject: torrent]];
}

//will try to start, taking into consideration the start preference
- (void) attemptToStartMultipleAuto: (NSArray *) torrents
{
    NSString * startSetting = [fDefaults stringForKey: @"StartSetting"];
    if ([startSetting isEqualToString: @"Start"])
    {
        NSEnumerator * enumerator = [torrents objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if ([torrent waitingToStart])
                [torrent startTransfer];
        
        return;
    }
    else if (![startSetting isEqualToString: @"Wait"])
        return;
    else;
    
    //determine the number of downloads needed to start
    int desiredActive = [fDefaults integerForKey: @"WaitToStartNumber"];
            
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        if ([torrent isActive] && ![torrent isSeeding])
        {
            desiredActive--;
            if (desiredActive <= 0)
                break;
        }
    
    //sort torrents by order value
    NSArray * sortedTorrents;
    if ([torrents count] > 1 && desiredActive > 0)
    {
        NSSortDescriptor * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                    @"orderValue" ascending: YES] autorelease];
        NSArray * descriptors = [[NSArray alloc] initWithObjects: orderDescriptor, nil];
        
        sortedTorrents = [torrents sortedArrayUsingDescriptors: descriptors];
        [descriptors release];
    }
    else
        sortedTorrents = torrents;

    enumerator = [sortedTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        if ([torrent waitingToStart])
        {
            if ([torrent progress] >= 1.0)
                [torrent startTransfer];
            else if (desiredActive > 0)
            {
                [torrent startTransfer];
                desiredActive--;
            }
            else
                continue;
            
            [torrent update];
        }
    }
}

- (void) reloadInspectorSettings: (NSNotification *) notification
{
    [fInfoController updateInfoStatsAndSettings];
}

- (void) checkAutoImportDirectory: (NSTimer *) timer
{
    if (![fDefaults boolForKey: @"AutoImport"])
        return;
    	
    NSString * path = [[fDefaults stringForKey: @"AutoImportDirectory"] stringByExpandingTildeInPath];
    
    //if folder cannot be found or the contents hasn't changed simply give up
    NSArray * allFileNames;
    if (!(allFileNames = [[NSFileManager defaultManager] directoryContentsAtPath: path])
            || [allFileNames isEqualToArray: fAutoImportedNames])
        return;

    //try to add files that haven't already been added
    NSMutableArray * newFileNames = [NSMutableArray arrayWithArray: allFileNames];
    [newFileNames removeObjectsInArray: fAutoImportedNames];
    
    //save the current list of files
    [fAutoImportedNames setArray: allFileNames];
    
    NSEnumerator * enumerator = [newFileNames objectEnumerator];
    NSString * file;
    unsigned oldCount;
    while ((file = [enumerator nextObject]))
        if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
        {
            oldCount = [fTorrents count];
            [self openFiles: [NSArray arrayWithObject: [path stringByAppendingPathComponent: file]]];
            
            //import only actually happened if the torrent array is larger
            if (oldCount < [fTorrents count])
                [GrowlApplicationBridge notifyWithTitle: @"Torrent File Auto Added" description: file
                    notificationName: GROWL_AUTO_ADD iconData: nil priority: 0 isSticky: NO clickContext: nil];
        }
}

- (void) autoImportChange: (NSNotification *) notification
{
    [fAutoImportedNames removeAllObjects];
    [self checkAutoImportDirectory: nil];
}

- (int) numberOfRowsInTableView: (NSTableView *) t
{
    return [fDisplayedTorrents count];
}

- (void) tableView: (NSTableView *) t willDisplayCell: (id) cell
    forTableColumn: (NSTableColumn *) tableColumn row: (int) row
{
    [cell setTorrent: [fDisplayedTorrents objectAtIndex: row]];
}

- (BOOL) tableView: (NSTableView *) tableView writeRowsWithIndexes: (NSIndexSet *) indexes
    toPasteboard: (NSPasteboard *) pasteboard
{
    //only allow reordering of rows if sorting by order with no filter
    if ([fSortType isEqualToString: @"Order"] && [fFilterType isEqualToString: @"None"]
            && [[fSearchFilterField stringValue] length] == 0)
    {
        [pasteboard declareTypes: [NSArray arrayWithObject: TORRENT_TABLE_VIEW_DATA_TYPE] owner: self];
        [pasteboard setData: [NSKeyedArchiver archivedDataWithRootObject: indexes]
                                forType: TORRENT_TABLE_VIEW_DATA_TYPE];
        return YES;
    }
    return NO;
}

- (NSDragOperation) tableView: (NSTableView *) t validateDrop: (id <NSDraggingInfo>) info
    proposedRow: (int) row proposedDropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: NSFilenamesPboardType])
    {
        //check if any files to add have "torrent" as an extension
        NSEnumerator * enumerator = [[pasteboard propertyListForType: NSFilenamesPboardType] objectEnumerator];
        NSString * file;
        while ((file = [enumerator nextObject]))
            if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
            {
                [fTableView setDropRow: -1 dropOperation: NSTableViewDropOn];
                return NSDragOperationGeneric;
            }
    }
    else if ([[pasteboard types] containsObject: TORRENT_TABLE_VIEW_DATA_TYPE])
    {
        [fTableView setDropRow: row dropOperation: NSTableViewDropAbove];
        return NSDragOperationGeneric;
    }
    else
        return NSDragOperationNone;
}

- (BOOL) tableView: (NSTableView *) t acceptDrop: (id <NSDraggingInfo>) info
    row: (int) newRow dropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: NSFilenamesPboardType])
    {
        //create an array of files with the "torrent" extension
        NSMutableArray * filesToOpen = [[NSMutableArray alloc] init];
        NSEnumerator * enumerator = [[pasteboard propertyListForType: NSFilenamesPboardType] objectEnumerator];
        NSString * file;
        while ((file = [enumerator nextObject]))
            if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
                [filesToOpen addObject: file];
    
        [self application: NSApp openFiles: filesToOpen];
        [filesToOpen release];
    }
    else
    {
        //remember selected rows if needed
        NSArray * selectedTorrents = nil;
        int numSelected = [fTableView numberOfSelectedRows];
        if (numSelected > 0 && numSelected < [fDisplayedTorrents count])
            selectedTorrents = [self torrentsAtIndexes: [fTableView selectedRowIndexes]];
    
        NSIndexSet * indexes = [NSKeyedUnarchiver unarchiveObjectWithData:
                                [pasteboard dataForType: TORRENT_TABLE_VIEW_DATA_TYPE]];
        
        //move torrent in array 
        NSArray * movingTorrents = [[self torrentsAtIndexes: indexes] retain];
        [fDisplayedTorrents removeObjectsInArray: movingTorrents];
        
        //determine the insertion index now that transfers to move have been removed
        int i, decrease = 0;
        for (i = [indexes firstIndex]; i < newRow && i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
            decrease++;
        
        //insert objects at new location
        for (i = 0; i < [movingTorrents count]; i++)
            [fDisplayedTorrents insertObject: [movingTorrents objectAtIndex: i] atIndex: newRow - decrease + i];
        
        [movingTorrents release];
        
        //redo order values
        int low = [indexes firstIndex], high = [indexes lastIndex];
        if (newRow < low)
            low = newRow;
        else if (newRow > high + 1)
            high = newRow - 1;
        else;
        
        for (i = low; i <= high; i++)
            [[fDisplayedTorrents objectAtIndex: i] setOrderValue: i];
        
        [fTableView reloadData];
        
        //set selected rows if needed
        if (selectedTorrents)
        {
            Torrent * torrent;
            NSEnumerator * enumerator = [selectedTorrents objectEnumerator];
            NSMutableIndexSet * indexSet = [[NSMutableIndexSet alloc] init];
            while ((torrent = [enumerator nextObject]))
                [indexSet addIndex: [fDisplayedTorrents indexOfObject: torrent]];
            
            [fTableView selectRowIndexes: indexSet byExtendingSelection: NO];
            [indexSet release];
        }
    }
    
    return YES;
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [fInfoController updateInfoForTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]];
}

- (void) toggleSmallView: (id) sender
{
    BOOL makeSmall = ![fDefaults boolForKey: @"SmallView"];
    
    [fTableView setRowHeight: makeSmall ? ROW_HEIGHT_SMALL : ROW_HEIGHT_REGULAR];
    [fSmallViewItem setState: makeSmall];
    
    [fDefaults setBool: makeSmall forKey: @"SmallView"];
    
    //window min height
    NSSize contentMinSize = [fWindow contentMinSize],
            contentSize = [[fWindow contentView] frame].size;
    contentMinSize.height = contentSize.height - [fScrollView frame].size.height
                            + [fTableView rowHeight] + [fTableView intercellSpacing].height;
    [fWindow setContentMinSize: contentMinSize];
    
    //resize for larger min height if not set to auto size
    if (![fDefaults boolForKey: @"AutoSize"])
    {
        if (!makeSmall && contentSize.height < contentMinSize.height)
        {
            NSRect frame = [fWindow frame];
            float heightChange = contentMinSize.height - contentSize.height;
            frame.size.height += heightChange;
            frame.origin.y -= heightChange;
            
            [fWindow setFrame: frame display: YES];
            [fTableView reloadData];
        }
    }
    else
        [self setWindowSizeToFit];
}

- (void) toggleStatusBar: (id) sender
{
    [self showStatusBar: [fStatusBar isHidden] animate: YES];
    [fDefaults setBool: ![fStatusBar isHidden] forKey: @"StatusBar"];
}

- (void) showStatusBar: (BOOL) show animate: (BOOL) animate
{
    if (show != [fStatusBar isHidden])
        return;

    if (show)
        [fStatusBar setHidden: NO];

    NSRect frame = [fWindow frame];
    float heightChange = [fStatusBar frame].size.height;
    if (!show)
        heightChange *= -1;

    frame.size.height += heightChange;
    frame.origin.y -= heightChange;
    
    [self updateUI: nil];
    
    //set views to not autoresize
    unsigned int statsMask = [fStatusBar autoresizingMask];
    unsigned int filterMask = [fFilterBar autoresizingMask];
    unsigned int scrollMask = [fScrollView autoresizingMask];
    [fStatusBar setAutoresizingMask: 0];
    [fFilterBar setAutoresizingMask: 0];
    [fScrollView setAutoresizingMask: 0];
    
    [fWindow setFrame: frame display: YES animate: animate]; 
    
    //re-enable autoresize
    [fStatusBar setAutoresizingMask: statsMask];
    [fFilterBar setAutoresizingMask: filterMask];
    [fScrollView setAutoresizingMask: scrollMask];
    
    //change min size
    NSSize minSize = [fWindow contentMinSize];
    minSize.height += heightChange;
    [fWindow setContentMinSize: minSize];
    
    if (!show)
        [fStatusBar setHidden: YES];
}

- (void) toggleFilterBar: (id) sender
{
    //disable filtering when hiding
    if (![fFilterBar isHidden])
    {
        [fSearchFilterField setStringValue: @""];
        [self setFilter: fNoFilterButton];
    }

    [self showFilterBar: [fFilterBar isHidden] animate: YES];
    [fDefaults setBool: ![fFilterBar isHidden] forKey: @"FilterBar"];
}

- (void) showFilterBar: (BOOL) show animate: (BOOL) animate
{
    if (show != [fFilterBar isHidden])
        return;

    if (show)
        [fFilterBar setHidden: NO];

    NSRect frame = [fWindow frame];
    float heightChange = [fFilterBar frame].size.height;
    if (!show)
        heightChange *= -1;

    frame.size.height += heightChange;
    frame.origin.y -= heightChange;
    
    //set views to not autoresize
    unsigned int filterMask = [fFilterBar autoresizingMask];
    unsigned int scrollMask = [fScrollView autoresizingMask];
    [fFilterBar setAutoresizingMask: 0];
    [fScrollView setAutoresizingMask: 0];
    
    [fWindow setFrame: frame display: YES animate: animate]; 
    
    //re-enable autoresize
    [fFilterBar setAutoresizingMask: filterMask];
    [fScrollView setAutoresizingMask: scrollMask];
    
    //change min size
    NSSize minSize = [fWindow contentMinSize];
    minSize.height += heightChange;
    [fWindow setContentMinSize: minSize];
    
    if (!show)
    {
        [fFilterBar setHidden: YES];
        [fWindow makeFirstResponder: fTableView];
    }
}

- (void) toggleAdvancedBar: (id) sender
{
    int state = ![fAdvancedBarItem state];
    [fAdvancedBarItem setState: state];
    [fDefaults setBool: state forKey: @"UseAdvancedBar"];
    
    [fTableView display];
}

- (NSToolbarItem *) toolbar: (NSToolbar *) t itemForItemIdentifier:
    (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if ([ident isEqualToString: TOOLBAR_OPEN])
    {
        [item setLabel: @"Open"];
        [item setPaletteLabel: @"Open Torrent Files"];
        [item setToolTip: @"Open torrent files"];
        [item setImage: [NSImage imageNamed: @"Open.png"]];
        [item setTarget: self];
        [item setAction: @selector(openShowSheet:)];
    }
    else if ([ident isEqualToString: TOOLBAR_REMOVE])
    {
        [item setLabel: @"Remove"];
        [item setPaletteLabel: @"Remove Selected"];
        [item setToolTip: @"Remove selected transfers"];
        [item setImage: [NSImage imageNamed: @"Remove.png"]];
        [item setTarget: self];
        [item setAction: @selector(removeNoDelete:)];
    }
    else if ([ident isEqualToString: TOOLBAR_INFO])
    {
        [item setLabel: @"Inspector"];
        [item setPaletteLabel: @"Toggle Inspector"];
        [item setToolTip: @"Toggle the torrent inspector"];
        [item setImage: [NSImage imageNamed: @"Info.png"]];
        [item setTarget: self];
        [item setAction: @selector(showInfo:)];
    }
    else if ([ident isEqualToString: TOOLBAR_PAUSE_ALL])
    {
        [item setLabel: @"Pause All"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Pause all transfers"];
        [item setImage: [NSImage imageNamed: @"PauseAll.png"]];
        [item setTarget: self];
        [item setAction: @selector(stopAllTorrents:)];
    }
    else if ([ident isEqualToString: TOOLBAR_RESUME_ALL])
    {
        [item setLabel: @"Resume All"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Resume all transfers"];
        [item setImage: [NSImage imageNamed: @"ResumeAll.png"]];
        [item setTarget: self];
        [item setAction: @selector(resumeAllTorrents:)];
    }
    else if ([ident isEqualToString: TOOLBAR_PAUSE_SELECTED])
    {
        [item setLabel: @"Pause"];
        [item setPaletteLabel: @"Pause Selected"];
        [item setToolTip: @"Pause selected transfers"];
        [item setImage: [NSImage imageNamed: @"PauseSelected.png"]];
        [item setTarget: self];
        [item setAction: @selector(stopSelectedTorrents:)];
    }
    else if ([ident isEqualToString: TOOLBAR_RESUME_SELECTED])
    {
        [item setLabel: @"Resume"];
        [item setPaletteLabel: @"Resume Selected"];
        [item setToolTip: @"Resume selected transfers"];
        [item setImage: [NSImage imageNamed: @"ResumeSelected.png"]];
        [item setTarget: self];
        [item setAction: @selector(resumeSelectedTorrents:)];
    }
    else if ([ident isEqualToString: TOOLBAR_FILTER])
    {
        [item setLabel: @"Filter"];
        [item setPaletteLabel: @"Toggle Filter"];
        [item setToolTip: @"Toggle the filter bar"];
        [item setImage: [NSImage imageNamed: @"Filter.png"]];
        [item setTarget: self];
        [item setAction: @selector(toggleFilterBar:)];
    }
    else
    {
        [item release];
        return nil;
    }

    return item;
}

- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar *) t
{
    return [NSArray arrayWithObjects:
            TOOLBAR_OPEN, TOOLBAR_REMOVE,
            TOOLBAR_PAUSE_SELECTED, TOOLBAR_RESUME_SELECTED,
            TOOLBAR_PAUSE_ALL, TOOLBAR_RESUME_ALL, TOOLBAR_FILTER, TOOLBAR_INFO,
            NSToolbarSeparatorItemIdentifier,
            NSToolbarSpaceItemIdentifier,
            NSToolbarFlexibleSpaceItemIdentifier,
            NSToolbarCustomizeToolbarItemIdentifier, nil];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) t
{
    return [NSArray arrayWithObjects:
            TOOLBAR_OPEN, TOOLBAR_REMOVE,
            NSToolbarSeparatorItemIdentifier,
            TOOLBAR_PAUSE_ALL, TOOLBAR_RESUME_ALL,
            NSToolbarFlexibleSpaceItemIdentifier,
            TOOLBAR_FILTER, TOOLBAR_INFO, nil];
}

- (BOOL) validateToolbarItem: (NSToolbarItem *) toolbarItem
{
    NSString * ident = [toolbarItem itemIdentifier];

    //enable remove item
    if ([ident isEqualToString: TOOLBAR_REMOVE])
        return [fTableView numberOfSelectedRows] > 0;

    //enable pause all item
    if ([ident isEqualToString: TOOLBAR_PAUSE_ALL])
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive])
                return YES;
        return NO;
    }

    //enable resume all item
    if ([ident isEqualToString: TOOLBAR_RESUME_ALL])
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isPaused])
                return YES;
        return NO;
    }

    //enable pause item
    if ([ident isEqualToString: TOOLBAR_PAUSE_SELECTED])
    {
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            if ([[fDisplayedTorrents objectAtIndex: i] isActive])
                return YES;
        return NO;
    }
    
    //enable resume item
    if ([ident isEqualToString: TOOLBAR_RESUME_SELECTED])
    {
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            if ([[fDisplayedTorrents objectAtIndex: i] isPaused])
                return YES;
        return NO;
    }

    return YES;
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];

    //only enable some items if it is in a context menu or the window is useable 
    BOOL canUseMenu = [fWindow isKeyWindow] || [[[menuItem menu] title] isEqualToString: @"Context"];

    //enable open items
    if (action == @selector(openShowSheet:))
        return [fWindow attachedSheet] == nil;

    //enable show info
    if (action == @selector(showInfo:))
    {
        NSString * title = [[fInfoController window] isVisible] ? @"Hide Inspector" : @"Show Inspector";
        if (![[menuItem title] isEqualToString: title])
                [menuItem setTitle: title];

        return YES;
    }
    
    //enable prev/next inspector tab
    if (action == @selector(setInfoTab:))
        return [[fInfoController window] isVisible];
    
    //enable toggle status bar
    if (action == @selector(toggleStatusBar:))
    {
        NSString * title = [fStatusBar isHidden] ? @"Show Status Bar" : @"Hide Status Bar";
        if (![[menuItem title] isEqualToString: title])
                [menuItem setTitle: title];

        return canUseMenu;
    }
    
    //enable toggle filter bar
    if (action == @selector(toggleFilterBar:))
    {
        NSString * title = [fFilterBar isHidden] ? @"Show Filter Bar" : @"Hide Filter Bar";
        if (![[menuItem title] isEqualToString: title])
                [menuItem setTitle: title];

        return canUseMenu;
    }

    //enable reveal in finder
    if (action == @selector(revealFile:))
        return canUseMenu && [fTableView numberOfSelectedRows] > 0;

    //enable remove items
    if (action == @selector(removeNoDelete:) || action == @selector(removeDeleteData:)
        || action == @selector(removeDeleteTorrent:) || action == @selector(removeDeleteDataAndTorrent:))
    {
        BOOL warning = NO,
            onlyDownloading = [fDefaults boolForKey: @"CheckRemoveDownloading"],
            canDelete = action != @selector(removeDeleteTorrent:) && action != @selector(removeDeleteDataAndTorrent:);
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            torrent = [fDisplayedTorrents objectAtIndex: i];
            if (!warning && [torrent isActive])
            {
                warning = onlyDownloading ? ![torrent isSeeding] : YES;
                if (warning && canDelete)
                    break;
            }
            if (!canDelete && [torrent publicTorrent])
            {
                canDelete = YES;
                if (warning)
                    break;
            }
        }
    
        //append or remove ellipsis when needed
        NSString * title = [menuItem title], * ellipsis = [NSString ellipsis];
        if (warning && [fDefaults boolForKey: @"CheckRemove"])
        {
            if (![title hasSuffix: ellipsis])
                [menuItem setTitle: [title stringByAppendingEllipsis]];
        }
        else
        {
            if ([title hasSuffix: ellipsis])
                [menuItem setTitle: [title substringToIndex: [title rangeOfString: ellipsis].location]];
        }
        
        return canUseMenu && canDelete && [fTableView numberOfSelectedRows] > 0;
    }

    //enable pause all item
    if (action == @selector(stopAllTorrents:))
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive])
                return YES;
        return NO;
    }
    
    //enable resume all item
    if (action == @selector(resumeAllTorrents:))
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isPaused])
                return YES;
        return NO;
    }
    
    //enable resume waiting item
    if (action == @selector(resumeWaitingTorrents:))
    {
        if (![[fDefaults stringForKey: @"StartSetting"] isEqualToString: @"Wait"])
            return NO;
    
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable pause item
    if (action == @selector(stopSelectedTorrents:))
    {
        if (!canUseMenu)
            return NO;
    
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            torrent = [fDisplayedTorrents objectAtIndex: i];
            if ([torrent isActive])
                return YES;
        }
        return NO;
    }
    
    //enable resume item
    if (action == @selector(resumeSelectedTorrents:))
    {
        if (!canUseMenu)
            return NO;
    
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            torrent = [fDisplayedTorrents objectAtIndex: i];
            if ([torrent isPaused])
                return YES;
        }
        return NO;
    }
    
    //enable sort and advanced bar items
    if (action == @selector(setSort:) || action == @selector(toggleAdvancedBar:)
            || action == @selector(toggleSmallView:))
        return canUseMenu;
    
    //enable copy torrent file item
    if (action == @selector(copyTorrentFile:))
        return canUseMenu && [fTableView numberOfSelectedRows] > 0;

    return YES;
}

- (void) sleepCallBack: (natural_t) messageType argument: (void *) messageArgument
{
    NSEnumerator * enumerator;
    Torrent * torrent;
    BOOL active;

    switch (messageType)
    {
        case kIOMessageSystemWillSleep:
            //close all connections before going to sleep and remember we should resume when we wake up
            [fTorrents makeObjectsPerformSelector: @selector(sleep)];

            //wait for running transfers to stop (5 second timeout)
            NSDate * start = [NSDate date];
            BOOL timeUp = NO;
            
            NSEnumerator * enumerator = [fTorrents objectEnumerator];
            Torrent * torrent;
            while (!timeUp && (torrent = [enumerator nextObject]))
                while (![torrent isPaused] && !(timeUp = [start timeIntervalSinceNow] < -5.0))
                {
                    usleep(100000);
                    [torrent update];
                }

            IOAllowPowerChange(fRootPort, (long) messageArgument);
            break;

        case kIOMessageCanSystemSleep:
            //pevent idle sleep unless all paused
            active = NO;
            enumerator = [fTorrents objectEnumerator];
            while ((torrent = [enumerator nextObject]))
                if ([torrent isActive])
                {
                    active = YES;
                    break;
                }

            if (active)
                IOCancelPowerChange(fRootPort, (long) messageArgument);
            else
                IOAllowPowerChange(fRootPort, (long) messageArgument);
            break;

        case kIOMessageSystemHasPoweredOn:
            //resume sleeping transfers after we wake up
            [fTorrents makeObjectsPerformSelector: @selector(wakeUp)];
            break;
    }
}

- (void) resetDockBadge: (NSNotification *) notification
{
    float downloadRate, uploadRate;
    tr_torrentRates(fLib, & downloadRate, & uploadRate);
    
    [fBadger updateBadgeWithCompleted: fCompleted uploadRate: uploadRate downloadRate: downloadRate];
}

- (NSRect) windowWillUseStandardFrame: (NSWindow *) window defaultFrame: (NSRect) defaultFrame
{
    //if auto size is enabled, the current frame shouldn't need to change
    NSRect frame = [fDefaults boolForKey: @"AutoSize"] ? [window frame] : [self sizedWindowFrame];
    
    frame.size.width = [fDefaults boolForKey: @"SmallView"] ? [fWindow minSize].width : WINDOW_REGULAR_WIDTH;
    return frame;
}

- (void) setWindowSizeToFit
{
    if ([fDefaults boolForKey: @"AutoSize"])
        [fWindow setFrame: [self sizedWindowFrame] display: YES animate: YES];
}

- (NSRect) sizedWindowFrame
{
    NSRect frame = [fWindow frame];
    float newHeight = frame.size.height - [fScrollView frame].size.height
        + [fDisplayedTorrents count] * ([fTableView rowHeight] + [fTableView intercellSpacing].height);

    float minHeight = [fWindow minSize].height;
    if (newHeight < minHeight)
        newHeight = minHeight;
    else
    {
        float maxHeight = [[fWindow screen] visibleFrame].size.height;
        if ([fStatusBar isHidden])
            maxHeight -= [fStatusBar frame].size.height;
        if ([fFilterBar isHidden]) 
            maxHeight -= [fFilterBar frame].size.height;
        
        if (newHeight > maxHeight)
            newHeight = maxHeight;
    }

    frame.origin.y -= (newHeight - frame.size.height);
    frame.size.height = newHeight;
    return frame;
}

- (void) showMainWindow: (id) sender
{
    [fWindow makeKeyAndOrderFront: nil];
}

- (void) windowDidBecomeKey: (NSNotification *) notification
{
    //reset dock badge for completed
    if (fCompleted > 0)
    {
        fCompleted = 0;
        [self resetDockBadge: nil];
    }
}

- (NSSize) windowWillResize: (NSWindow *) sender toSize: (NSSize) proposedFrameSize
{
    //only resize horizontally if autosize is enabled
    if ([fDefaults boolForKey: @"AutoSize"])
        proposedFrameSize.height = [fWindow frame].size.height;
    return proposedFrameSize;
}

- (void) windowDidResize: (NSNotification *) notification
{
    //hide search filter if it overlaps filter buttons
    [fSearchFilterField setHidden: NSMaxX([fPauseFilterButton frame]) + 2.0 > [fSearchFilterField frame].origin.x];
}

- (void) linkHomepage: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: WEBSITE_URL]];
}

- (void) linkForums: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: FORUM_URL]];
}

- (void) checkUpdate: (id) sender
{
    [fPrefsController checkUpdate];
}

- (void) prepareForUpdate: (NSNotification *) notification
{
    fUpdateInProgress = YES;
}

- (NSDictionary *) registrationDictionaryForGrowl
{
    NSArray * notifications = [NSArray arrayWithObjects: GROWL_DOWNLOAD_COMPLETE, GROWL_SEEDING_COMPLETE,
                                                            GROWL_AUTO_ADD, GROWL_AUTO_SPEED_LIMIT, nil];
    return [NSDictionary dictionaryWithObjectsAndKeys: notifications, GROWL_NOTIFICATIONS_ALL,
                                notifications, GROWL_NOTIFICATIONS_DEFAULT, nil];
}

@end
