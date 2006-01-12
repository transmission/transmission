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

#include <IOKit/IOMessage.h>

#include "Controller.h"
#include "NameCell.h"
#include "ProgressCell.h"
#include "Utils.h"

#define TOOLBAR_OPEN   @"Toolbar Open"
#define TOOLBAR_RESUME @"Toolbar Resume"
#define TOOLBAR_STOP   @"Toolbar Stop"
#define TOOLBAR_REMOVE @"Toolbar Remove"
#define TOOLBAR_INFO   @"Toolbar Info"

static void sleepCallBack( void * controller, io_service_t y,
        natural_t messageType, void * messageArgument )
{
    Controller * c = controller;
    [c sleepCallBack: messageType argument: messageArgument];
}

@implementation Controller

- (void) enableToolbarItem: (NSString *) ident flag: (BOOL) e
{
    NSArray * array = [fToolbar items];
    NSToolbarItem * item;

    if( [ident isEqualToString: TOOLBAR_OPEN] )
    {
        item = [array objectAtIndex: 0];
        [item setAction: e ? @selector( openShowSheet: ) : NULL];
    }
    else if( [ident isEqualToString: TOOLBAR_RESUME] )
    {
        item = [array objectAtIndex: 1];
        [item setAction: e ? @selector( resumeTorrent: ) : NULL];
    }
    else if( [ident isEqualToString: TOOLBAR_STOP] )
    {
        item = [array objectAtIndex: 2];
        [item setAction: e ? @selector( stopTorrent: ) : NULL];
    }
    else if( [ident isEqualToString: TOOLBAR_REMOVE] )
    {
        item = [array objectAtIndex: 3];
        [item setAction: e ? @selector( removeTorrent: ) : NULL];
    }
    else if( [ident isEqualToString: TOOLBAR_INFO] )
    {
        item = [array objectAtIndex: 5];
        [item setAction: e ? @selector( showInfo: ) : NULL];
    }
}

- (void) updateToolbar
{
    int row = [fTableView selectedRow];

    [self enableToolbarItem: TOOLBAR_RESUME flag: NO];
    [self enableToolbarItem: TOOLBAR_STOP   flag: NO];
    [self enableToolbarItem: TOOLBAR_REMOVE flag: NO];

    if( row < 0 )
    {
        return;
    }

    if( fStat[row].status &
        ( TR_STATUS_CHECK | TR_STATUS_DOWNLOAD | TR_STATUS_SEED ) )
    {
        [self enableToolbarItem: TOOLBAR_STOP   flag: YES];
    }
    else
    {
        [self enableToolbarItem: TOOLBAR_RESUME flag: YES];
        [self enableToolbarItem: TOOLBAR_REMOVE flag: YES];
    }
}

