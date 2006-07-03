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

#define TORRENT_TABLE_VIEW_DATA_TYPE    @"TorrentTableViewDataType"

#define WEBSITE_URL @"http://transmission.m0k.org/"
#define FORUM_URL   @"http://transmission.m0k.org/forum/"

#define GROWL_PATH  @"/Library/PreferencePanes/Growl.prefPane/Contents/Resources/GrowlHelperApp.app"

static void sleepCallBack(void * controller, io_service_t y,
        natural_t messageType, void * messageArgument)
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
        fDefaults = [NSUserDefaults standardUserDefaults];
        fInfoController = [[InfoWindowController alloc] initWithWindowNibName: @"InfoWindow"];
        fPrefsController = [[PrefsController alloc] initWithWindowNibName: @"PrefsWindow"];
        fBadger = [[Badger alloc] init];
        
        //check and register Growl if it is installed for this user or all users
        NSFileManager * manager = [NSFileManager defaultManager];
        fHasGrowl = [manager fileExistsAtPath: GROWL_PATH]
            || [manager fileExistsAtPath: [NSHomeDirectory() stringByAppendingPathComponent: GROWL_PATH]];
        [self growlRegister];
    }
    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];

    [fTorrents release];
    [fToolbar release];
    [fInfoController release];
    [fBadger release];
    [fSortType release];
    
    tr_close( fLib );
    [super dealloc];
}

- (void) awakeFromNib
{
    [fPrefsController setPrefs: fLib];
    
    [fAdvancedBarItem setState: [fDefaults boolForKey: @"UseAdvancedBar"]];

    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Transmission Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: YES];
    [fToolbar setAutosavesConfiguration: YES];
    [fWindow setToolbar: fToolbar];
    [fWindow setDelegate: self];
    
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
    
    //set up status bar
    NSRect statusBarFrame = [fStatusBar frame];
    statusBarFrame.size.width = [fWindow frame].size.width;
    [fStatusBar setFrame: statusBarFrame];
    
    NSView * contentView = [fWindow contentView];
    [contentView addSubview: fStatusBar];
    [fStatusBar setFrameOrigin: NSMakePoint(0, [fScrollView frame].origin.y + [fScrollView frame].size.height)];
    [self showStatusBar: [fDefaults boolForKey: @"StatusBar"] animate: NO];
    
    //set speed limit
    if ([fDefaults boolForKey: @"SpeedLimit"])
    {
        [fSpeedLimitItem setState: NSOnState];
        [fSpeedLimitDockItem setState: NSOnState];
        [fSpeedLimitButton setState: NSOnState];
    }

    [fActionButton setToolTip: @"Shortcuts for changing global settings."];
    [fSpeedLimitButton setToolTip: @"Speed Limit overrides the total bandwidth limits with its own limits."];

    [fTableView setTorrents: fTorrents];
    [[fTableView tableColumnWithIdentifier: @"Torrent"] setDataCell: [[TorrentCell alloc] init]];

    [fTableView registerForDraggedTypes: [NSArray arrayWithObjects: NSFilenamesPboardType,
                                                        TORRENT_TABLE_VIEW_DATA_TYPE, nil]];

    //register for sleep notifications
    IONotificationPortRef notify;
    io_object_t iterator;
    if (fRootPort = IORegisterForSystemPower(self, & notify, sleepCallBack, & iterator))
    {
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notify),
                            kCFRunLoopCommonModes);
    }
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
    
    [self torrentNumberChanged];
    
    //set sort
    fSortType = [[fDefaults stringForKey: @"Sort"] retain];
    
    NSMenuItem * currentSortItem;
    if ([fSortType isEqualToString: @"Name"])
        currentSortItem = fNameSortItem;
    else if ([fSortType isEqualToString: @"State"])
        currentSortItem = fStateSortItem;
    else if ([fSortType isEqualToString: @"Progress"])
        currentSortItem = fProgressSortItem;
    else if ([fSortType isEqualToString: @"Date"])
        currentSortItem = fDateSortItem;
    else
        currentSortItem = fOrderSortItem;
    [currentSortItem setState: NSOnState];
    
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
    
    [nc addObserver: self selector: @selector(prepareForUpdate:)
                    name: SUUpdaterWillRestartNotification object: nil];
    fUpdateInProgress = NO;
    
    [nc addObserver: self selector: @selector(limitGlobalChange:)
                    name: @"LimitGlobalChange" object: nil];
    
    [nc addObserver: self selector: @selector(ratioGlobalChange:)
                    name: @"RatioGlobalChange" object: nil];
    
    //check to start another because of stopped torrent
    [nc addObserver: self selector: @selector(checkWaitingForStopped:)
                    name: @"StoppedDownloading" object: nil];
    
    //check all torrents for starting
    [nc addObserver: self selector: @selector(globalStartSettingChange:)
                    name: @"GlobalStartSettingChange" object: nil];

    //check if torrent should now start
    [nc addObserver: self selector: @selector(torrentStartSettingChange:)
                    name: @"TorrentStartSettingChange" object: nil];
    
    //change that just impacts the inspector
    [nc addObserver: self selector: @selector(reloadInspector:)
                    name: @"TorrentSettingChange" object: nil];

    //timer to update the interface
    fCompleted = 0;
    [self updateUI: nil];
    fTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0 target: self
        selector: @selector( updateUI: ) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSEventTrackingRunLoopMode];
    
    [self sortTorrents];
    
    [fWindow makeKeyAndOrderFront: nil];

    [self reloadInspector: nil];
    if ([fDefaults boolForKey: @"InfoVisible"])
        [self showInfo: nil];
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

        BOOL shouldAsk = [fDefaults boolForKey: @"CheckQuitDownloading"] ? downloading > 0 : active > 0;
        if (shouldAsk)
        {
            NSString * message = active == 1
                ? @"There is an active transfer. Do you really want to quit?"
                : [NSString stringWithFormat:
                    @"There are %d active transfers. Do you really want to quit?", active];

            NSBeginAlertSheet(@"Confirm Quit", @"Quit", @"Cancel", nil, fWindow, self,
                                @selector(quitSheetDidEnd:returnCode:contextInfo:),
                                nil, nil, message);
            return NSTerminateLater;
        }
    }

    return NSTerminateNow;
}

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode
    contextInfo: (void *) contextInfo
{
    [NSApp stopModal];
    [NSApp replyToApplicationShouldTerminate: returnCode == NSAlertDefaultReturn];
}

