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
    fLib      = tr_init();
    fTorrents = [[NSMutableArray alloc] initWithCapacity: 10];
    return self;
}

- (void) dealloc
{
    [fTorrents release];
    tr_close( fLib );
    [fAppIcon release];
    [super dealloc];
}

- (void) awakeFromNib
{
    fAppIcon = [[NSApp applicationIconImage] copy];

    [fPrefsController setPrefsWindow: fLib];
    fDefaults = [NSUserDefaults standardUserDefaults];

    //check advanced bar menu item
    [fAdvancedBarItem setState: [fDefaults
        boolForKey:@"UseAdvancedBar"] ? NSOnState : NSOffState];

    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Transmission Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: YES];
    [fToolbar setAutosavesConfiguration: YES];
    [fWindow setToolbar: fToolbar];
    [fWindow setDelegate: self];

    [fTableView setTorrents: fTorrents];
    [[fTableView tableColumnWithIdentifier: @"Torrent"] setDataCell:
        [[TorrentCell alloc] init]];

    [fTableView registerForDraggedTypes: [NSArray arrayWithObjects:
        NSFilenamesPboardType, NULL]];

    //Register for sleep notifications
    IONotificationPortRef  notify;
    io_object_t            anIterator;

    fRootPort = IORegisterForSystemPower( self, & notify, sleepCallBack,
                                          & anIterator);
    if (fRootPort)
    {
        CFRunLoopAddSource( CFRunLoopGetCurrent(),
                            IONotificationPortGetRunLoopSource( notify ),
                            kCFRunLoopCommonModes );
    }
    else
        NSLog( @"Could not IORegisterForSystemPower" );

    NSString * torrentPath, * downloadFolder, * paused;
    NSDictionary * dic;

    Torrent * torrent;
    NSEnumerator * enumerator = [[fDefaults arrayForKey: @"History"] objectEnumerator];
    while ((dic = [enumerator nextObject]))
    {
        torrentPath    = [dic objectForKey: @"TorrentPath"];
        downloadFolder = [dic objectForKey: @"DownloadFolder"];
        paused         = [dic objectForKey: @"Paused"];

        if (!torrentPath || !downloadFolder || !paused)
            continue;

        torrent = [[Torrent alloc] initWithPath: torrentPath lib: fLib];
        if( !torrent )
            continue;

        [fTorrents addObject: torrent];

        [torrent setFolder: downloadFolder];

        if ([paused isEqualToString: @"NO"])
            [torrent start];
    }

    //check and register Growl if it is installed for this user or all users
    NSFileManager * manager = [NSFileManager defaultManager];
    fHasGrowl = [manager fileExistsAtPath: GROWL_PATH]
                || [manager fileExistsAtPath: [[NSString stringWithFormat: @"~%@",
                GROWL_PATH] stringByExpandingTildeInPath]];
    [self growlRegister: self];

    //initialize badging
    fBadger = [[Badger alloc] init];

    //update the interface every 500 ms
    fCompleted = 0;
    [self updateUI: nil];
    fTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0 target: self
        selector: @selector( updateUI: ) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer
        forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer
        forMode: NSEventTrackingRunLoopMode];

    [self checkForUpdateTimer: nil];
    fUpdateTimer = [NSTimer scheduledTimerWithTimeInterval: 60.0
        target: self selector: @selector( checkForUpdateTimer: )
        userInfo: nil repeats: YES];
}

- (void) windowDidBecomeKey: (NSNotification *) n
{
    /* Reset the number of recently completed downloads */
    fCompleted = 0;
}

- (void) windowDidResize: (NSNotification *) n
{
    [fTableView sizeToFit];
}

- (BOOL) windowShouldClose: (id) sender
{
    [fWindow orderOut: nil];
    return NO;
}

