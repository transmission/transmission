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

#include "PrefsController.h"

@interface PrefsController (Private)

- (void) folderSheetShow:   (id) sender;
- (void) folderSheetClosed: (NSOpenPanel *) s returnCode: (int) code
                                contextInfo: (void *) info;
- (void) loadSettings;
- (void) saveSettings;
- (void) updatePopUp;

@end

@implementation PrefsController

/***********************************************************************
 * setHandle
 ***********************************************************************
 *
 **********************************************************************/
- (void) setHandle: (tr_handle_t *) handle
{
    NSUserDefaults * defaults;
    NSDictionary   * appDefaults;
    NSString       * desktop, * port;

    fHandle = handle;

    /* Register defaults settings:
        - Simple bar
        - Always download to Desktop
        - Port TR_DEFAULT_PORT
        - 20 KB/s upload limit */
    desktop = [NSHomeDirectory() stringByAppendingString: @"/Desktop"];
    port    = [NSString stringWithFormat: @"%d", TR_DEFAULT_PORT];

    defaults    = [NSUserDefaults standardUserDefaults];
    appDefaults = [NSDictionary dictionaryWithObjectsAndKeys:
                    @"NO",       @"UseAdvancedBar",
                    @"Constant", @"DownloadChoice",
                    desktop,     @"DownloadFolder",
                    port,        @"BindPort",
                    @"20",       @"UploadLimit",
                    NULL];
    [defaults registerDefaults: appDefaults];

    /* Apply settings */
    tr_setBindPort( fHandle, [defaults integerForKey: @"BindPort"] );
    tr_setUploadLimit( fHandle, [defaults integerForKey: @"UploadLimit"] );
}

/***********************************************************************
 * show
 ***********************************************************************
 *
 **********************************************************************/
- (void) show: (id) sender
{
    NSRect  mainFrame;
    NSRect  prefsFrame;
    NSRect  screenRect;
    NSPoint point;

    [self loadSettings];

    /* Place the window */
    mainFrame  = [fWindow frame];
    prefsFrame = [fPrefsWindow frame];
    screenRect = [[NSScreen mainScreen] visibleFrame];
    point.x    = mainFrame.origin.x + mainFrame.size.width / 2 -
                    prefsFrame.size.width / 2;
    point.y    = mainFrame.origin.y + mainFrame.size.height - 30;

    /* Make sure it is in the screen */
    if( point.x < screenRect.origin.x )
    {
        point.x = screenRect.origin.x;
    }
    if( point.x + prefsFrame.size.width >
            screenRect.origin.x + screenRect.size.width )
    {
        point.x = screenRect.origin.x +
            screenRect.size.width - prefsFrame.size.width;
    }
    if( point.y - prefsFrame.size.height < screenRect.origin.y )
    {
        point.y = screenRect.origin.y + prefsFrame.size.height;
    }

    [fPrefsWindow setFrameTopLeftPoint: point];
    [fPrefsWindow makeKeyAndOrderFront: NULL];
}

/***********************************************************************
 * ratio
 ***********************************************************************
 *
 **********************************************************************/
- (void) ratio: (id) sender
{
    [fFolderPopUp setEnabled: ![fFolderMatrix selectedRow]];
}

/***********************************************************************
 * check
 ***********************************************************************
 *
 **********************************************************************/
- (void) check: (id) sender
{
    if( [fUploadCheck state] == NSOnState )
    {
        [fUploadField setEnabled: YES];
    }
    else
    {
        [fUploadField setEnabled: NO];
        [fUploadField setStringValue: @""];
    }
}

/***********************************************************************
 * cancel
 ***********************************************************************
 * Discards changes and closes the Preferences window
 **********************************************************************/
- (void) cancel: (id) sender
{
    [fDownloadFolder release];
    [fPrefsWindow close];
}

/***********************************************************************
 * save
 ***********************************************************************
 * Checks the user-defined options. If they are correct, saves settings
 * and closes the Preferences window. Otherwise corrects them and leaves
 * the window open
 **********************************************************************/
- (void) save: (id) sender
{
    int              bindPort;
    int              uploadLimit;

    /* Bind port */
    bindPort = [fPortField intValue];
    bindPort = MAX( 1, bindPort );
    bindPort = MIN( bindPort, 65535 );

    if( ![[fPortField stringValue] isEqualToString:
            [NSString stringWithFormat: @"%d", bindPort]] )
    {
        [fPortField setIntValue: bindPort];
        return;
    }

    /* Upload limit */
    if( [fUploadCheck state] == NSOnState )
    {
        uploadLimit = [fUploadField intValue];
        uploadLimit = MAX( 0, uploadLimit );

        if( ![[fUploadField stringValue] isEqualToString:
                [NSString stringWithFormat: @"%d", uploadLimit]] )
        {
            [fUploadField setIntValue: uploadLimit];
            return;
        }
    }

    [self saveSettings];
    [self cancel: NULL];
}

@end /* @implementation PrefsController */

@implementation PrefsController (Private)