- (void) applicationWillTerminate: (NSNotification *) notification
{
    //stop updating the interface
    [fTimer invalidate];
    
    //save history and stop running torrents
    [self updateTorrentHistory];
    [fTorrents makeObjectsPerformSelector: @selector(stopTransferForQuit)];
    
    //remember window states
    [fDefaults setBool: [[fInfoController window] isVisible] forKey: @"InfoVisible"];
    [fInfoController close];
    [fPrefsController close];
    [fWindow close];
    [self showStatusBar: NO animate: NO];
    
    //clear badge
    [fBadger clearBadge];

    //end quickly if updated version will open
    if (fUpdateInProgress)
        return;

    //wait for running transfers to stop (5 seconds timeout)
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
    
    [fTorrents release];
}

- (void) folderChoiceClosed: (NSOpenPanel *) openPanel returnCode: (int) code
    contextInfo: (Torrent *) torrent
{
    if (code == NSOKButton)
    {
        [torrent setDownloadFolder: [[openPanel filenames] objectAtIndex: 0]];
        [torrent update];
        [self attemptToStartAuto: torrent];
        [fTorrents addObject: torrent];
        
        [self torrentNumberChanged];
    }
    
    [NSApp stopModal];
}

- (void) application: (NSApplication *) sender openFiles: (NSArray *) filenames
{
    NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"], * torrentPath;
    Torrent * torrent;
    NSEnumerator * enumerator = [filenames objectEnumerator];
    while ((torrentPath = [enumerator nextObject]))
    {
        if (!(torrent = [[Torrent alloc] initWithPath: torrentPath lib: fLib]))
            continue;

        //add it to the "File > Open Recent" menu
        [[NSDocumentController sharedDocumentController]
            noteNewRecentDocumentURL: [NSURL fileURLWithPath: torrentPath]];

        if ([downloadChoice isEqualToString: @"Ask"])
        {
            NSOpenPanel * panel = [NSOpenPanel openPanel];

            [panel setPrompt: @"Select Download Folder"];
            [panel setAllowsMultipleSelection: NO];
            [panel setCanChooseFiles: NO];
            [panel setCanChooseDirectories: YES];

            [panel setMessage: [@"Select the download folder for "
                    stringByAppendingString: [torrentPath lastPathComponent]]];

            [panel beginSheetForDirectory: nil file: nil types: nil
                modalForWindow: fWindow modalDelegate: self didEndSelector:
                @selector( folderChoiceClosed:returnCode:contextInfo: )
                contextInfo: torrent];
            [NSApp runModalForWindow: panel];
        }
        else
        {
            NSString * folder = [downloadChoice isEqualToString: @"Constant"]
                ? [[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath]
                : [torrentPath stringByDeletingLastPathComponent];

            [torrent setDownloadFolder: folder];
            [torrent update];
            [self attemptToStartAuto: torrent];
            [fTorrents addObject: torrent];
        }
        
        [torrent release];
    }

    [self torrentNumberChanged];

    [self updateUI: nil];
    [self sortTorrents];
    [self updateTorrentHistory];
}

- (NSArray *) torrentsAtIndexes: (NSIndexSet *) indexSet
{
    if ([fTorrents respondsToSelector: @selector(objectsAtIndexes:)])
        return [fTorrents objectsAtIndexes: indexSet];
    else
    {
        NSMutableArray * torrents = [NSMutableArray arrayWithCapacity: [indexSet count]];
        unsigned int i;
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            [torrents addObject: [fTorrents objectAtIndex: i]];

        return torrents;
    }
}

- (void) torrentNumberChanged
{
    int count = [fTorrents count];
    [fTotalTorrentsField setStringValue: [NSString stringWithFormat:
        @"%d Transfer%s", count, count == 1 ? "" : "s"]];
}

//called on by applescript
- (void) open: (NSArray *) files
{
    [self performSelectorOnMainThread: @selector(openFromSheet:) withObject: files waitUntilDone: NO];
}

- (void) openShowSheet: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];
    NSArray * fileTypes = [NSArray arrayWithObject: @"torrent"];

    [panel setAllowsMultipleSelection: YES];
    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: NO];

    [panel beginSheetForDirectory: nil file: nil types: fileTypes
        modalForWindow: fWindow modalDelegate: self didEndSelector:
        @selector(openSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) openFromSheet: (NSArray *) filenames
{
    [self application: NSApp openFiles: filenames];
}

- (void) openSheetClosed: (NSOpenPanel *) panel returnCode: (int) code
    contextInfo: (void *) info
{
    if (code == NSOKButton)
        [self performSelectorOnMainThread: @selector(openFromSheet:)
                    withObject: [panel filenames] waitUntilDone: NO];
}

- (void) resumeTorrent: (id) sender
{
    [self resumeTorrentWithIndex: [fTableView selectedRowIndexes]];
}

- (void) resumeAllTorrents: (id) sender
{
    [self resumeTorrentWithIndex: [NSIndexSet indexSetWithIndexesInRange:
                                    NSMakeRange(0, [fTorrents count])]];
}

- (void) resumeTorrentWithIndex: (NSIndexSet *) indexSet
{
    unsigned int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [[fTorrents objectAtIndex: i] startTransfer];
    
    [self updateUI: nil];
    [self reloadInspector: nil];
    [self updateTorrentHistory];
}

- (void) stopTorrent: (id) sender
{
    [self stopTorrentWithIndex: [fTableView selectedRowIndexes]];
}

- (void) stopAllTorrents: (id) sender
{
    [self stopTorrentWithIndex: [NSIndexSet indexSetWithIndexesInRange:
                                    NSMakeRange(0, [fTorrents count])]];
}

- (void) stopTorrentWithIndex: (NSIndexSet *) indexSet
{
    //don't want any of these starting then stopping
    unsigned int i;
    Torrent * torrent;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
    {
        torrent = [fTorrents objectAtIndex: i];
        [torrent setWaitToStart: NO];
    }

    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [[fTorrents objectAtIndex: i] stopTransfer];
    
    [self updateUI: nil];
    [self reloadInspector: nil];
    [self updateTorrentHistory];
}

- (void) removeWithIndex: (NSIndexSet *) indexSet
        deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent
{
    NSArray * torrents = [[self torrentsAtIndexes: indexSet] retain];
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
    }

    BOOL shouldAsk = [fDefaults boolForKey: @"CheckRemoveDownloading"] ? downloading > 0 : active > 0;
    if (shouldAsk)
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
                        [[fTorrents objectAtIndex: [fTableView selectedRow]] name]];
            message = @"This transfer is active."
                        " Once removed, continuing the transfer will require the torrent file."
                        " Do you really want to remove it?";
        }
        else
        {
            title = [NSString stringWithFormat: @"Comfirm Removal of %d Transfers", selected];
            if (selected == active)
                message = [NSString stringWithFormat:
                    @"There are %d active transfers.", active];
            else
                message = [NSString stringWithFormat:
                    @"There are %d transfers (%d active).", selected, active];
            message = [message stringByAppendingString:
                @" Once removed, continuing the transfers will require the torrent files."
                " Do you really want to remove them?"];
        }

        NSBeginAlertSheet(title, @"Remove", @"Cancel", nil, fWindow, self,
            @selector(removeSheetDidEnd:returnCode:contextInfo:), nil, dict, message);
    }
    else
        [self confirmRemove: torrents deleteData: deleteData deleteTorrent: deleteTorrent];
}