- (BOOL) applicationShouldHandleReopen: (NSApplication *) app
    hasVisibleWindows: (BOOL) flag
{
    [self showMainWindow: nil];
    return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    int active = 0;
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while( ( torrent = [enumerator nextObject] ) )
    {
        if( [torrent isActive] )
        {
            active++;
        }
    }

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

- (void) quitSheetDidEnd:(NSWindow *)sheet returnCode:(int)returnCode
                        contextInfo:(void  *)contextInfo
{
    [NSApp stopModal];
    [NSApp replyToApplicationShouldTerminate:
        (returnCode == NSAlertDefaultReturn)];
}

- (void) applicationWillTerminate: (NSNotification *) notification
{
    NSEnumerator * enumerator;
    Torrent * torrent;

    // Stop updating the interface
    [fTimer invalidate];
    [fUpdateTimer invalidate];

    //clear badge
    [fBadger clearBadge];
    [fBadger release];

    // Save history
    [self updateTorrentHistory];

    // Stop running torrents
    enumerator = [fTorrents objectEnumerator];
    while( ( torrent = [enumerator nextObject] ) )
    {
        [torrent stop];
    }

    // Wait for torrents to stop (5 seconds timeout)
    NSDate * start = [NSDate date];
    while( [fTorrents count] )
    {
        torrent = [fTorrents objectAtIndex: 0];
        while( [[NSDate date] timeIntervalSinceDate: start] < 5 &&
                ![torrent isPaused] )
        {
            usleep( 100000 );
            [torrent update];
        }
        [fTorrents removeObject: torrent];
        [torrent release];
    }
}

- (void) showPreferenceWindow: (id) sender
{
    //place the window if not open
    if (![fPrefsWindow isVisible])
    {
        [fPrefsWindow center];
    }

    [fPrefsWindow makeKeyAndOrderFront:NULL];
}

- (void) folderChoiceClosed: (NSOpenPanel *) s returnCode: (int) code
    contextInfo: (Torrent *) torrent
{
    if (code == NSOKButton)
    {
        [fTorrents addObject: torrent];
        [torrent setFolder: [[s filenames] objectAtIndex: 0]];
        [torrent start];
    }
    else
    {
        [torrent release];
    }
    [NSApp stopModal];
}

- (void) application: (NSApplication *) sender
         openFiles: (NSArray *) filenames
{
    NSString * downloadChoice, * downloadFolder, * torrentPath;
    Torrent * torrent;

    downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
    downloadFolder = [fDefaults stringForKey: @"DownloadFolder"];

    NSEnumerator * enumerator = [filenames objectEnumerator];
    while ((torrentPath = [enumerator nextObject]))
    {
        torrent = [[Torrent alloc] initWithPath: torrentPath lib: fLib];
        if( !torrent )
            continue;

        /* Add it to the "File > Open Recent" menu */
        [[NSDocumentController sharedDocumentController]
            noteNewRecentDocumentURL: [NSURL fileURLWithPath: torrentPath]];

        if( [downloadChoice isEqualToString: @"Constant"] )
        {
            [fTorrents addObject: torrent];
            [torrent setFolder: [downloadFolder stringByExpandingTildeInPath]];
            [torrent start];
        }
        else if( [downloadChoice isEqualToString: @"Torrent"] )
        {
            [fTorrents addObject: torrent];
            [torrent setFolder: [torrentPath stringByDeletingLastPathComponent]];
            [torrent start];
        }
        else
        {
            NSOpenPanel * panel = [NSOpenPanel openPanel];

            [panel setPrompt: @"Select Download Folder"];
            [panel setAllowsMultipleSelection: NO];
            [panel setCanChooseFiles: NO];
            [panel setCanChooseDirectories: YES];

            if( [panel respondsToSelector: @selector(setMessage:)] )
                /* >= 10.3 */
                [panel setMessage: [NSString stringWithFormat:
                                    @"Select the download folder for %@",
                                    [torrentPath lastPathComponent]]];

            [panel beginSheetForDirectory: NULL file: NULL types: NULL
                modalForWindow: fWindow modalDelegate: self didEndSelector:
                @selector( folderChoiceClosed:returnCode:contextInfo: )
                contextInfo: torrent];
            [NSApp runModalForWindow: panel];
        }
    }

    [self updateUI: nil];
    [self updateTorrentHistory];
}

- (void) advancedChanged: (id) sender
{
    [fAdvancedBarItem setState: ![fAdvancedBarItem state]];
    [fDefaults setObject: [fAdvancedBarItem state] == NSOffState ? @"NO" : @"YES"
                forKey:@"UseAdvancedBar"];

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
    NSOpenPanel * panel;
    NSArray     * fileTypes;

    panel     = [NSOpenPanel openPanel];
    fileTypes = [NSArray arrayWithObject: @"torrent"];

    [panel setAllowsMultipleSelection: YES];
    [panel setCanChooseFiles:          YES];
    [panel setCanChooseDirectories:    NO];

    [panel beginSheetForDirectory: NULL file: NULL types: fileTypes
        modalForWindow: fWindow modalDelegate: self didEndSelector:
        @selector( openSheetClosed:returnCode:contextInfo: )
        contextInfo: NULL];
}

- (void) cantFindAName: (NSArray *) filenames
{
    [self application: NSApp openFiles: filenames];
}

- (void) openSheetClosed: (NSOpenPanel *) s returnCode: (int) code
    contextInfo: (void *) info
{
    if( code == NSOKButton )
        [self performSelectorOnMainThread: @selector(cantFindAName:)
                    withObject: [s filenames] waitUntilDone: NO];
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
    [self updateTorrentHistory];
}

- (void) removeTorrentWithIndex: (NSIndexSet *) indexSet
                  deleteTorrent: (BOOL) deleteTorrent
                     deleteData: (BOOL) deleteData
{
	int active = 0;
    unsigned int i;
    for (i = [indexSet firstIndex]; i != NSNotFound; i = [indexSet indexGreaterThanIndex: i])
		if ([[fTorrents objectAtIndex: i] isActive])
			active++;

    if( active > 0 && [fDefaults boolForKey: @"CheckRemove"] )
    {
        NSDictionary * dict = [NSDictionary dictionaryWithObjectsAndKeys:
            [fTableView selectedRowIndexes], @"Index",
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
        [self confirmRemoveTorrentWithIndex: indexSet
                deleteTorrent: deleteTorrent
                deleteData: deleteData];
    }
}

- (void) removeSheetDidEnd:(NSWindow *)sheet returnCode:(int)returnCode
                        contextInfo:(NSDictionary *)dict
{
    [NSApp stopModal];

    if( returnCode == NSAlertDefaultReturn )
    {
        [self confirmRemoveTorrentWithIndex: [dict objectForKey:@"Index"]
            deleteTorrent: [[dict objectForKey:@"DeleteTorrent"] boolValue]
            deleteData: [[dict objectForKey:@"DeleteData"] boolValue]];
    }

    [dict release];
}

- (void) confirmRemoveTorrentWithIndex: (NSIndexSet *) indexSet
            deleteTorrent: (BOOL) deleteTorrent
            deleteData: (BOOL) deleteData
{
    Torrent * torrent;
	unsigned int i;
	
    for (i = [indexSet lastIndex]; i != NSNotFound; i = [indexSet indexLessThanIndex: i])
	{
		torrent = [fTorrents objectAtIndex: i];
	
		[torrent stop];

		if( deleteData )
			[torrent trashData];
			
		if( deleteTorrent )
			[torrent trashTorrent];
		
		[fTorrents removeObject: torrent];
		[torrent release];
	}

    [self updateUI: nil];
    [self updateTorrentHistory];
}

- (void) removeTorrent: (id) sender
{
    [self removeTorrentWithIndex: [fTableView selectedRowIndexes] deleteTorrent: NO deleteData: NO ];
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

- (void) showInfo: (id) sender
{
    if( [fInfoPanel isVisible] )
    {
        [fInfoPanel close];
    }
    else
    {
        [fInfoPanel orderFront: sender];
    }
}

- (void) updateInfoPanel
{
	int numberSelected = [fTableView numberOfSelectedRows];
    if( numberSelected != 1 )
    {
        [fInfoImageView setImage: fAppIcon];
		
        [fInfoName setStringValue: numberSelected == 0
				? @"No Torrent Selected"
				: [[NSString stringWithInt: numberSelected]
					stringByAppendingString: @" Torrents Selected"]];
					
        [fInfoSize setStringValue: @""];
        [fInfoTracker setStringValue: @""];
        [fInfoAnnounce setStringValue: @""];
        [fInfoPieceSize setStringValue: @""];
        [fInfoPieces setStringValue: @""];
        [fInfoHash1 setStringValue: @""];
        [fInfoHash2 setStringValue: @""];
        [fInfoSeeders setStringValue: @""];
        [fInfoLeechers setStringValue: @""];
        [fInfoDownloaded setStringValue: @""];
        [fInfoUploaded setStringValue: @""];
        return;
    }
	
	int row = [fTableView selectedRow];

    Torrent * torrent = [fTorrents objectAtIndex: row];
    [fInfoImageView setImage: [torrent iconNonFlipped]];
    [fInfoName setStringValue: [torrent name]];
    [fInfoSize setStringValue: [NSString
        stringForFileSize: [torrent size]]];
    [fInfoTracker setStringValue: [torrent tracker]];
    [fInfoAnnounce setStringValue: [torrent announce]];
    [fInfoPieceSize setStringValue: [NSString
        stringForFileSize: [torrent pieceSize]]];
    [fInfoPieces setIntValue: [torrent pieceCount]];
    [fInfoHash1 setStringValue: [torrent hash1]];
    [fInfoHash2 setStringValue: [torrent hash2]];
    int seeders = [torrent seeders], leechers = [torrent leechers];
    [fInfoSeeders setStringValue: seeders < 0 ?
        @"?" : [NSString stringWithInt: seeders]];
    [fInfoLeechers setStringValue: leechers < 0 ?
        @"?" : [NSString stringWithInt: leechers]];
    [fInfoDownloaded setStringValue: [NSString
        stringForFileSize: [torrent downloaded]]];
    [fInfoUploaded setStringValue: [NSString
        stringForFileSize: [torrent uploaded]]];
}

- (void) updateUI: (NSTimer *) t
{
    float dl, ul;
    NSEnumerator * enumerator;
    Torrent * torrent;

    /* Update the TableView */
    enumerator = [fTorrents objectEnumerator];
    while( ( torrent = [enumerator nextObject] ) )
    {
        [torrent update];

        if( [torrent justFinished] )
        {
            /* Notifications */
            [self notifyGrowl: [torrent name]];
            if( ![fWindow isKeyWindow] )
            {
                fCompleted++;
            }
        }
    }
    [fTableView reloadData];

    //Update the global DL/UL rates
    tr_torrentRates( fLib, &dl, &ul );
    NSString * downloadRate = [NSString stringForSpeed: dl];
    NSString * uploadRate = [NSString stringForSpeed: ul];
    [fTotalDLField setStringValue: downloadRate];
    [fTotalULField setStringValue: uploadRate];

    [self updateInfoPanel];

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
    {
        [history addObject: [NSDictionary dictionaryWithObjectsAndKeys:
            [torrent path],                      @"TorrentPath",
            [torrent getFolder],                 @"DownloadFolder",
            [torrent isActive] ? @"NO" : @"YES", @"Paused",
            nil]];
    }

    [fDefaults setObject: history forKey: @"History"];
}

- (int) numberOfRowsInTableView: (NSTableView *) t
{
    return [fTorrents count];
}

- (id) tableView: (NSTableView *) t objectValueForTableColumn:
    (NSTableColumn *) tableColumn row: (int) rowIndex
{
    return nil;
}

- (void) tableView: (NSTableView *) t willDisplayCell: (id) cell
    forTableColumn: (NSTableColumn *) tableColumn row: (int) rowIndex
{
    [cell setTorrent: [fTorrents objectAtIndex: rowIndex]];

    if( OSX_VERSION >= 10.3 && [fWindow isKeyWindow] &&
					[fTableView isRowSelected: rowIndex] )
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

    [fTableView setDropRow: [fTorrents count]
        dropOperation: NSTableViewDropAbove];
    return NSDragOperationGeneric;
}

- (void) tableViewSelectionDidChange: (NSNotification *) n
{
    [self updateInfoPanel];
}

- (NSToolbarItem *) toolbar: (NSToolbar *) t itemForItemIdentifier:
    (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if( [ident isEqualToString: TOOLBAR_OPEN] )
    {
        [item setLabel: @"Open"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Open a torrent"];
        [item setImage: [NSImage imageNamed: @"Open.png"]];
        [item setTarget: self];
        [item setAction: @selector( openShowSheet: )];
    }
    else if( [ident isEqualToString: TOOLBAR_REMOVE] )
    {
        [item setLabel: @"Remove"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Remove torrent from list"];
        [item setImage: [NSImage imageNamed: @"Remove.png"]];
        [item setTarget: self];
        [item setAction: @selector( removeTorrent: )];
    }
    else if( [ident isEqualToString: TOOLBAR_INFO] )
    {
        [item setLabel: @"Info"];
        [item setPaletteLabel: [item label]];
        [item setToolTip: @"Information"];
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
            TOOLBAR_RESUME_ALL, TOOLBAR_PAUSE_ALL,
            TOOLBAR_INFO,
            NSToolbarSeparatorItemIdentifier,
            NSToolbarSpaceItemIdentifier,
            NSToolbarFlexibleSpaceItemIdentifier,
            NSToolbarCustomizeToolbarItemIdentifier,
            NULL];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) t
{
    return [NSArray arrayWithObjects:
            TOOLBAR_OPEN, TOOLBAR_REMOVE,
            NSToolbarSeparatorItemIdentifier,
            TOOLBAR_RESUME_ALL, TOOLBAR_PAUSE_ALL,
            NSToolbarFlexibleSpaceItemIdentifier,
            TOOLBAR_INFO,
            NULL];
}

- (void) runCustomizationPalette: (id) sender
{
    [fToolbar runCustomizationPalette:sender];
}

- (void) showHideToolbar: (id) sender
{
    [fWindow toggleToolbarShown:sender];
}

- (BOOL)validateToolbarItem:(NSToolbarItem *)toolbarItem
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

- (BOOL)validateMenuItem:(NSMenuItem *)menuItem
{
    SEL action = [menuItem action];

    //disable menus if customize sheet is active
    if ([fToolbar customizationPaletteIsRunning])
        return NO;

    //enable customize toolbar item
    if (action == @selector(showHideToolbar:))
    {
        [menuItem setTitle: [fToolbar isVisible] ? @"Hide Toolbar" : @"Show Toolbar"];
        return YES;
    }

    //enable show info
    if (action == @selector(showInfo:))
    {
        [menuItem setTitle: [fInfoPanel isVisible] ? @"Hide Info" : @"Show Info"];
        return YES;
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

    if (action == @selector(revealFromMenu:))
    {
        return [fTableView numberOfSelectedRows] > 0;
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
        if( active && [fDefaults boolForKey: @"CheckRemove"] )
        {
            if (![[menuItem title] hasSuffix:NS_ELLIPSIS])
                [menuItem setTitle:[[menuItem title] stringByAppendingString:NS_ELLIPSIS]];
        }
        else
        {
            if ([[menuItem title] hasSuffix:NS_ELLIPSIS])
                [menuItem setTitle:[[menuItem title] substringToIndex:[[menuItem title] length]-[NS_ELLIPSIS length]]];
        }
        return [fTableView numberOfSelectedRows] > 0;
    }

    //enable pause item
    if( action == @selector(stopTorrent:) )
    {
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
            enumerator = [fTorrents objectEnumerator];
            while( ( torrent = [enumerator nextObject] ) )
            {
                [torrent sleep];
            }

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
            enumerator = [fTorrents objectEnumerator];
            while( ( torrent = [enumerator nextObject] ) )
            {
                [torrent wakeUp];
            }
            break;
    }
}

- (NSRect) windowWillUseStandardFrame: (NSWindow *) w
    defaultFrame: (NSRect) defaultFrame
{
    NSRect rectWin, rectView;
    float  foo;

    rectWin  = [fWindow frame];
    rectView = [fScrollView frame];
    foo      = 10.0 - rectView.size.height + MAX( 1U, [fTorrents count] ) *
        ( [fTableView rowHeight] + [fTableView intercellSpacing].height );

    rectWin.size.height += foo;
    rectWin.origin.y    -= foo;

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

- (void) revealFromMenu: (id) sender
{
    Torrent * torrent;
    torrent = [fTorrents objectAtIndex: [fTableView selectedRow]];
    [torrent reveal];
}

- (void) checkForUpdate: (id) sender
{
    [self checkForUpdateAuto: NO];
}

- (void) checkForUpdateTimer: (NSTimer *) timer
{
    NSString * check = [fDefaults stringForKey: @"VersionCheck"];
    if( [check isEqualToString: @"Never"] )
        return;

    NSTimeInterval interval;
    if( [check isEqualToString: @"Daily"] )
        interval = 24 * 3600;
    else if( [check isEqualToString: @"Weekly"] )
        interval = 7 * 24 * 3600;
    else
        return;

    id lastObject = [fDefaults objectForKey: @"VersionCheckLast"];
    NSDate * lastDate = [lastObject isKindOfClass: [NSDate class]] ?
        lastObject : nil;
    if( lastDate )
    {
        NSTimeInterval actualInterval =
            [[NSDate date] timeIntervalSinceDate: lastDate];
        if( actualInterval > 0 && actualInterval < interval )
        {
            return;
        }
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
    NSDictionary * dict = [NSPropertyListSerialization
                            propertyListFromData: [sender resourceDataUsingCache: NO]
                            mutabilityOption: NSPropertyListImmutable
                            format: nil errorDescription: nil];

    //check if plist was actually found and contains a version
    NSString * webVersion = nil;
    if (dict)
        webVersion = [dict objectForKey: @"Version"];
    if (!webVersion)
    {
        if (!fCheckIsAutomatic)
        {
            if( OSX_VERSION >= 10.3 )
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
            else
                /* XXX */;
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

        if( OSX_VERSION >= 10.3 )
        {
            result = [currentSub compare: webSub options: NSNumericSearch];
            if (result != NSOrderedSame)
            {
                if (result == NSOrderedAscending)
                    webGreater = YES;
                break;
            }
        }
        else
            /* XXX */;
    }

    if (webGreater)
    {
        if( OSX_VERSION >= 10.3 )
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
        else
            /* XXX */;
    }
    else if (!fCheckIsAutomatic)
    {
        if( OSX_VERSION >= 10.3 )
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
        else
            /* XXX */;
    }
    else;
}

@end