- (void) awakeFromNib
{
    fHandle = tr_init();

    [fPrefsController setHandle: fHandle];

    [fWindow setContentMinSize: NSMakeSize( 400, 120 )];

    /* Check or uncheck menu item in respect to current preferences */
    [fAdvancedBarItem setState: [[NSUserDefaults standardUserDefaults]
        boolForKey:@"UseAdvancedBar"] ? NSOnState : NSOffState];
    
    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Transmission Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: YES];
    [fToolbar setAutosavesConfiguration: YES];
    [fWindow  setToolbar:  fToolbar];
    [fWindow  setDelegate: self];

    [self enableToolbarItem: TOOLBAR_OPEN   flag: YES];
    [self enableToolbarItem: TOOLBAR_RESUME flag: NO];
    [self enableToolbarItem: TOOLBAR_STOP   flag: NO];
    [self enableToolbarItem: TOOLBAR_REMOVE flag: NO];
    [self enableToolbarItem: TOOLBAR_INFO   flag: YES];

    [fTableView setDataSource: self];
    [fTableView setDelegate:   self];

    NSTableColumn * tableColumn;
    NameCell      * nameCell;
    ProgressCell  * progressCell;

    nameCell     = [[NameCell     alloc] init];
    progressCell = [[ProgressCell alloc] init];
    tableColumn  = [fTableView tableColumnWithIdentifier: @"Name"];
    [tableColumn setDataCell: nameCell];
    [tableColumn setMinWidth: 10.0];
    [tableColumn setMaxWidth: 3000.0];

    tableColumn  = [fTableView tableColumnWithIdentifier: @"Progress"];
    [tableColumn setDataCell: progressCell];
    [tableColumn setMinWidth: 134.0];
    [tableColumn setMaxWidth: 134.0];

    [fTableView  sizeToFit];

    [fTableView registerForDraggedTypes: [NSArray arrayWithObjects:
        NSFilenamesPboardType, NULL]];

    IONotificationPortRef  notify;
    io_object_t            anIterator;

    /* Register for sleep notifications */
    fRootPort = IORegisterForSystemPower( self, &notify, sleepCallBack,
                                          &anIterator);
    if( !fRootPort )
    {
        printf( "Could not IORegisterForSystemPower\n" );
    }
    else
    {
        CFRunLoopAddSource( CFRunLoopGetCurrent(),
                            IONotificationPortGetRunLoopSource( notify ),
                            kCFRunLoopCommonModes );
    }

    NSUserDefaults * defaults = [NSUserDefaults standardUserDefaults];

    NSArray * history = [defaults arrayForKey: @"History"];
    if( history )
    {
        unsigned i;
        NSDictionary * dic;
        NSString * torrentPath, * downloadFolder, * paused;

        for( i = 0; i < [history count]; i++ )
        {
            dic = [history objectAtIndex: i];

            torrentPath    = [dic objectForKey: @"TorrentPath"];
            downloadFolder = [dic objectForKey: @"DownloadFolder"];
            paused         = [dic objectForKey: @"Paused"];

            if( !torrentPath || !downloadFolder || !paused )
            {
                continue;
            }

            if( tr_torrentInit( fHandle, [torrentPath UTF8String] ) )
            {
                continue;
            }

            tr_torrentSetFolder( fHandle, tr_torrentCount( fHandle ) - 1,
                                    [downloadFolder UTF8String] );

            if( [paused isEqualToString: @"NO"] )
            {
                tr_torrentStart( fHandle, tr_torrentCount( fHandle ) - 1 );
            }
        }
    }

    /* Update the interface every 500 ms */
    fCount = 0;
    fStat  = NULL;
    fTimer = [NSTimer scheduledTimerWithTimeInterval: 0.5 target: self
        selector: @selector( updateUI: ) userInfo: NULL repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer
        forMode: NSModalPanelRunLoopMode];
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

- (NSApplicationTerminateReply) applicationShouldTerminate:
    (NSApplication *) app
{
    NSMutableArray * history = [NSMutableArray
        arrayWithCapacity: TR_MAX_TORRENT_COUNT];
    int i;

    /* Stop updating the interface */
    [fTimer invalidate];

    /* Save history and stop running torrents */
    for( i = 0; i < fCount; i++ )
    {
        [history addObject: [NSDictionary dictionaryWithObjectsAndKeys:
            [NSString stringWithUTF8String: fStat[i].info.torrent],
            @"TorrentPath",
            [NSString stringWithUTF8String: tr_torrentGetFolder( fHandle, i )],
            @"DownloadFolder",
            ( fStat[i].status & ( TR_STATUS_CHECK | TR_STATUS_DOWNLOAD |
                TR_STATUS_SEED ) ) ? @"NO" : @"YES",
            @"Paused",
            NULL]];

        if( fStat[i].status & ( TR_STATUS_CHECK |
                TR_STATUS_DOWNLOAD | TR_STATUS_SEED ) )
        {
            tr_torrentStop( fHandle, i );
        }
    }

    /* Wait for torrents to stop (5 seconds timeout) */
    NSDate * start = [NSDate date];
    while( fCount > 0 )
    {
        while( [[NSDate date] timeIntervalSinceDate: start] < 5 )
        {
            fCount = tr_torrentStat( fHandle, &fStat );
            if( fStat[0].status & TR_STATUS_PAUSE )
            {
                break;
            }
            usleep( 500000 );
        }
        tr_torrentClose( fHandle, 0 );
        fCount = tr_torrentStat( fHandle, &fStat );
    }

    tr_close( fHandle );

    [[NSUserDefaults standardUserDefaults]
        setObject: history forKey: @"History"];

    return NSTerminateNow;
}