- (void) removeSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode
                        contextInfo: (NSDictionary *) dict
{
    [NSApp stopModal];

    NSArray * torrents = [dict objectForKey: @"Torrents"];
    BOOL deleteData = [[dict objectForKey: @"DeleteData"] boolValue],
        deleteTorrent = [[dict objectForKey: @"DeleteTorrent"] boolValue];
    [dict release];
    
    if (returnCode == NSAlertDefaultReturn)
        [self confirmRemove: torrents deleteData: deleteData deleteTorrent: deleteTorrent];
    else
        [torrents release];
}

- (void) confirmRemove: (NSArray *) torrents
        deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent
{
    //don't want any of these starting then stopping
    NSEnumerator * enumerator = [torrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent setWaitToStart: NO];

    NSNumber * lowestOrderValue = [NSNumber numberWithInt: [torrents count]], * currentOrederValue;

    enumerator = [torrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        [torrent stopTransfer];

        if (deleteData)
            [torrent trashData];
        if (deleteTorrent)
            [torrent trashTorrent];
        
        //determine lowest order value
        currentOrederValue = [torrent orderValue];
        if ([lowestOrderValue compare: currentOrederValue] == NSOrderedDescending)
            lowestOrderValue = currentOrederValue;

        [torrent removeForever];
        [fTorrents removeObject: torrent];
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
    
    [self torrentNumberChanged];
    [fTableView deselectAll: nil];
    [self updateUI: nil];
    [self updateTorrentHistory];
}

- (void) removeNoDelete: (id) sender
{
    [self removeWithIndex: [fTableView selectedRowIndexes] deleteData: NO deleteTorrent: NO];
}

- (void) removeDeleteData: (id) sender
{
    [self removeWithIndex: [fTableView selectedRowIndexes] deleteData: YES deleteTorrent: NO];
}

- (void) removeDeleteTorrent: (id) sender
{
    [self removeWithIndex: [fTableView selectedRowIndexes] deleteData: NO deleteTorrent: YES];
}

- (void) removeDeleteBoth: (id) sender
{
    [self removeWithIndex: [fTableView selectedRowIndexes] deleteData: YES deleteTorrent: YES];
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
        [panel setCanSelectHiddenExtension: NO];
        [panel setExtensionHidden: NO];
        
        [panel beginSheetForDirectory: nil file: [torrent name]
            modalForWindow: fWindow modalDelegate: self didEndSelector:
            @selector( saveTorrentCopySheetClosed:returnCode:contextInfo: )
            contextInfo: torrents];
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
        [[fTorrents objectAtIndex: i] revealData];
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

- (void) updateUI: (NSTimer *) t
{
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
    {
        [torrent update];

        if ([torrent justFinished])
        {
            [self checkWaitingForFinished: torrent];
        
            //notifications
            [self notifyGrowl: [torrent name]];
            if (![fWindow isKeyWindow])
                fCompleted++;
        }
    }

    if ([fSortType isEqualToString: @"Progress"] || [fSortType isEqualToString: @"State"])
        [self sortTorrents];
    else
        [fTableView reloadData];
    
    //update the global DL/UL rates
    float downloadRate, uploadRate;
    tr_torrentRates(fLib, & downloadRate, & uploadRate);
    if (fStatusBarVisible)
    {
        [fTotalDLField setStringValue: [NSString stringForSpeed: downloadRate]];
        [fTotalULField setStringValue: [NSString stringForSpeed: uploadRate]];
    }

    if ([[fInfoController window] isVisible])
        [fInfoController updateInfoStats];

    //badge dock
    [fBadger updateBadgeWithCompleted: fCompleted
        uploadRate: uploadRate downloadRate: downloadRate];
}

- (void) updateTorrentHistory
{
    NSMutableArray * history = [NSMutableArray
        arrayWithCapacity: [fTorrents count]];

    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while( ( torrent = [enumerator nextObject] ) )
        [history addObject: [torrent history]];

    [fDefaults setObject: history forKey: @"History"];
    [fDefaults synchronize];
}

- (void) sortTorrents
{
    //remember selected rows if needed
    NSArray * selectedTorrents = nil;
    int numSelected = [fTableView numberOfSelectedRows];
    if (numSelected > 0 && numSelected < [fTorrents count])
        selectedTorrents = [self torrentsAtIndexes: [fTableView selectedRowIndexes]];

    NSSortDescriptor * nameDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"name" ascending: YES] autorelease],
                    * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"orderValue" ascending: YES] autorelease];

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
        NSSortDescriptor * dateDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"date" ascending: YES] autorelease];
    
        descriptors = [[NSArray alloc] initWithObjects: dateDescriptor, orderDescriptor, nil];
    }
    else
        descriptors = [[NSArray alloc] initWithObjects: orderDescriptor, nil];

    [fTorrents sortUsingDescriptors: descriptors];
    
    [descriptors release];
    
    [fTableView reloadData];
    
    //set selected rows if needed
    if (selectedTorrents)
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [selectedTorrents objectEnumerator];
        NSMutableIndexSet * indexSet = [[NSMutableIndexSet alloc] init];
        while ((torrent = [enumerator nextObject]))
            [indexSet addIndex: [fTorrents indexOfObject: torrent]];
        
        [fTableView selectRowIndexes: indexSet byExtendingSelection: NO];
        [indexSet release];
    }
}

