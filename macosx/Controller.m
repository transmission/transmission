/******************************************************************************
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
#import "StringAdditions.h"
#import "Utils.h"
#import "TorrentTableView.h"

#import "PrefsController.h"

#define TOOLBAR_OPEN        @"Toolbar Open"
#define TOOLBAR_REMOVE      @"Toolbar Remove"
#define TOOLBAR_INFO        @"Toolbar Info"
#define TOOLBAR_PAUSE_ALL   @"Toolbar Pause All"
#define TOOLBAR_RESUME_ALL  @"Toolbar Resume All"

#define WEBSITE_URL         @"http://transmission.m0k.org/"
#define FORUM_URL           @"http://transmission.m0k.org/forum/"
#define VERSION_PLIST_URL   @"http://transmission.m0k.org/version.plist"

#define GROWL_PATH  @"/Library/PreferencePanes/Growl.prefPane/Contents/Resources/GrowlHelperApp.app"

static void sleepCallBack( void * controller, io_service_t y,
        natural_t messageType, void * messageArgument )
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
    }
    return self;
}

- (void) dealloc
{
    [fTorrents release];
    
    [fInfoController release];
    
    tr_close( fLib );
    [super dealloc];
}

- (void) awakeFromNib
{
    [fPrefsController setPrefsWindow: fLib];
    fDefaults = [NSUserDefaults standardUserDefaults];

    fInfoController = [[InfoWindowController alloc] initWithWindowNibName: @"InfoWindow"];
    
    [fAdvancedBarItem setState: [fDefaults
        boolForKey: @"UseAdvancedBar"] ? NSOnState : NSOffState];

    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Transmission Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: YES];
    [fToolbar setAutosavesConfiguration: YES];
    [fWindow setToolbar: fToolbar];
    [fWindow setDelegate: self];
    
    fStatusBar = YES;
    if (![fDefaults boolForKey: @"StatusBar"])
        [self toggleStatusBar: nil];
    
    [fActionButton setToolTip: @"Shortcuts for performing special actions."];

    [fTableView setTorrents: fTorrents];
    [[fTableView tableColumnWithIdentifier: @"Torrent"] setDataCell:
        [[TorrentCell alloc] init]];

    [fTableView registerForDraggedTypes:
        [NSArray arrayWithObject: NSFilenamesPboardType]];

    //Register for sleep notifications
    IONotificationPortRef notify;
    io_object_t anIterator;
    if (fRootPort= IORegisterForSystemPower(self, & notify,
                                sleepCallBack, & anIterator))
    {
        CFRunLoopAddSource( CFRunLoopGetCurrent(),
                            IONotificationPortGetRunLoopSource( notify ),
                            kCFRunLoopCommonModes );
    }
    else
        NSLog( @"Could not IORegisterForSystemPower" );

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
    fSortType = [fDefaults stringForKey: @"Sort"];
    if ([fSortType isEqualToString: @"Name"])
        fCurrentSortItem = fNameSortItem;
    else if ([fSortType isEqualToString: @"State"])
        fCurrentSortItem = fStateSortItem;
    else
        fCurrentSortItem = fDateSortItem;
    [fCurrentSortItem setState: NSOnState];

    //check and register Growl if it is installed for this user or all users
    NSFileManager * manager = [NSFileManager defaultManager];
    fHasGrowl = [manager fileExistsAtPath: GROWL_PATH]
                || [manager fileExistsAtPath: [[NSString stringWithFormat: @"~%@",
                GROWL_PATH] stringByExpandingTildeInPath]];
    [self growlRegister: self];

    //initialize badging
    fBadger = [[Badger alloc] init];

    //timer to update the interface
    fCompleted = 0;
    [self updateUI: nil];
    fTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0 target: self
        selector: @selector( updateUI: ) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer
        forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer
        forMode: NSEventTrackingRunLoopMode];

    //timer to check for updates
    [self checkForUpdateTimer: nil];
    fUpdateTimer = [NSTimer scheduledTimerWithTimeInterval: 60.0
        target: self selector: @selector( checkForUpdateTimer: )
        userInfo: nil repeats: YES];
    
    [self sortTorrents];
    
    //show windows
    [fWindow makeKeyAndOrderFront: nil];
    if ([fDefaults boolForKey: @"InfoVisible"])
        [self showInfo: nil];
}

- (void) windowDidBecomeKey: (NSNotification *) n
{
    /* Reset the number of recently completed downloads */
    fCompleted = 0;
}