- (void) folderChoiceClosed: (NSOpenPanel *) s returnCode: (int) code
    contextInfo: (void *) info
{
    if( code != NSOKButton )
    {
        tr_torrentClose( fHandle, tr_torrentCount( fHandle ) - 1 );
        [NSApp stopModal];
        return;
    }

    tr_torrentSetFolder( fHandle, tr_torrentCount( fHandle ) - 1,
                         [[[s filenames] objectAtIndex: 0] UTF8String] );
    tr_torrentStart( fHandle, tr_torrentCount( fHandle ) - 1 );
    [NSApp stopModal];
}


- (void) application: (NSApplication *) sender
         openFiles: (NSArray *) filenames
{
    unsigned i;
    NSUserDefaults * defaults;
    NSString * downloadChoice, * downloadFolder, * torrentPath;

    defaults       = [NSUserDefaults standardUserDefaults];
    downloadChoice = [defaults stringForKey: @"DownloadChoice"];
    downloadFolder = [defaults stringForKey: @"DownloadFolder"];

    for( i = 0; i < [filenames count]; i++ )
    {
        torrentPath = [filenames objectAtIndex: i];

        if( tr_torrentInit( fHandle, [torrentPath UTF8String] ) )
        {
            continue;
        }

        if( [downloadChoice isEqualToString: @"Constant"] )
        {
            tr_torrentSetFolder( fHandle, tr_torrentCount( fHandle ) - 1,
                                 [downloadFolder UTF8String] );
            tr_torrentStart( fHandle, tr_torrentCount( fHandle ) - 1 );
            continue;
        }

        if( [downloadChoice isEqualToString: @"Torrent"] )
        {
            tr_torrentSetFolder( fHandle, tr_torrentCount( fHandle ) - 1,
                [[torrentPath stringByDeletingLastPathComponent] UTF8String] );
            tr_torrentStart( fHandle, tr_torrentCount( fHandle ) - 1 );
            continue;
        }

        NSOpenPanel * panel;
        NSString    * message;

        panel   = [NSOpenPanel openPanel];
        message = [NSString stringWithFormat:
            @"Select the download folder for %@",
            [torrentPath lastPathComponent]];
        
        [panel setPrompt:                  @"Select"];
        [panel setMessage:                   message];
        [panel setAllowsMultipleSelection:        NO];
        [panel setCanChooseFiles:                 NO];
        [panel setCanChooseDirectories:          YES];

        [panel beginSheetForDirectory: NULL file: NULL types: NULL
            modalForWindow: fWindow modalDelegate: self didEndSelector:
            @selector( folderChoiceClosed:returnCode:contextInfo: )
            contextInfo: NULL];
        [NSApp runModalForWindow: panel];
    }

    [self updateUI: NULL];
}

- (void) advancedChanged: (id) sender
{
    NSUserDefaults * defaults = [NSUserDefaults standardUserDefaults];
    if( [fAdvancedBarItem state] == NSOnState )
    {
        [fAdvancedBarItem setState: NSOffState];
        [defaults setObject:@"NO" forKey:@"UseAdvancedBar"];
    }
    else
    {
        [fAdvancedBarItem setState: NSOnState];
        [defaults setObject:@"YES" forKey:@"UseAdvancedBar"];
    }
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
    tr_torrentStart( fHandle, [fTableView selectedRow] );
    [self updateToolbar];
}

- (void) stopTorrent: (id) sender
{
    tr_torrentStop( fHandle, [fTableView selectedRow] );
    [self updateToolbar];
}