- (void) setSort: (id) sender
{
    NSMenuItem * prevSortItem;
    if ([fSortType isEqualToString: @"Name"])
        prevSortItem = fNameSortItem;
    else if ([fSortType isEqualToString: @"State"])
        prevSortItem = fStateSortItem;
    else if ([fSortType isEqualToString: @"Progress"])
        prevSortItem = fProgressSortItem;
    else if ([fSortType isEqualToString: @"Date"])
        prevSortItem = fDateSortItem;
    else
        prevSortItem = fOrderSortItem;
    
    if (sender != prevSortItem)
    {
        [prevSortItem setState: NSOffState];
        [sender setState: NSOnState];

        [fSortType release];
        if (sender == fNameSortItem)
            fSortType = [[NSString alloc] initWithString: @"Name"];
        else if (sender == fStateSortItem)
            fSortType = [[NSString alloc] initWithString: @"State"];
        else if (sender == fProgressSortItem)
            fSortType = [[NSString alloc] initWithString: @"Progress"];
        else if (sender == fDateSortItem)
            fSortType = [[NSString alloc] initWithString: @"Date"];
        else
            fSortType = [[NSString alloc] initWithString: @"Order"];
           
        [fDefaults setObject: fSortType forKey: @"Sort"];
    }

    [self sortTorrents];
}