- (void) folderSheetShow: (id) sender
{
    NSOpenPanel * panel;

    panel = [NSOpenPanel openPanel];

    [panel setPrompt:                  @"Select"];
    [panel setAllowsMultipleSelection:        NO];
    [panel setCanChooseFiles:                 NO];
    [panel setCanChooseDirectories:          YES];

    [panel beginSheetForDirectory: NULL file: NULL types: NULL
        modalForWindow: fPrefsWindow modalDelegate: self didEndSelector:
        @selector( folderSheetClosed:returnCode:contextInfo: )
        contextInfo: NULL];
}

- (void) folderSheetClosed: (NSOpenPanel *) s returnCode: (int) code
    contextInfo: (void *) info
{
    [fFolderPopUp selectItemAtIndex: 0];

    if( code != NSOKButton )
    {
        return;
    }

    [fDownloadFolder release];
    fDownloadFolder = [[s filenames] objectAtIndex: 0];
    [fDownloadFolder retain];

    [self updatePopUp];
}

/***********************************************************************
 * loadSettings
 ***********************************************************************
 * Update the interface with the current settings
 **********************************************************************/
- (void) loadSettings
{
    NSUserDefaults * defaults;
    NSString       * downloadChoice;
    int              uploadLimit;

    /* Fill with current settings */
    defaults = [NSUserDefaults standardUserDefaults];

    /* Download folder selection */
    downloadChoice  = [defaults stringForKey: @"DownloadChoice"];
    fDownloadFolder = [defaults stringForKey: @"DownloadFolder"];
    [fDownloadFolder retain];

    if( [downloadChoice isEqualToString: @"Constant"] )
    {
        [fFolderMatrix selectCellAtRow: 0 column: 0];
    }
    else if( [downloadChoice isEqualToString: @"Torrent"] )
    {
        [fFolderMatrix selectCellAtRow: 1 column: 0];
    }
    else
    {
        [fFolderMatrix selectCellAtRow: 2 column: 0];
    }
    [self ratio: NULL];
    [self updatePopUp];

    [fPortField setIntValue: [defaults integerForKey: @"BindPort"]];

    uploadLimit = [defaults integerForKey: @"UploadLimit"];
    if( uploadLimit < 0 )
    {
        [fUploadCheck setState: NSOffState];
    }
    else
    {
        [fUploadCheck setState: NSOnState];
        [fUploadField setIntValue: uploadLimit];
    }
    [self check: NULL];
}

/***********************************************************************
 * saveSettings
 ***********************************************************************
 *
 **********************************************************************/
- (void) saveSettings
{
    NSUserDefaults * defaults;
    int              bindPort;
    int              uploadLimit;

    defaults = [NSUserDefaults standardUserDefaults];

    /* Download folder */
    switch( [fFolderMatrix selectedRow] )
    {
        case 0:
            [defaults setObject: @"Constant" forKey: @"DownloadChoice"];
            break;
        case 1:
            [defaults setObject: @"Torrent" forKey: @"DownloadChoice"];
            break;
        case 2:
            [defaults setObject: @"Ask" forKey: @"DownloadChoice"];
            break;
    }
    [defaults setObject: fDownloadFolder forKey: @"DownloadFolder"];

    /* Bind port */
    bindPort = [fPortField intValue];
    tr_setBindPort( fHandle, bindPort );
    [defaults setObject: [NSString stringWithFormat: @"%d", bindPort]
        forKey: @"BindPort"];

    /* Upload limit */
    if( [fUploadCheck state] == NSOnState )
    {
        uploadLimit = [fUploadField intValue];
    }
    else
    {
        uploadLimit = -1;
    }
    tr_setUploadLimit( fHandle, uploadLimit );
    [defaults setObject: [NSString stringWithFormat: @"%d", uploadLimit]
        forKey: @"UploadLimit"];
}

/***********************************************************************
 * updatePopUp
 ***********************************************************************
 * Uses fDownloadFolder to update the displayed folder name and icon
 **********************************************************************/
- (void) updatePopUp
{
    NSMenuItem     * menuItem;
    NSImage        * image32, * image16;

    /* Set up the pop up */
    [fFolderPopUp        removeAllItems];
    [fFolderPopUp        addItemWithTitle: @""];
    [[fFolderPopUp menu] addItem: [NSMenuItem separatorItem]];
    [fFolderPopUp        addItemWithTitle: @"Other..."];

    menuItem = (NSMenuItem *) [fFolderPopUp lastItem];
    [menuItem setTarget: self];
    [menuItem setAction: @selector( folderSheetShow: )];

    /* Get the icon for the folder */
    image32 = [[NSWorkspace sharedWorkspace] iconForFile:
                fDownloadFolder];
    image16 = [[NSImage alloc] initWithSize: NSMakeSize(16,16)];

    /* 32x32 -> 16x16 scaling */
    [image16 lockFocus];
    [[NSGraphicsContext currentContext]
        setImageInterpolation: NSImageInterpolationHigh];
    [image32 drawInRect: NSMakeRect(0,0,16,16)
        fromRect: NSMakeRect(0,0,32,32) operation: NSCompositeCopy
        fraction: 1.0];
    [image16 unlockFocus];

    /* Update the menu item */
    menuItem = (NSMenuItem *) [fFolderPopUp itemAtIndex: 0];
    [menuItem setTitle: [fDownloadFolder lastPathComponent]];
    [menuItem setImage: image16];

    [image16 release];
}

@end /* @implementation PrefsController (Private) */