- (void) removeTorrent: (id) sender
{
    tr_torrentClose( fHandle, [fTableView selectedRow] );
    [self updateUI: NULL];
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
    int row;

    /* Update the NSTableView */
    if( fStat )
    {
        free( fStat );
    }
    fCount = tr_torrentStat( fHandle, &fStat );
    [fTableView reloadData];

    /* Update the global DL/UL rates */
    tr_torrentRates( fHandle, &dl, &ul );
    [fTotalDLField setStringValue: [NSString stringWithFormat:
        @"Total DL: %.2f KB/s", dl]];
    [fTotalULField setStringValue: [NSString stringWithFormat:
        @"Total UL: %.2f KB/s", ul]];

    /* Update DL/UL totals in the Info panel */
    row = [fTableView selectedRow];
    if( row > -1 )
    {
        [fInfoDownloaded setStringValue:
            stringForFileSize( fStat[row].downloaded )];
        [fInfoUploaded setStringValue:
            stringForFileSize( fStat[row].uploaded )];
    }

    /* Must we do this? Can't remember */
    [self updateToolbar];
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
    if( [[tableColumn identifier] isEqualToString: @"Name"] )
    {
        [(NameCell *) cell setStat: &fStat[rowIndex]];
    }
    else if( [[tableColumn identifier] isEqualToString: @"Progress"] )
    {
        [(ProgressCell *) cell setStat: &fStat[rowIndex]];
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

    [self updateToolbar];

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
        return;
    }

    /* Update info window */
    [fInfoTitle setStringValue: [NSString stringWithCString:
        fStat[row].info.name]];
    [fInfoTracker setStringValue: [NSString stringWithFormat:
        @"%s:%d", fStat[row].info.trackerAddress, fStat[row].info.trackerPort]];
    [fInfoAnnounce setStringValue: [NSString stringWithCString:
        fStat[row].info.trackerAnnounce]];
    [fInfoSize setStringValue:
        stringForFileSize( fStat[row].info.totalSize )];
    [fInfoPieces setStringValue: [NSString stringWithFormat: @"%d",
        fStat[row].info.pieceCount]];
    [fInfoPieceSize setStringValue:
        stringForFileSize( fStat[row].info.pieceSize )];
    [fInfoFolder setStringValue: [[NSString stringWithUTF8String:
        tr_torrentGetFolder( fHandle, row )] lastPathComponent]];
}

- (NSToolbarItem *) toolbar: (NSToolbar *) t itemForItemIdentifier:
    (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item;
    item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    [item setTarget: self];

    if( [ident isEqualToString: TOOLBAR_OPEN] )
    {
        [item setLabel: @"Open"];
        [item setToolTip: @"Open a torrent"];
        [item setImage: [NSImage imageNamed: @"Open.tiff"]];
    }
    else if( [ident isEqualToString: TOOLBAR_RESUME] )
    {
        [item setLabel: @"Resume"];
        [item setToolTip: @"Resume download"];
        [item setImage: [NSImage imageNamed: @"Resume.tiff"]];
    }
    else if( [ident isEqualToString: TOOLBAR_STOP] )
    {
        [item setLabel: @"Stop"];
        [item setToolTip: @"Stop download"];
        [item setImage: [NSImage imageNamed: @"Stop.tiff"]];
    }
    else if( [ident isEqualToString: TOOLBAR_REMOVE] )
    {
        [item setLabel: @"Remove"];
        [item setToolTip: @"Remove torrent from list"];
        [item setImage: [NSImage imageNamed: @"Remove.tiff"]];
    }
    else if( [ident isEqualToString: TOOLBAR_INFO] )
    {
        [item setLabel: @"Info"];
        [item setToolTip: @"Information"];
        [item setImage: [NSImage imageNamed: @"Info.tiff"]];
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
            TOOLBAR_OPEN, TOOLBAR_RESUME, TOOLBAR_STOP, TOOLBAR_REMOVE,
            NSToolbarFlexibleSpaceItemIdentifier, TOOLBAR_INFO, NULL];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) t
{
    return [self toolbarAllowedItemIdentifiers: t];
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
            /* Do not prevent idle sleep */
            /* TODO: prevent it unless there are all paused? */
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
            [self updateToolbar];
            break;
    }
}

- (NSRect) windowWillUseStandardFrame: (NSWindow *) w
    defaultFrame: (NSRect) defaultFrame
{
    NSRect rectWin, rectView;
    float  foo;

    rectWin  = [fWindow frame];
    rectView = [[fWindow contentView] frame];
    foo      = 68.0 + MAX( 1, tr_torrentCount( fHandle ) ) * 62.0 -
                  rectView.size.height;

    rectWin.size.height += foo;
    rectWin.origin.y    -= foo;

    return rectWin;
}

- (void) showMainWindow: (id) sender
{
    [fWindow makeKeyAndOrderFront: NULL];
}

- (void) linkHomepage: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL
        URLWithString:@"http://transmission.m0k.org/"]];
}

- (void) linkForums: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL
        URLWithString:@"http://transmission.m0k.org/forum/"]];
}

@end