- (void) toggleSpeedLimit: (id) sender
{
    int state = [fSpeedLimitItem state] ? NSOffState : NSOnState;

    [fSpeedLimitItem setState: state];
    [fSpeedLimitDockItem setState: state];
    [fSpeedLimitButton setState: state];
    
    [fPrefsController enableSpeedLimit: state];
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
    [self checkWaitingForFinished: [notification object]];
}

- (void) checkWaitingForFinished: (Torrent *) finishedTorrent
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
        [self reloadInspector: nil];
        [self updateTorrentHistory];
    }
}

- (void) globalStartSettingChange: (NSNotification *) notification
{
    [self attemptToStartMultipleAuto: fTorrents];
    
    [self updateUI: nil];
    [self reloadInspector: nil];
    [self updateTorrentHistory];
}

- (void) torrentStartSettingChange: (NSNotification *) notification
{
    [self attemptToStartMultipleAuto: [notification object]];

    [self updateUI: nil];
    [self reloadInspector: nil];
    [self updateTorrentHistory];
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
        if ([torrent progress] >= 1.0)
            [torrent startTransfer];
        else
        {
            if ([torrent waitingToStart] && desiredActive > 0)
            {
                [torrent startTransfer];
                desiredActive--;
            }
        }
        
        [torrent update];
    }
}

- (void) reloadInspector: (NSNotification *) notification
{
    [fInfoController updateInfoForTorrents: [self torrentsAtIndexes: [fTableView selectedRowIndexes]]];
}

- (int) numberOfRowsInTableView: (NSTableView *) t
{
    return [fTorrents count];
}

- (void) tableView: (NSTableView *) t willDisplayCell: (id) cell
    forTableColumn: (NSTableColumn *) tableColumn row: (int) row
{
    [cell setTorrent: [fTorrents objectAtIndex: row]];
}

