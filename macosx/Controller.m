/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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

#import "NameCell.h"
#import "ProgressCell.h"
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

- (void) awakeFromNib
{
    [fWindow setContentMinSize: NSMakeSize( 400, 120 )];
    
    fHandle = tr_init();

    [fPrefsController setPrefsWindow: fHandle];
    fDefaults = [NSUserDefaults standardUserDefaults];
    
    [fInfoPanel setFrameAutosaveName:@"InfoPanel"];

    //check advanced bar menu item
    [fAdvancedBarItem setState: [fDefaults
        boolForKey:@"UseAdvancedBar"] ? NSOnState : NSOffState];
    
    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Transmission Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: YES];
    [fToolbar setAutosavesConfiguration: YES];
    [fWindow setToolbar: fToolbar];
    [fWindow setDelegate: self];

    NSTableColumn * tableColumn;
    NameCell * nameCell = [[NameCell alloc] init];
    ProgressCell * progressCell = [[ProgressCell alloc] init];

    tableColumn  = [fTableView tableColumnWithIdentifier: @"Name"];
    [tableColumn setDataCell: nameCell];

    tableColumn  = [fTableView tableColumnWithIdentifier: @"Progress"];
    [tableColumn setDataCell: progressCell];

    [fTableView setAutosaveTableColumns: YES];
    //[fTableView sizeToFit];

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
        printf( "Could not IORegisterForSystemPower\n" );

    NSString * torrentPath, * downloadFolder, * paused;
    NSDictionary * dic;

    NSEnumerator * enumerator = [[fDefaults arrayForKey: @"History"] objectEnumerator];
    while ((dic = [enumerator nextObject]))
    {
        torrentPath    = [dic objectForKey: @"TorrentPath"];
        downloadFolder = [dic objectForKey: @"DownloadFolder"];
        paused         = [dic objectForKey: @"Paused"];
            
        if (!torrentPath || !downloadFolder || !paused)
            continue;

        if (tr_torrentInit(fHandle, [torrentPath UTF8String]))
            continue;

        tr_torrentSetFolder( fHandle, tr_torrentCount( fHandle ) - 1,
                                [downloadFolder UTF8String] );

        if ([paused isEqualToString: @"NO"])
            tr_torrentStart( fHandle, tr_torrentCount( fHandle ) - 1 );
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
    fCount = 0;
    fDownloading = 0;
    fSeeding = 0;
    fCompleted = 0;
    fStat  = nil;
    fTimer = [NSTimer scheduledTimerWithTimeInterval: 1.0 target: self
        selector: @selector( updateUI: ) userInfo: NULL repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer
        forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer
        forMode: NSEventTrackingRunLoopMode];

    [self checkForUpdateTimer: nil];
    fUpdateTimer = [NSTimer scheduledTimerWithTimeInterval: 60.0
        target: self selector: @selector( checkForUpdateTimer: )
        userInfo: NULL repeats: YES];
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
    [fWindow orderOut: NULL];
    return NO;
}

- (BOOL) applicationShouldHandleReopen: (NSApplication *) app
    hasVisibleWindows: (BOOL) flag
{
    [self showMainWindow: NULL];
    return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    int active = fDownloading + fSeeding;
    if (active > 0 && [[NSUserDefaults standardUserDefaults] boolForKey: @"CheckQuit"])
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
    int i;
    
    // Stop updating the interface
    [fTimer invalidate];
    [fUpdateTimer invalidate];

    //clear badge
    [fBadger clearBadge];
    [fBadger release];                                                          

    // Save history and stop running torrents
    NSMutableArray * history = [NSMutableArray arrayWithCapacity: fCount];
    BOOL active;
    for( i = 0; i < fCount; i++ )
    {
        active = fStat[i].status &
            ( TR_STATUS_CHECK | TR_STATUS_DOWNLOAD | TR_STATUS_SEED );

        [history addObject: [NSDictionary dictionaryWithObjectsAndKeys:
            [NSString stringWithUTF8String: fStat[i].info.torrent],
            @"TorrentPath",
            [NSString stringWithUTF8String: tr_torrentGetFolder( fHandle, i )],
            @"DownloadFolder",
            active ? @"NO" : @"YES",
            @"Paused",
            NULL]];

        if( active )
        {
            tr_torrentStop( fHandle, i );
        }
    }
    [fDefaults setObject: history forKey: @"History"];

    // Wait for torrents to stop (5 seconds timeout)
    NSDate * start = [NSDate date];
    while( fCount > 0 )
    {
        while( [[NSDate date] timeIntervalSinceDate: start] < 5 &&
                !( fStat[0].status & TR_STATUS_PAUSE ) )
        {
            usleep( 100000 );
            tr_torrentStat( fHandle, &fStat );
        }
        tr_torrentClose( fHandle, 0 );
        fCount = tr_torrentStat( fHandle, &fStat );
    }

    tr_close( fHandle );
}