- (BOOL) applicationShouldHandleReopen: (NSApplication *) app
    hasVisibleWindows: (BOOL) flag
{
    [self showMainWindow: nil];
    return NO;
}

- (NSApplicationTerminateReply) applicationShouldTerminate: (NSApplication *) sender
{
    int active = 0;
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while( ( torrent = [enumerator nextObject] ) )
        if( [torrent isActive] )
            active++;

    if (active > 0 && [fDefaults boolForKey: @"CheckQuit"])
    {
        NSString * message = active == 1
            ? @"There is an active torrent. Do you really want to quit?"
            : [NSString stringWithFormat:
                @"There are %d active torrents. Do you really want to quit?",
                active];

        NSBeginAlertSheet(@"Confirm Quit",
                            @"Quit", @"Cancel", nil,
                            fWindow, self,
                            @selector(quitSheetDidEnd:returnCode:contextInfo:),
                            nil, nil, message);
        return NSTerminateLater;
    }

    return NSTerminateNow;
}

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode
                        contextInfo: (void *) contextInfo
{
    [NSApp stopModal];
    [NSApp replyToApplicationShouldTerminate:
        (returnCode == NSAlertDefaultReturn)];
}

- (void) applicationWillTerminate: (NSNotification *) notification
{
    // Stop updating the interface
    [fTimer invalidate];
    [fUpdateTimer invalidate];

    //clear badge
    [fBadger clearBadge];
    [fBadger release];
    
    //remember info window state
    [fDefaults setBool: [[fInfoController window] isVisible] forKey: @"InfoVisible"];

    //save history
    [self updateTorrentHistory];

    // Stop running torrents
    [fTorrents makeObjectsPerformSelector: @selector(stop)];

    // Wait for torrents to stop (5 seconds timeout)
    NSDate * start = [NSDate date];
    Torrent * torrent;
    while ([fTorrents count] > 0)
    {
        torrent = [fTorrents objectAtIndex: 0];
        while( [[NSDate date] timeIntervalSinceDate: start] < 5 &&
                ![torrent isPaused] )
        {
            usleep( 100000 );
            [torrent update];
        }
        [fTorrents removeObject: torrent];
    }
}

- (void) showPreferenceWindow: (id) sender
{
    if (![fPrefsWindow isVisible])
        [fPrefsWindow center];

    [fPrefsWindow makeKeyAndOrderFront: nil];
}

- (void) folderChoiceClosed: (NSOpenPanel *) s returnCode: (int) code
    contextInfo: (Torrent *) torrent
{
    if (code == NSOKButton)
    {
        [torrent setFolder: [[s filenames] objectAtIndex: 0]];
        if ([fDefaults boolForKey: @"AutoStartDownload"])
            [torrent start];
        [fTorrents addObject: torrent];
        
        [self torrentNumberChanged];
    }
    
    [NSApp stopModal];
}