- (BOOL) tableView: (NSTableView *) tableView writeRowsWithIndexes: (NSIndexSet *) indexes
    toPasteboard: (NSPasteboard *) pasteboard
{
    if ([fSortType isEqualToString: @"Order"])
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
        if ([[[pasteboard propertyListForType: NSFilenamesPboardType]
            pathsMatchingExtensions: [NSArray arrayWithObject: @"torrent"]] count] > 0)
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
    else;
    
    return NSDragOperationNone;
}

- (BOOL) tableView: (NSTableView *) t acceptDrop: (id <NSDraggingInfo>) info
    row: (int) newRow dropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: NSFilenamesPboardType])
        [self application: NSApp openFiles: [[[info draggingPasteboard] propertyListForType: NSFilenamesPboardType]
            pathsMatchingExtensions: [NSArray arrayWithObject: @"torrent"]]];
    else
    {
        //remember selected rows if needed
        NSArray * selectedTorrents = nil;
        int numSelected = [fTableView numberOfSelectedRows];
        if (numSelected > 0 && numSelected < [fTorrents count])
            selectedTorrents = [self torrentsAtIndexes: [fTableView selectedRowIndexes]];
    
        NSIndexSet * indexes = [NSKeyedUnarchiver unarchiveObjectWithData:
                                [pasteboard dataForType: TORRENT_TABLE_VIEW_DATA_TYPE]];
        
        //move torrent in array 
        NSArray * movingTorrents = [[self torrentsAtIndexes: indexes] retain];
        [fTorrents removeObjectsInArray: movingTorrents];
        
        //determine the insertion index now that transfers to move have been removed
        int i, decrease = 0;
        for (i = [indexes firstIndex]; i < newRow && i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
            decrease++;
        
        //insert objects at new location
        for (i = 0; i < [movingTorrents count]; i++)
            [fTorrents insertObject: [movingTorrents objectAtIndex: i] atIndex: newRow - decrease + i];
        
        [movingTorrents release];
        
        //redo order values
        int low = [indexes firstIndex], high = [indexes lastIndex];
        if (newRow < low)
            low = newRow;
        else if (newRow > high + 1)
            high = newRow - 1;
        else;
        
        for (i = low; i <= high; i++)
            [[fTorrents objectAtIndex: i] setOrderValue: i];
        
        [fTableView reloadData];
        
        //set selected rows if needed
        if (selectedTorrents)
        {
            Torrent * torrent;
            NSEnumerator * enumerator = [selectedTorrents objectEnumerator];
            NSMutableIndexSet * indexSet = [[NSMutableIndexSet alloc] init];
            while ((torrent = [enumerator nextObject]))
                [indexSet addIndex: [fTorrents indexOfObject: torrent]];
            
            [fTableView selectRowIndexes: indexSet byExtendingSelection: NO];
            [indexSet release];
        }
    }
    
    return YES;
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [self reloadInspector: nil];
}

- (void) toggleStatusBar: (id) sender
{
    [self showStatusBar: !fStatusBarVisible animate: YES];
    [fDefaults setBool: fStatusBarVisible forKey: @"StatusBar"];
}

- (void) showStatusBar: (BOOL) show animate: (BOOL) animate
{
    if (show == fStatusBarVisible)
        return;

    NSRect frame = [fWindow frame];
    float heightChange = [fStatusBar frame].size.height;
    if (!show)
        heightChange *= -1;

    frame.size.height += heightChange;
    frame.origin.y -= heightChange;
        
    fStatusBarVisible = !fStatusBarVisible;
    
    [self updateUI: nil];
    
    //set views to not autoresize
    unsigned int statsMask = [fStatusBar autoresizingMask];
    unsigned int scrollMask = [fScrollView autoresizingMask];
    [fStatusBar setAutoresizingMask: 0];
    [fScrollView setAutoresizingMask: 0];
    
    [fWindow setFrame: frame display: YES animate: animate]; 
    
    //re-enable autoresize
    [fStatusBar setAutoresizingMask: statsMask];
    [fScrollView setAutoresizingMask: scrollMask];
    
    //change min size
    NSSize minSize = [fWindow contentMinSize];
    minSize.height += heightChange;
    [fWindow setContentMinSize: minSize];
}

- (void) toggleAdvancedBar: (id) sender
{
    [fAdvancedBarItem setState: ![fAdvancedBarItem state]];
    [fDefaults setBool: [fAdvancedBarItem state] forKey: @"UseAdvancedBar"];

    [fTableView display];
}