- (void) showPreferenceWindow: (id) sender
{
    //place the window if not open
    if (![fPrefsWindow isVisible])
    {
        NSRect  prefsFrame, screenRect;
        NSPoint point;

        prefsFrame = [fPrefsWindow frame];
        screenRect = [[NSScreen mainScreen] visibleFrame];
        point.x    = (screenRect.size.width - prefsFrame.size.width) * 0.5;
        point.y    = screenRect.origin.y + screenRect.size.height * 0.67 +
                     prefsFrame.size.height * 0.33;

        [fPrefsWindow setFrameTopLeftPoint: point];
    }
    
    [fPrefsWindow makeKeyAndOrderFront:NULL];
}

- (void) folderChoiceClosed: (NSOpenPanel *) s returnCode: (int) code
    contextInfo: (void *) info
{
    if (code == NSOKButton)
    {
        tr_torrentSetFolder( fHandle, tr_torrentCount( fHandle ) - 1,
                         [[[s filenames] objectAtIndex: 0] UTF8String] );
        tr_torrentStart( fHandle, tr_torrentCount( fHandle ) - 1 );
    }
    else
    {
        tr_torrentClose( fHandle, tr_torrentCount( fHandle ) - 1 );
    }
    [NSApp stopModal];
}

- (void) application: (NSApplication *) sender
         openFiles: (NSArray *) filenames
{
    NSString * downloadChoice, * downloadFolder, * torrentPath;

    downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
    downloadFolder = [fDefaults stringForKey: @"DownloadFolder"];

    NSEnumerator * enumerator = [filenames objectEnumerator];
    while ((torrentPath = [enumerator nextObject]))
    {
        if( tr_torrentInit( fHandle, [torrentPath UTF8String] ) )
            continue;

        /* Add it to the "File > Open Recent" menu */
        [[NSDocumentController sharedDocumentController]
            noteNewRecentDocumentURL: [NSURL fileURLWithPath: torrentPath]];

        if( [downloadChoice isEqualToString: @"Constant"] )
        {
            tr_torrentSetFolder( fHandle, tr_torrentCount( fHandle ) - 1,
                                 [[downloadFolder stringByExpandingTildeInPath] UTF8String] );
            tr_torrentStart( fHandle, tr_torrentCount( fHandle ) - 1 );
        }
        else if( [downloadChoice isEqualToString: @"Torrent"] )
        {
            tr_torrentSetFolder( fHandle, tr_torrentCount( fHandle ) - 1,
                [[torrentPath stringByDeletingLastPathComponent] UTF8String] );
            tr_torrentStart( fHandle, tr_torrentCount( fHandle ) - 1 );
        }
        else
        {
            NSOpenPanel * panel = [NSOpenPanel openPanel];
           
            [panel setPrompt: @"Select Download Folder"];
            [panel setMessage: [NSString stringWithFormat:
                                @"Select the download folder for %@",
                                [torrentPath lastPathComponent]]];
            [panel setAllowsMultipleSelection: NO];
            [panel setCanChooseFiles: NO];
            [panel setCanChooseDirectories: YES];

            [panel beginSheetForDirectory: NULL file: NULL types: NULL
                modalForWindow: fWindow modalDelegate: self didEndSelector:
                @selector( folderChoiceClosed:returnCode:contextInfo: )
                contextInfo: NULL];
            [NSApp runModalForWindow: panel];
        }
    }

    [self updateUI: NULL];
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
    fFilenames = [files retain];
    [self performSelectorOnMainThread: @selector(cantFindAName:)
                withObject: NULL waitUntilDone: NO];
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

- (void) cantFindAName: (id) sender
{
    [self application: NSApp openFiles: fFilenames];
    [fFilenames release];
}

- (void) openSheetClosed: (NSOpenPanel *) s returnCode: (int) code
    contextInfo: (void *) info
{
    if( code != NSOKButton )
    {
        return;
    }

    fFilenames = [[s filenames] retain];

    [self performSelectorOnMainThread: @selector(cantFindAName:)
                withObject: NULL waitUntilDone: NO];
}

- (void) resumeTorrent: (id) sender
{
    [self resumeTorrentWithIndex: [fTableView selectedRow]];
}

- (void) resumeAllTorrents: (id) sender
{
    int i;
    for ( i = 0; i < fCount; i++)
    {
        if ( fStat[i].status & ( TR_STATUS_STOPPING 
        | TR_STATUS_PAUSE | TR_STATUS_STOPPED ) )
        {
            [self resumeTorrentWithIndex: i];
        }
    }
}

- (void) resumeTorrentWithIndex: (int) idx
{
    tr_torrentStart( fHandle, idx );
    [self updateUI: NULL];
}

- (void) stopTorrent: (id) sender
{
    [self stopTorrentWithIndex: [fTableView selectedRow]];
}

- (void) stopAllTorrents: (id) sender
{
    int i;
    for ( i = 0; i < fCount; i++)
    {
        if ( fStat[i].status & ( TR_STATUS_CHECK 
        | TR_STATUS_DOWNLOAD | TR_STATUS_SEED) )
        {
            [self stopTorrentWithIndex: i];
        }
    }
}

- (void) stopTorrentWithIndex: (int) idx
{
    tr_torrentStop( fHandle, idx );
    [self updateUI: NULL];
}

- (void) removeTorrentWithIndex: (int) idx
                  deleteTorrent: (BOOL) deleteTorrent
                     deleteData: (BOOL) deleteData
{
    if ( fStat[idx].status & ( TR_STATUS_CHECK 
        | TR_STATUS_DOWNLOAD | TR_STATUS_SEED )  )
    {
        if ([fDefaults boolForKey: @"CheckRemove"])
        {
            NSDictionary * dict = [NSDictionary dictionaryWithObjectsAndKeys:
                        [NSString stringWithFormat: @"%d", idx], @"Index",
                        [NSString stringWithFormat: @"%d", deleteTorrent], @"DeleteTorrent",
                        [NSString stringWithFormat: @"%d", deleteData], @"DeleteData",
                        nil];
            [dict retain];
            
            NSBeginAlertSheet(@"Confirm Remove",
                                @"Remove", @"Cancel", nil,
                                fWindow, self,
                                @selector(removeSheetDidEnd:returnCode:contextInfo:),
                                NULL, dict, @"This torrent is active. Do you really want to remove it?");
            return;
        }
        //stop if not stopped
        else
            [self stopTorrentWithIndex:idx];
    }
    
    [self confirmRemoveTorrentWithIndex: idx
            deleteTorrent: deleteTorrent
            deleteData: deleteData];
}

- (void) removeSheetDidEnd:(NSWindow *)sheet returnCode:(int)returnCode
                        contextInfo:(NSDictionary  *)dict
{
    [NSApp stopModal];
    if (returnCode != NSAlertDefaultReturn)
    {
        [dict release];
        return;
    }
    
    int idx = [[dict objectForKey:@"Index"] intValue];
    
    [self stopTorrentWithIndex:idx];

    [self confirmRemoveTorrentWithIndex: idx
        deleteTorrent: [[dict objectForKey:@"DeleteTorrent"] intValue]
        deleteData: [[dict objectForKey:@"DeleteData"] intValue]];
    [dict release];
}
                     
- (void) confirmRemoveTorrentWithIndex: (int) idx
            deleteTorrent: (BOOL) deleteTorrent
            deleteData: (BOOL) deleteData
{
    if( deleteData )
    {
        [self finderTrash: [NSString stringWithFormat: @"%@/%@",
            [NSString stringWithUTF8String: fStat[idx].folder],
            [NSString stringWithUTF8String: fStat[idx].info.name]]];
    }
    if( deleteTorrent )
    {
        [self finderTrash: [NSString stringWithUTF8String:
            fStat[idx].info.torrent]];
    }
    
    tr_torrentClose( fHandle, idx );
    [self updateUI: NULL];
}

- (void) removeTorrent: (id) sender
{
    [self removeTorrentWithIndex: [fTableView selectedRow] deleteTorrent: NO deleteData: NO ];
}

- (void) removeTorrentDeleteFile: (id) sender
{
    [self removeTorrentWithIndex: [fTableView selectedRow] deleteTorrent: YES deleteData: NO];
}

- (void) removeTorrentDeleteData: (id) sender
{
    [self removeTorrentWithIndex: [fTableView selectedRow] deleteTorrent: NO deleteData: YES];
}

- (void) removeTorrentDeleteBoth: (id) sender
{
    [self removeTorrentWithIndex: [fTableView selectedRow] deleteTorrent: YES deleteData: YES];
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

- (void) updateUI: (NSTimer *) t
{
    float dl, ul;
    int row, i;

    //Update the NSTableView
    if (fStat)
        free(fStat);
        
    fCount = tr_torrentStat( fHandle, &fStat );
    fDownloading = 0;
    fSeeding = 0;
    [fTableView updateUI: fStat];

    //Update the global DL/UL rates
    tr_torrentRates( fHandle, &dl, &ul );
    NSString * downloadRate = [NSString stringForSpeed: dl];
    NSString * uploadRate = [NSString stringForSpeed: ul];
    [fTotalDLField setStringValue: downloadRate];
    [fTotalULField setStringValue: uploadRate];

    //Update DL/UL totals in the Info panel
    row = [fTableView selectedRow];
    if( row >= 0 )
    {
        [fInfoDownloaded setStringValue:
            [NSString stringForFileSize: fStat[row].downloaded]];
        [fInfoUploaded setStringValue:
            [NSString stringForFileSize: fStat[row].uploaded]];
    }
    
    //check if torrents have recently ended.
    for (i = 0; i < fCount; i++)
    {
        if (fStat[i].status & (TR_STATUS_CHECK | TR_STATUS_DOWNLOAD))
            fDownloading++;
        else if (fStat[i].status & TR_STATUS_SEED)
            fSeeding++;

        if( !tr_getFinished( fHandle, i ) )
            continue;

        fCompleted++;
        [self notifyGrowl: [NSString stringWithUTF8String: 
            fStat[i].info.name]];
        tr_setFinished( fHandle, i, 0 );
    }

    //badge dock
    NSUserDefaults * defaults = [NSUserDefaults standardUserDefaults];
    [fBadger updateBadgeWithCompleted: [defaults boolForKey: @"BadgeCompleted"] ? fCompleted : 0
                    uploadRate: ul >= 0.1 && [defaults boolForKey: @"BadgeUploadRate"] ? uploadRate : nil
                    downloadRate: dl >= 0.1 && [defaults boolForKey: @"BadgeDownloadRate"] ? downloadRate : nil];
}

- (int) numberOfRowsInTableView: (NSTableView *) t
{
    return fCount;
}

- (id) tableView: (NSTableView *) t objectValueForTableColumn:
    (NSTableColumn *) tableColumn row: (int) rowIndex
{
    return NULL;
}

- (void) tableView: (NSTableView *) t willDisplayCell: (id) cell
    forTableColumn: (NSTableColumn *) tableColumn row: (int) rowIndex
{
    BOOL w;

    w = [fWindow isKeyWindow] && rowIndex == [fTableView selectedRow];
    if( [[tableColumn identifier] isEqualToString: @"Name"] )
    {
        [(NameCell *) cell setStat: &fStat[rowIndex] whiteText: w];
    }
    else if( [[tableColumn identifier] isEqualToString: @"Progress"] )
    {
        [(ProgressCell *) cell setStat: &fStat[rowIndex] whiteText: w];
    }
}

- (BOOL) tableView: (NSTableView *) t acceptDrop:
    (id <NSDraggingInfo>) info row: (int) row dropOperation:
    (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard;

    pasteboard = [info draggingPasteboard];
    if( ![[pasteboard types] containsObject: NSFilenamesPboardType] )
    {
        return NO;
    }

    [self application: NSApp openFiles:
        [pasteboard propertyListForType: NSFilenamesPboardType]];

    return YES;
}

- (NSDragOperation) tableView: (NSTableView *) t validateDrop:
    (id <NSDraggingInfo>) info proposedRow: (int) row
    proposedDropOperation: (NSTableViewDropOperation) operation
{
    return NSDragOperationGeneric;
}

- (void) tableViewSelectionDidChange: (NSNotification *) n
{
    int row = [fTableView selectedRow];

    if( row < 0 )
    {
        [fInfoTitle      setStringValue: @"No torrent selected"];
        [fInfoTracker    setStringValue: @""];
        [fInfoAnnounce   setStringValue: @""];
        [fInfoSize       setStringValue: @""];
        [fInfoPieces     setStringValue: @""];
        [fInfoPieceSize  setStringValue: @""];
        [fInfoFolder     setStringValue: @""];
        [fInfoDownloaded setStringValue: @""];
        [fInfoUploaded   setStringValue: @""];
        [fInfoSeeders    setStringValue: @""];
        [fInfoLeechers   setStringValue: @""];
        return;
    }

    /* Update info window */
    [fInfoTitle setStringValue: [NSString stringWithUTF8String:
        fStat[row].info.name]];
    [fInfoTracker setStringValue: [NSString stringWithFormat:
        @"%s:%d", fStat[row].info.trackerAddress, fStat[row].info.trackerPort]];
    [fInfoAnnounce setStringValue: [NSString stringWithCString:
        fStat[row].info.trackerAnnounce]];
    [fInfoSize setStringValue:
        [NSString stringForFileSize: fStat[row].info.totalSize]];
    [fInfoPieces setStringValue: [NSString stringWithFormat: @"%d",
        fStat[row].info.pieceCount]];
    [fInfoPieceSize setStringValue:
        [NSString stringForFileSize: fStat[row].info.pieceSize]];
    [fInfoFolder setStringValue: [[NSString stringWithUTF8String:
        tr_torrentGetFolder( fHandle, row )] lastPathComponent]];
        
    if ( fStat[row].seeders == -1 ) {
        [fInfoSeeders setStringValue: [NSString stringWithUTF8String: "?"]];
    } else {
        [fInfoSeeders setStringValue: [NSString stringWithFormat: @"%d",
            fStat[row].seeders]];
    }
    if ( fStat[row].leechers == -1 ) {
        [fInfoLeechers setStringValue: [NSString stringWithUTF8String: "?"]];
    } else {
        [fInfoLeechers setStringValue: [NSString stringWithFormat: @"%d",
            fStat[row].leechers]];
    }
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
        return NULL;
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
        return [fTableView selectedRow] >= 0;
        

    //enable resume all item
    if (action == @selector(resumeAllTorrents:))
        return fCount > fDownloading + fSeeding;

    //enable pause all item
    if (action == @selector(stopAllTorrents:))
        return fDownloading > 0 || fSeeding > 0;                                
    
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
        return fCount > fDownloading + fSeeding;

    //enable pause all item
    if (action == @selector(stopAllTorrents:))
        return fDownloading > 0 || fSeeding > 0;                                
    
    int row = [fTableView selectedRow];
        
    //enable remove items
    if (action == @selector(removeTorrent:)
        || action == @selector(removeTorrentDeleteFile:)
        || action == @selector(removeTorrentDeleteData:)
        || action == @selector(removeTorrentDeleteBoth:))
    {
        //append or remove ellipsis when needed
        if (row >= 0 && fStat[row].status & ( TR_STATUS_CHECK | TR_STATUS_DOWNLOAD)
                    && [[fDefaults stringForKey: @"CheckRemove"] isEqualToString:@"YES"])
        {
            if (![[menuItem title] hasSuffix:NS_ELLIPSIS])
                [menuItem setTitle:[[menuItem title] stringByAppendingString:NS_ELLIPSIS]];
        }
        else
        {
            if ([[menuItem title] hasSuffix:NS_ELLIPSIS])
                [menuItem setTitle:[[menuItem title] substringToIndex:[[menuItem title] length]-[NS_ELLIPSIS length]]];
        }
        return row >= 0;
    }
    
    //enable reveal in finder item
    if (action == @selector(revealFromMenu:))
        return row >= 0;
        
    //enable and change pause / remove item
    if (action == @selector(resumeTorrent:) || action == @selector(stopTorrent:))
    {
        if (row >= 0 && fStat[row].status & TR_STATUS_PAUSE)
        {
            [menuItem setTitle: @"Resume"];
            [menuItem setAction: @selector( resumeTorrent: )];
        }
        else
        {
            [menuItem setTitle: @"Pause"];
            [menuItem setAction: @selector( stopTorrent: )];
        }
        return row >= 0;
    }
    
    return YES;
}

- (void) sleepCallBack: (natural_t) messageType argument:
                          (void *) messageArgument
{
    int i;

    switch( messageType )
    {
        case kIOMessageSystemWillSleep:
            /* Close all connections before going to sleep and remember
               we should resume when we wake up */
            for( i = 0; i < fCount; i++ )
            {
                if( fStat[i].status & ( TR_STATUS_CHECK |
                        TR_STATUS_DOWNLOAD | TR_STATUS_SEED ) )
                {
                    tr_torrentStop( fHandle, i );
                    fResumeOnWake[i] = 1;
                }
                else
                {
                    fResumeOnWake[i] = 0;
                }
            }

            /* TODO: wait a few seconds to let the torrents
               stop properly */
            
            IOAllowPowerChange( fRootPort, (long) messageArgument );
            break;

        case kIOMessageCanSystemSleep:
            /* Prevent idle sleep unless all paused */
            if (fDownloading > 0 || fSeeding > 0)
                IOCancelPowerChange( fRootPort, (long) messageArgument );
            else
                IOAllowPowerChange( fRootPort, (long) messageArgument );
            break;

        case kIOMessageSystemHasPoweredOn:
            /* Resume download after we wake up */
            for( i = 0; i < fCount; i++ )
            {
                if( fResumeOnWake[i] )
                {
                    tr_torrentStart( fHandle, i );
                }
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
    foo      = 25.0 + MAX( 1, fCount ) * ( [fTableView rowHeight] +
                 [fTableView intercellSpacing].height ) -
                 rectView.size.height;

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
        printf( "Growl notify failed\n" );
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
        printf( "Growl registration failed\n" );
    }
    [appleScript release];
}

- (void) revealFromMenu: (id) sender
{
    int row = [fTableView selectedRow];
    if (row >= 0)
    {
        [self finderReveal: [NSString stringWithFormat: @"%@/%@",
            [NSString stringWithUTF8String: fStat[row].folder],
            [NSString stringWithUTF8String: fStat[row].info.name]]];
    }
}

- (void) finderReveal: (NSString *) path
{
    NSString * string;
    NSAppleScript * appleScript;
    NSDictionary * error;
    
    string = [NSString stringWithFormat:
        @"tell application \"Finder\"\n"
         "  activate\n"
         "  reveal (POSIX file \"%@\")\n"
         "end tell", path];

    appleScript = [[NSAppleScript alloc] initWithSource: string];
    if( ![appleScript executeAndReturnError: &error] )
    {
        printf( "finderReveal failed\n" );
    }
    [appleScript release];
}

- (void) finderTrash: (NSString *) path
{
    NSString * string;
    NSAppleScript * appleScript;
    NSDictionary * error;

    string = [NSString stringWithFormat:
        @"tell application \"Finder\"\n"
         "  move (POSIX file \"%@\") to trash\n"
         "end tell", path];

    appleScript = [[NSAppleScript alloc] initWithSource: string];
    if( ![appleScript executeAndReturnError: &error] )
    {
        printf( "finderTrash failed\n" );
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