- (void) application: (NSApplication *) sender
         openFiles: (NSArray *) filenames
{
    BOOL autoStart = [fDefaults boolForKey: @"AutoStartDownload"];
    
    NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
    NSString * torrentPath;
    Torrent * torrent;
    NSEnumerator * enumerator = [filenames objectEnumerator];
    while ((torrentPath = [enumerator nextObject]))
    {
        if (!(torrent = [[Torrent alloc] initWithPath: torrentPath lib: fLib]))
            continue;

        /* Add it to the "File > Open Recent" menu */
        [[NSDocumentController sharedDocumentController]
            noteNewRecentDocumentURL: [NSURL fileURLWithPath: torrentPath]];

        if ([downloadChoice isEqualToString: @"Ask"])
        {
            NSOpenPanel * panel = [NSOpenPanel openPanel];

            [panel setPrompt: @"Select Download Folder"];
            [panel setAllowsMultipleSelection: NO];
            [panel setCanChooseFiles: NO];
            [panel setCanChooseDirectories: YES];

            [panel setMessage: [NSString stringWithFormat:
                                @"Select the download folder for %@",
                                [torrentPath lastPathComponent]]];

            [panel beginSheetForDirectory: nil file: nil types: nil
                modalForWindow: fWindow modalDelegate: self didEndSelector:
                @selector( folderChoiceClosed:returnCode:contextInfo: )
                contextInfo: torrent];
            [NSApp runModalForWindow: panel];
        }
        else
        {
            NSString * folder = [downloadChoice isEqualToString: @"Constant"]
                                ? [[fDefaults stringForKey: @"DownloadFolder"]
                                        stringByExpandingTildeInPath]
                                : [torrentPath stringByDeletingLastPathComponent];

            [torrent setFolder: folder];
            if (autoStart)
                [torrent start];
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
    NSMutableArray * torrents = [NSMutableArray arrayWithCapacity: [indexSet count]];
    unsigned int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        [torrents addObject: [fTorrents objectAtIndex: i]];

    return torrents;
}

- (void) torrentNumberChanged
{
    int count = [fTorrents count];
    [fTotalTorrentsField setStringValue: [NSString stringWithFormat:
                @"%d Torrent%s", count, count == 1 ? "" : "s"]];
}

- (void) advancedChanged: (id) sender
{
    [fAdvancedBarItem setState: ![fAdvancedBarItem state]];
    [fDefaults setBool: [fAdvancedBarItem state] forKey: @"UseAdvancedBar"];

    [fTableView display];
}

//called on by applescript
- (void) open: (NSArray *) files
{
    [self performSelectorOnMainThread: @selector(cantFindAName:)
                withObject: files waitUntilDone: NO];
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
        @selector( openSheetClosed:returnCode:contextInfo: )
        contextInfo: nil];
}

- (void) cantFindAName: (NSArray *) filenames
{
    [self application: NSApp openFiles: filenames];
}

- (void) openSheetClosed: (NSOpenPanel *) panel returnCode: (int) code
    contextInfo: (void *) info
{
    if( code == NSOKButton )
        [self performSelectorOnMainThread: @selector(cantFindAName:)
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
    Torrent * torrent;
    unsigned int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
    {
        torrent = [fTorrents objectAtIndex: i];
        [torrent start];
    }
    
    [self updateUI: nil];
    [self sortTorrents];
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
    Torrent * torrent;
    unsigned int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
    {
        torrent = [fTorrents objectAtIndex: i];
        [torrent stop];
    }
    
    [self updateUI: nil];
    [self sortTorrents];
    [self updateTorrentHistory];
}

- (void) removeTorrentWithIndex: (NSIndexSet *) indexSet
                  deleteTorrent: (BOOL) deleteTorrent
                     deleteData: (BOOL) deleteData
{
    NSArray * torrents = [[self torrentsAtIndexes: indexSet] retain];
    int active = 0;

    Torrent * torrent;
    NSEnumerator * enumerator = [torrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        if ([torrent isActive])
            active++;

    if( active > 0 && [fDefaults boolForKey: @"CheckRemove"] )
    {
        NSDictionary * dict = [NSDictionary dictionaryWithObjectsAndKeys:
            torrents, @"Torrents",
            [NSNumber numberWithBool: deleteTorrent], @"DeleteTorrent",
            [NSNumber numberWithBool: deleteData], @"DeleteData",
            nil];
        [dict retain];

        NSString * title, * message;
        
        int selected = [fTableView numberOfSelectedRows];
        if (selected == 1)
        {
            title = [NSString stringWithFormat: @"Comfirm Removal of %@",
                        [[fTorrents objectAtIndex: [fTableView selectedRow]] name]];
            message = @"This torrent is active. Do you really want to remove it?";
        }
        else
        {
            title = [NSString stringWithFormat: @"Comfirm Removal of %d Torrents", active];
            if (selected == active)
                message = [NSString stringWithFormat:
                    @"There are %d active torrents. Do you really want to remove them?", active];
            else
                message = [NSString stringWithFormat:
                    @"There are %d torrents (%d active). Do you really want to remove them?", selected, active];
        }

        NSBeginAlertSheet(title,
            @"Remove", @"Cancel", nil, fWindow, self,
            @selector(removeSheetDidEnd:returnCode:contextInfo:),
            nil, dict, message);
    }
    else
    {
        [self confirmRemoveTorrents: torrents
                deleteTorrent: deleteTorrent
                deleteData: deleteData];
    }
}

- (void) removeSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode
                        contextInfo: (NSDictionary *) dict
{
    [NSApp stopModal];

    NSArray * torrents = [dict objectForKey: @"Torrents"];
    BOOL deleteTorrent = [[dict objectForKey: @"DeleteTorrent"] boolValue];
    BOOL deleteData = [[dict objectForKey: @"DeleteData"] boolValue];
    [dict release];
    
    if (returnCode == NSAlertDefaultReturn)
    {
        [self confirmRemoveTorrents: torrents
            deleteTorrent: deleteTorrent
            deleteData: deleteData];
    }
    else
        [torrents release];
}

- (void) confirmRemoveTorrents: (NSArray *) torrents
            deleteTorrent: (BOOL) deleteTorrent
            deleteData: (BOOL) deleteData
{
    Torrent * torrent;
    NSEnumerator * enumerator = [torrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        [torrent stop];

        if( deleteData )
            [torrent trashData];
            
        if( deleteTorrent )
            [torrent trashTorrent];

        [fTorrents removeObject: torrent];
    }
    [torrents release];
    
    [self torrentNumberChanged];

    [fTableView deselectAll: nil];
    [self updateUI: nil];
    [self updateTorrentHistory];
}

- (void) removeTorrent: (id) sender
{
    [self removeTorrentWithIndex: [fTableView selectedRowIndexes] deleteTorrent: NO deleteData: NO];
}

- (void) removeTorrentDeleteFile: (id) sender
{
    [self removeTorrentWithIndex: [fTableView selectedRowIndexes] deleteTorrent: YES deleteData: NO];
}

- (void) removeTorrentDeleteData: (id) sender
{
    [self removeTorrentWithIndex: [fTableView selectedRowIndexes] deleteTorrent: NO deleteData: YES];
}

- (void) removeTorrentDeleteBoth: (id) sender
{
    [self removeTorrentWithIndex: [fTableView selectedRowIndexes] deleteTorrent: YES deleteData: YES];
}

- (void) revealTorrent: (id) sender
{
    Torrent * torrent;
    NSIndexSet * indexSet = [fTableView selectedRowIndexes];
    unsigned int i;
    
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
    {
        torrent = [fTorrents objectAtIndex: i];
        [torrent reveal];
    }
}

- (void) updateUI: (NSTimer *) t
{
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while( ( torrent = [enumerator nextObject] ) )
    {
        [torrent update];

        if( [torrent justFinished] )
        {
            /* Notifications */
            [self notifyGrowl: [torrent name]];
            if( ![fWindow isKeyWindow] )
                fCompleted++;
                
            [self sortTorrents];
        }
    }
    
    [fTableView reloadData];
    
    //Update the global DL/UL rates
    float dl, ul;
    tr_torrentRates( fLib, &dl, &ul );
    NSString * downloadRate = [NSString stringForSpeed: dl];
    NSString * uploadRate = [NSString stringForSpeed: ul];
    [fTotalDLField setStringValue: downloadRate];
    [fTotalULField setStringValue: uploadRate];

    [self updateInfoStats];

    //badge dock
    [fBadger updateBadgeWithCompleted: fCompleted
        uploadRate: ul >= 0.1 && [fDefaults boolForKey: @"BadgeUploadRate"]
          ? [NSString stringForSpeedAbbrev: ul] : nil
        downloadRate: dl >= 0.1 && [fDefaults boolForKey: @"BadgeDownloadRate"]
          ? [NSString stringForSpeedAbbrev: dl] : nil];
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
}

- (void) showInfo: (id) sender
{
    if ([[fInfoController window] isVisible])
        [[fInfoController window] performClose: nil];
    else
    {
        [fInfoController updateInfoForTorrents: [self torrentsAtIndexes:
                                            [fTableView selectedRowIndexes]]];
        [[fInfoController window] orderFront: nil];
    }
}

- (void) updateInfo
{
    if ([[fInfoController window] isVisible])
        [fInfoController updateInfoForTorrents: [self torrentsAtIndexes:
                                            [fTableView selectedRowIndexes]]];
}

- (void) updateInfoStats
{
    if ([[fInfoController window] isVisible])
        [fInfoController updateInfoStatsForTorrents: [self torrentsAtIndexes:
                                            [fTableView selectedRowIndexes]]];
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
                    * dateDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                            @"date" ascending: YES] autorelease];

    NSArray * descriptors;
    if ([fSortType isEqualToString: @"Name"])
        descriptors = [[NSArray alloc] initWithObjects: nameDescriptor, dateDescriptor, nil];
    else if ([fSortType isEqualToString: @"State"])
    {
        NSSortDescriptor * stateDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"stateSortKey" ascending: NO] autorelease];
        descriptors = [[NSArray alloc] initWithObjects: stateDescriptor, nameDescriptor, dateDescriptor, nil];
    }
    else
        descriptors = [[NSArray alloc] initWithObjects: dateDescriptor, nameDescriptor, nil];

    [fTorrents sortUsingDescriptors: descriptors];
    [descriptors release];
    
    [fTableView reloadData];
    
    //set selected rows if needed
    if (selectedTorrents)
    {
        [fTableView deselectAll: nil];
        
        Torrent * torrent;
        NSEnumerator * enumerator = [selectedTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            [fTableView selectRow: [fTorrents indexOfObject: torrent] byExtendingSelection: YES];
    }
}