- (NSToolbarItem *) toolbar: (NSToolbar *) t itemForItemIdentifier:
    (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if( [ident isEqualToString: TOOLBAR_OPEN] )
    {
        [item setLabel: @"Open"];
        [item setPaletteLabel: @"Open Torrent Files"];
        [item setToolTip: @"Open torrent files"];
        [item setImage: [NSImage imageNamed: @"Open.png"]];
        [item setTarget: self];
        [item setAction: @selector( openShowSheet: )];
    }
    else if( [ident isEqualToString: TOOLBAR_REMOVE] )
    {
        [item setLabel: @"Remove"];
        [item setPaletteLabel: @"Remove Selected"];
        [item setToolTip: @"Remove selected transfers"];
        [item setImage: [NSImage imageNamed: @"Remove.png"]];
        [item setTarget: self];
        [item setAction: @selector( removeNoDelete: )];
    }
    else if( [ident isEqualToString: TOOLBAR_INFO] )
    {
        [item setLabel: @"Inspector"];
        [item setPaletteLabel: @"Show/Hide Inspector"];
        [item setToolTip: @"Display torrent inspector"];
        [item setImage: [NSImage imageNamed: @"Info.png"]];
        [item setTarget: self];
        [item setAction: @selector( showInfo: )];
    }
    else if( [ident isEqualToString: TOOLBAR_PAUSE_ALL] )
    {
        [item setLabel: @"Pause All"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Pause all transfers"];
        [item setImage: [NSImage imageNamed: @"PauseAll.png"]];
        [item setTarget: self];
        [item setAction: @selector( stopAllTorrents: )];
    }
    else if( [ident isEqualToString: TOOLBAR_RESUME_ALL] )
    {
        [item setLabel: @"Resume All"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Resume all transfers"];
        [item setImage: [NSImage imageNamed: @"ResumeAll.png"]];
        [item setTarget: self];
        [item setAction: @selector( resumeAllTorrents: )];
    }
    else if( [ident isEqualToString: TOOLBAR_PAUSE_SELECTED] )
    {
        [item setLabel: @"Pause"];
        [item setPaletteLabel: @"Pause Selected"];
        [item setToolTip: @"Pause selected transfers"];
        [item setImage: [NSImage imageNamed: @"PauseSelected.png"]];
        [item setTarget: self];
        [item setAction: @selector( stopTorrent: )];
    }
    else if( [ident isEqualToString: TOOLBAR_RESUME_SELECTED] )
    {
        [item setLabel: @"Resume"];
        [item setPaletteLabel: @"Resume Selected"];
        [item setToolTip: @"Resume selected transfers"];
        [item setImage: [NSImage imageNamed: @"ResumeSelected.png"]];
        [item setTarget: self];
        [item setAction: @selector( resumeTorrent: )];
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
            TOOLBAR_PAUSE_ALL, TOOLBAR_RESUME_ALL, TOOLBAR_INFO,
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
            TOOLBAR_INFO, nil];
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
            if ([[fTorrents objectAtIndex: i] isActive])
                return YES;
        return NO;
    }
    
    //enable resume item
    if ([ident isEqualToString: TOOLBAR_RESUME_SELECTED])
    {
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
            if ([[fTorrents objectAtIndex: i] isPaused])
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
        NSString * title = fStatusBarVisible ? @"Hide Status Bar" : @"Show Status Bar";
        if (![[menuItem title] isEqualToString: title])
                [menuItem setTitle: title];

        return canUseMenu;
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

    //enable reveal in finder
    if (action == @selector(revealFile:))
        return canUseMenu && [fTableView numberOfSelectedRows] > 0;

    //enable remove items
    if (action == @selector(removeNoDelete:) || action == @selector(removeDeleteData:)
        || action == @selector(removeDeleteTorrent:) || action == @selector(removeDeleteBoth:))
    {
        BOOL warning = NO,
            onlyDownloading = [fDefaults boolForKey: @"CheckRemoveDownloading"],
            canDelete = action != @selector(removeDeleteTorrent:) && action != @selector(removeDeleteBoth:);
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            torrent = [fTorrents objectAtIndex: i];
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

    //enable pause item
    if( action == @selector(stopTorrent:) )
    {
        if (!canUseMenu)
            return NO;
    
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            torrent = [fTorrents objectAtIndex: i];
            if ([torrent isActive])
                return YES;
        }
        return NO;
    }
    
    //enable resume item
    if( action == @selector(resumeTorrent:) )
    {
        if (!canUseMenu)
            return NO;
    
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            torrent = [fTorrents objectAtIndex: i];
            if ([torrent isPaused])
                return YES;
        }
        return NO;
    }
    
    //enable sort and advanced bar items
    if (action == @selector(setSort:) || (action == @selector(toggleAdvancedBar:)))
        return canUseMenu;
    
    //enable copy torrent file item
    if( action == @selector(copyTorrentFile:) )
    {
        return canUseMenu && [fTableView numberOfSelectedRows] > 0;
    }

    return YES;
}

- (void) sleepCallBack: (natural_t) messageType argument: (void *) messageArgument
{
    NSEnumerator * enumerator;
    Torrent * torrent;
    BOOL active;

    switch( messageType )
    {
        case kIOMessageSystemWillSleep:
            /* Close all connections before going to sleep and remember
               we should resume when we wake up */
            [fTorrents makeObjectsPerformSelector: @selector(sleep)];

            /* Wait for torrents to stop (5 seconds timeout) */
            NSDate * start = [NSDate date];
            enumerator = [fTorrents objectEnumerator];
            while( ( torrent = [enumerator nextObject] ) )
            {
                while( [[NSDate date] timeIntervalSinceDate: start] < 5 &&
                        ![torrent isPaused] )
                {
                    usleep( 100000 );
                    [torrent update];
                }
            }

            IOAllowPowerChange( fRootPort, (long) messageArgument );
            break;

        case kIOMessageCanSystemSleep:
            /* Prevent idle sleep unless all paused */
            active = NO;
            enumerator = [fTorrents objectEnumerator];
            while ((torrent = [enumerator nextObject]))
                if ([torrent isActive])
                {
                    active = YES;
                    break;
                }

            if (active)
                IOCancelPowerChange( fRootPort, (long) messageArgument );
            else
                IOAllowPowerChange( fRootPort, (long) messageArgument );
            break;

        case kIOMessageSystemHasPoweredOn:
            /* Resume download after we wake up */
            [fTorrents makeObjectsPerformSelector: @selector(wakeUp)];
            break;
    }
}

- (NSRect) windowWillUseStandardFrame: (NSWindow *) w defaultFrame: (NSRect) defaultFrame
{
    NSRect windowRect = [fWindow frame];
    float newHeight = windowRect.size.height - [fScrollView frame].size.height
        + [fTorrents count] * ([fTableView rowHeight] + [fTableView intercellSpacing].height) + 30.0;

    float minHeight = [fWindow minSize].height;
    if (newHeight < minHeight)
        newHeight = minHeight;

    windowRect.origin.y -= (newHeight - windowRect.size.height);
    windowRect.size.height = newHeight;

    return windowRect;
}

- (void) showMainWindow: (id) sender
{
    [fWindow makeKeyAndOrderFront: nil];
}

- (void) windowDidBecomeKey: (NSNotification *) notification
{
    fCompleted = 0;
}

- (void) linkHomepage: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: WEBSITE_URL]];
}