- (void) setSort: (id) sender
{
    [fCurrentSortItem setState: NSOffState];
    fCurrentSortItem = sender;
    [sender setState: NSOnState];

    if (sender == fNameSortItem)
        fSortType = @"Name";
    else if (sender == fStateSortItem)
        fSortType = @"State";
    else
        fSortType = @"Date";
       
    [fDefaults setObject: fSortType forKey: @"Sort"];

    [self sortTorrents];
}

- (int) numberOfRowsInTableView: (NSTableView *) t
{
    return [fTorrents count];
}

- (void) tableView: (NSTableView *) t willDisplayCell: (id) cell
    forTableColumn: (NSTableColumn *) tableColumn row: (int) rowIndex
{
    [cell setTorrent: [fTorrents objectAtIndex: rowIndex]];

    if( [fWindow isKeyWindow] && [fTableView isRowSelected: rowIndex] )
        [cell setTextColor: [NSColor whiteColor]];
    else
        [cell setTextColor: [NSColor blackColor]];
}

- (BOOL) tableView: (NSTableView *) t acceptDrop:
    (id <NSDraggingInfo>) info row: (int) row dropOperation:
    (NSTableViewDropOperation) operation
{
    [self application: NSApp openFiles: [[[info draggingPasteboard]
        propertyListForType: NSFilenamesPboardType]
        pathsMatchingExtensions: [NSArray arrayWithObject: @"torrent"]]];
    return YES;
}

- (NSDragOperation) tableView: (NSTableView *) t validateDrop:
    (id <NSDraggingInfo>) info proposedRow: (int) row
    proposedDropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];

    if (![[pasteboard types] containsObject: NSFilenamesPboardType]
            || [[[pasteboard propertyListForType: NSFilenamesPboardType]
                pathsMatchingExtensions: [NSArray arrayWithObject: @"torrent"]]
                count] == 0)
        return NSDragOperationNone;

    [fTableView setDropRow: [fTableView numberOfRows]
        dropOperation: NSTableViewDropAbove];
    return NSDragOperationGeneric;
}

- (void) tableViewSelectionDidChange: (NSNotification *) n
{
    [self updateInfo];
}

- (NSToolbarItem *) toolbar: (NSToolbar *) t itemForItemIdentifier:
    (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if( [ident isEqualToString: TOOLBAR_OPEN] )
    {
        [item setLabel: @"Open"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Open torrent files"];
        [item setImage: [NSImage imageNamed: @"Open.png"]];
        [item setTarget: self];
        [item setAction: @selector( openShowSheet: )];
    }
    else if( [ident isEqualToString: TOOLBAR_REMOVE] )
    {
        [item setLabel: @"Remove"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Remove selected torrents"];
        [item setImage: [NSImage imageNamed: @"Remove.png"]];
        [item setTarget: self];
        [item setAction: @selector( removeTorrent: )];
    }
    else if( [ident isEqualToString: TOOLBAR_INFO] )
    {
        [item setLabel: @"Info"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Display torrent info"];
        [item setImage: [NSImage imageNamed: @"Info.png"]];
        [item setTarget: self];
        [item setAction: @selector( showInfo: )];
    }
    else if( [ident isEqualToString: TOOLBAR_RESUME_ALL] )
    {
        [item setLabel: @"Resume All"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Resume all torrents"];
        [item setImage: [NSImage imageNamed: @"ResumeAll.png"]];
        [item setTarget: self];
        [item setAction: @selector( resumeAllTorrents: )];
    }
    else if( [ident isEqualToString: TOOLBAR_PAUSE_ALL] )
    {
        [item setLabel: @"Pause All"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Pause all torrents"];
        [item setImage: [NSImage imageNamed: @"PauseAll.png"]];
        [item setTarget: self];
        [item setAction: @selector( stopAllTorrents: )];
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
            TOOLBAR_PAUSE_ALL, TOOLBAR_RESUME_ALL,
            TOOLBAR_INFO,
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

- (void) toggleStatusBar: (id) sender
{
    fStatusBar = !fStatusBar;

    NSSize frameSize = [fScrollView frame].size;
    [fStats setHidden: !fStatusBar];
    
    if (fStatusBar)
        frameSize.height -= 18;
    else
        frameSize.height += 18;

    [fScrollView setFrameSize: frameSize];
    [fWindow display];
    
    [fDefaults setBool: fStatusBar forKey: @"StatusBar"];
}

- (BOOL) validateToolbarItem: (NSToolbarItem *) toolbarItem
{
    SEL action = [toolbarItem action];

    //enable remove item
    if (action == @selector(removeTorrent:))
        return [fTableView numberOfSelectedRows] > 0;

    Torrent * torrent;
    NSEnumerator * enumerator;

    //enable resume all item
    if (action == @selector(resumeAllTorrents:))
    {
        enumerator = [fTorrents objectEnumerator];
        while( ( torrent = [enumerator nextObject] ) )
            if( [torrent isPaused] )
                return YES;
        return NO;
    }

    //enable pause all item
    if (action == @selector(stopAllTorrents:))
    {
        enumerator = [fTorrents objectEnumerator];
        while( ( torrent = [enumerator nextObject] ) )
            if( [torrent isActive] )
                return YES;
        return NO;
    }

    return YES;
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];

    //only enable some menus if window is useable
    BOOL canUseWindow = [fWindow isKeyWindow] && ![fToolbar customizationPaletteIsRunning];

    //enable show info
    if (action == @selector(showInfo:))
    {
        [menuItem setTitle: [[fInfoController window] isVisible] ? @"Hide Info" : @"Show Info"];
        return YES;
    }
    
    //enable toggle toolbar
    if (action == @selector(toggleStatusBar:))
    {
        [menuItem setTitle: fStatusBar ? @"Hide Status Bar" : @"Show Status Bar"];
        return canUseWindow;
    }

    //enable resume all item
    if (action == @selector(resumeAllTorrents:))
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while( ( torrent = [enumerator nextObject] ) )
            if( [torrent isPaused] )
                return YES;
        return NO;
    }

    //enable pause all item
    if (action == @selector(stopAllTorrents:))
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while( ( torrent = [enumerator nextObject] ) )
            if( [torrent isActive] )
                return YES;
        return NO;
    }

    if (action == @selector(revealTorrent:))
    {
        return canUseWindow && [fTableView numberOfSelectedRows] > 0;
    }

    //enable remove items
    if (action == @selector(removeTorrent:)
        || action == @selector(removeTorrentDeleteFile:)
        || action == @selector(removeTorrentDeleteData:)
        || action == @selector(removeTorrentDeleteBoth:))
    {
        BOOL active = NO;
        Torrent * torrent;
        NSIndexSet * indexSet = [fTableView selectedRowIndexes];
        unsigned int i;
        
        for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
        {
            torrent = [fTorrents objectAtIndex: i];
            if ([torrent isActive])
            {
                active = YES;
                break;
            }
        }
    
        //append or remove ellipsis when needed
        NSString * title = [menuItem title];
        if (active && [fDefaults boolForKey: @"CheckRemove"])
        {
            if (![title hasSuffix: NS_ELLIPSIS])
                [menuItem setTitle: [title stringByAppendingString: NS_ELLIPSIS]];
        }
        else
        {
            if ([title hasSuffix: NS_ELLIPSIS])
                [menuItem setTitle:[title substringToIndex:
                            [title rangeOfString: NS_ELLIPSIS].location]];
        }
        return canUseWindow && [fTableView numberOfSelectedRows] > 0;
    }

    //enable pause item
    if( action == @selector(stopTorrent:) )
    {
        if (!canUseWindow)
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
        if (!canUseWindow)
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
    
    //enable resume item
    if (action == @selector(setSort:))
        return canUseWindow;

    return YES;
}

- (void) sleepCallBack: (natural_t) messageType argument:
                          (void *) messageArgument
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
            while( !active && ( torrent = [enumerator nextObject] ) )
                if( [torrent isActive] )
                    active = YES;

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

- (NSRect) windowWillUseStandardFrame: (NSWindow *) w
    defaultFrame: (NSRect) defaultFrame
{
    NSRect rectWin = [fWindow frame];
    float newHeight = rectWin.size.height - [fScrollView frame].size.height
            + [fTorrents count] * ([fTableView rowHeight] + [fTableView intercellSpacing].height);

    float minHeight = [fWindow minSize].height;
    if (newHeight < minHeight)
        newHeight = minHeight;

    rectWin.origin.y -= (newHeight - rectWin.size.height);
    rectWin.size.height = newHeight;

    return rectWin;
}

- (void) showMainWindow: (id) sender
{
    [fWindow makeKeyAndOrderFront: nil];
}

- (void) linkHomepage: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL
        URLWithString: WEBSITE_URL]];
}

- (void) linkForums: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL
        URLWithString: FORUM_URL]];
}