- (void) linkForums: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: FORUM_URL]];
}

- (void) notifyGrowl: (NSString * ) file
{
    if (!fHasGrowl)
        return;

    NSString * growlScript = [NSString stringWithFormat:
        @"tell application \"System Events\"\n"
         "  if exists application process \"GrowlHelperApp\" then\n"
         "    tell application \"GrowlHelperApp\"\n "
         "      notify with name \"Download Complete\""
         "        title \"Download Complete\""
         "        description \"%@\""
         "        application name \"Transmission\"\n"
         "    end tell\n"
         "  end if\n"
         "end tell", file];
    
    NSAppleScript * appleScript = [[NSAppleScript alloc] initWithSource: growlScript];
    NSDictionary * error;
    if (![appleScript executeAndReturnError: & error])
        NSLog(@"Growl notify failed");
    [appleScript release];
}

- (void) growlRegister
{
    if (!fHasGrowl)
        return;

    NSString * growlScript = [NSString stringWithFormat:
        @"tell application \"System Events\"\n"
         "  if exists application process \"GrowlHelperApp\" then\n"
         "    tell application \"GrowlHelperApp\"\n"
         "      register as application \"Transmission\" "
         "        all notifications {\"Download Complete\"}"
         "        default notifications {\"Download Complete\"}"
         "        icon of application \"Transmission\"\n"
         "    end tell\n"
         "  end if\n"
         "end tell"];

    NSAppleScript * appleScript = [[NSAppleScript alloc] initWithSource: growlScript];
    NSDictionary * error;
    if (![appleScript executeAndReturnError: & error])
        NSLog(@"Growl registration failed");
    [appleScript release];
}

- (void) checkUpdate: (id) sender
{
    [fPrefsController checkUpdate];
}

- (void) prepareForUpdate: (NSNotification *) notification
{
    fUpdateInProgress = YES;
}

@end