- (void) notifyGrowl: (NSString * ) file
{
    NSString * growlScript;
    NSAppleScript * appleScript;
    NSDictionary * error;

    if( !fHasGrowl )
        return;

    growlScript = [NSString stringWithFormat:
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
    appleScript = [[NSAppleScript alloc] initWithSource: growlScript];
    if( ![appleScript executeAndReturnError: &error] )
    {
        NSLog( @"Growl notify failed" );
    }
    [appleScript release];
}

- (void) growlRegister: (id) sender
{
    NSString * growlScript;
    NSAppleScript * appleScript;
    NSDictionary * error;

    if( !fHasGrowl )
        return;

    growlScript = [NSString stringWithFormat:
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

    appleScript = [[NSAppleScript alloc] initWithSource: growlScript];
    if( ![appleScript executeAndReturnError: &error] )
    {
        NSLog( @"Growl registration failed" );
    }
    [appleScript release];
}

- (void) checkForUpdate: (id) sender
{
    [self checkForUpdateAuto: NO];
}

- (void) checkForUpdateTimer: (NSTimer *) timer
{
    NSString * check = [fDefaults stringForKey: @"VersionCheck"];

    NSTimeInterval interval;
    if( [check isEqualToString: @"Daily"] )
        interval = 24 * 3600;
    else if( [check isEqualToString: @"Weekly"] )
        interval = 7 * 24 * 3600;
    else
        return;

    NSDate * lastDate = [fDefaults objectForKey: @"VersionCheckLast"];
    if( lastDate )
    {
        NSTimeInterval actualInterval = [[NSDate date]
                            timeIntervalSinceDate: lastDate];
        if( actualInterval > 0 && actualInterval < interval )
            return;
    }

    [self checkForUpdateAuto: YES];
    [fDefaults setObject: [NSDate date] forKey: @"VersionCheckLast"];
}

- (void) checkForUpdateAuto: (BOOL) automatic
{
    fCheckIsAutomatic = automatic;
    [[NSURL URLWithString: VERSION_PLIST_URL]
            loadResourceDataNotifyingClient: self usingCache: NO];
}

- (void) URLResourceDidFinishLoading: (NSURL *) sender
{
    //check if plist was actually found and contains a version
    NSDictionary * dict = [NSPropertyListSerialization
                            propertyListFromData: [sender resourceDataUsingCache: NO]
                            mutabilityOption: NSPropertyListImmutable
                            format: nil errorDescription: nil];
    NSString * webVersion;
    if (!dict || !(webVersion = [dict objectForKey: @"Version"]))
    {
        if (!fCheckIsAutomatic)
        {
            NSAlert * dialog = [[NSAlert alloc] init];
            [dialog addButtonWithTitle: @"OK"];
            [dialog setMessageText: @"Error checking for updates."];
            [dialog setInformativeText:
                    @"Transmission was not able to check the latest version available."];
            [dialog setAlertStyle: NSInformationalAlertStyle];

            [dialog runModal];
            [dialog release];
        }
        return;
    }

    NSString * currentVersion = [[[NSBundle mainBundle] infoDictionary]
                                objectForKey: (NSString *)kCFBundleVersionKey];

    NSEnumerator * webEnum = [[webVersion componentsSeparatedByString: @"."] objectEnumerator],
            * currentEnum = [[currentVersion componentsSeparatedByString: @"."] objectEnumerator];
    NSString * webSub, * currentSub;

    BOOL webGreater = NO;
    NSComparisonResult result;
    while ((webSub = [webEnum nextObject]))
    {
        if (!(currentSub = [currentEnum nextObject]))
        {
            webGreater = YES;
            break;
        }

        result = [currentSub compare: webSub options: NSNumericSearch];
        if (result != NSOrderedSame)
        {
            if (result == NSOrderedAscending)
                webGreater = YES;
            break;
        }
    }

    if (webGreater)
    {
        NSAlert * dialog = [[NSAlert alloc] init];
        [dialog addButtonWithTitle: @"Go to Website"];
        [dialog addButtonWithTitle:@"Cancel"];
        [dialog setMessageText: @"New version is available!"];
        [dialog setInformativeText: [NSString stringWithFormat:
            @"A newer version (%@) is available for download from the Transmission website.", webVersion]];
        [dialog setAlertStyle: NSInformationalAlertStyle];

        if ([dialog runModal] == NSAlertFirstButtonReturn)
            [self linkHomepage: nil];

        [dialog release];
    }
    else if (!fCheckIsAutomatic)
    {
        NSAlert * dialog = [[NSAlert alloc] init];
        [dialog addButtonWithTitle: @"OK"];
        [dialog setMessageText: @"No new versions are available."];
        [dialog setInformativeText: [NSString stringWithFormat:
            @"You are running the most current version of Transmission (%@).", currentVersion]];
        [dialog setAlertStyle: NSInformationalAlertStyle];

        [dialog runModal];
        [dialog release];
    }
    else;
}

@end
