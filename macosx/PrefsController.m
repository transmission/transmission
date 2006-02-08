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

#import "PrefsController.h"
#import "Utils.h"

#define MIN_PORT            1
#define MAX_PORT            65535

#define DOWNLOAD_FOLDER     0
#define DOWNLOAD_TORRENT    2
#define DOWNLOAD_ASK        3

#define UPDATE_DAILY        0
#define UPDATE_WEEKLY       1
#define UPDATE_NEVER        2

#define TOOLBAR_GENERAL     @"General"
#define TOOLBAR_NETWORK     @"Network"

@interface PrefsController (Private)

- (void) showGeneralPref: (id) sender;
- (void) showNetworkPref: (id) sender;

- (void) setPrefView: (NSView *) view;

- (void) folderSheetClosed: (NSOpenPanel *) s returnCode: (int) code
                                contextInfo: (void *) info;
- (void) updatePopUp;

@end

@implementation PrefsController

+ (void) initialize
{
    [[NSUserDefaults standardUserDefaults] registerDefaults:
        [NSDictionary dictionaryWithContentsOfFile:
            [[NSBundle mainBundle] pathForResource: @"Defaults"
                ofType: @"plist"]]];
}

- (void)dealloc
{
    [fDownloadFolder release];
    [super dealloc];
}

- (void) setPrefsWindow: (tr_handle_t *) handle
{
    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Preferences Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: NO];
    [fPrefsWindow setToolbar: fToolbar];
    [fToolbar setDisplayMode: NSToolbarDisplayModeIconAndLabel];
    [fToolbar setSizeMode: NSToolbarSizeModeRegular];
    [[fPrefsWindow standardWindowButton: NSWindowToolbarButton]
        setFrame: NSZeroRect];
    
    [fToolbar setSelectedItemIdentifier: TOOLBAR_GENERAL];
    [self setPrefView: fGeneralView];

    fDefaults = [NSUserDefaults standardUserDefaults];
    
    //set download folder
    NSString * downloadChoice  = [fDefaults stringForKey: @"DownloadChoice"];
    fDownloadFolder = [[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath];
    [fDownloadFolder retain];

    if( [downloadChoice isEqualToString: @"Constant"] )
    {
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
    }
    else if( [downloadChoice isEqualToString: @"Torrent"] )
    {
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_TORRENT];
    }
    else
    {
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_ASK];
    }
    [self updatePopUp];

    //set bind port
    int bindPort = [fDefaults integerForKey: @"BindPort"];
    [fPortField setIntValue: bindPort];
    fHandle = handle;
    tr_setBindPort( fHandle, bindPort );
    
    //checks for old version upload speed of -1
    if ([fDefaults integerForKey: @"UploadLimit"] < 0)
    {
        [fDefaults setInteger: 20 forKey: @"UploadLimit"];
        [fDefaults setBool: NO forKey: @"CheckUpload"];
    }
    
    //set upload limit
    BOOL checkUpload = [fDefaults boolForKey: @"CheckUpload"];
    int uploadLimit = [fDefaults integerForKey: @"UploadLimit"];
    
    [fUploadCheck setState: checkUpload ? NSOnState : NSOffState];
    [fUploadField setIntValue: uploadLimit];
    [fUploadField setEnabled: checkUpload];
    
    tr_setUploadLimit( fHandle, checkUpload ? uploadLimit : -1 );
    
    //set remove and quit prompts
    [fQuitCheck setState: [fDefaults boolForKey: @"CheckQuit"] ?
        NSOnState : NSOffState];
    [fRemoveCheck setState: [fDefaults boolForKey: @"CheckRemove"] ?
        NSOnState : NSOffState];

    //set dock badging
    [fBadgeDownloadRateCheck setState: [fDefaults boolForKey: @"BadgeDownloadRate"]];
    [fBadgeUploadRateCheck setState: [fDefaults boolForKey: @"BadgeUploadRate"]];

    /* Check for update */
    NSString * versionCheck  = [fDefaults stringForKey: @"VersionCheck"];
    if( [versionCheck isEqualToString: @"Daily"] )
        [fUpdatePopUp selectItemAtIndex: UPDATE_DAILY];
    else if( [versionCheck isEqualToString: @"Weekly"] )
        [fUpdatePopUp selectItemAtIndex: UPDATE_WEEKLY];
    else if( [versionCheck isEqualToString: @"Never"] )
        [fUpdatePopUp selectItemAtIndex: UPDATE_NEVER];
    else
    {
        [fDefaults setObject: @"Weekly" forKey: @"VersionCheck"];
        [fUpdatePopUp selectItemAtIndex: UPDATE_WEEKLY];
    }
}

- (NSToolbarItem *) toolbar: (NSToolbar *) t itemForItemIdentifier:
    (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item;
    item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if ([ident isEqualToString: TOOLBAR_GENERAL])
    {
        [item setLabel: TOOLBAR_GENERAL];
        [item setImage: [NSImage imageNamed: @"Preferences.png"]];
        [item setTarget: self];
        [item setAction: @selector( showGeneralPref: )];
    }
    else if ([ident isEqualToString: TOOLBAR_NETWORK])
    {
        [item setLabel: TOOLBAR_NETWORK];
        [item setImage: [NSImage imageNamed: @"Network.png"]];
        [item setTarget: self];
        [item setAction: @selector( showNetworkPref: )];
    }
    else
    {
        [item release];
        return nil;
    }

    return item;
}

- (NSArray *) toolbarSelectableItemIdentifiers: (NSToolbar *)toolbar
{
    return [self toolbarDefaultItemIdentifiers: nil];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *)toolbar
{
    return [self toolbarAllowedItemIdentifiers: nil];
}

- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar *)toolbar
{
    return [NSArray arrayWithObjects:
            TOOLBAR_GENERAL,
            TOOLBAR_NETWORK,
            nil];
}

- (void) setPort: (id) sender
{
    int bindPort = [fPortField intValue];
    
    //if value entered is not an int or is not in range do not change
    if (![[fPortField stringValue] isEqualToString:
            [NSString stringWithFormat: @"%d", bindPort]] 
            || bindPort < MIN_PORT
            || bindPort > MAX_PORT)
    {
        NSBeep();
        bindPort = [fDefaults integerForKey: @"BindPort"];
        [fPortField setIntValue: bindPort];
    }
    else
    {
        tr_setBindPort( fHandle, bindPort );
        [fDefaults setInteger: bindPort forKey: @"BindPort"];
    }
}

- (void) setLimitUploadCheck: (id) sender
{
    BOOL checkUpload = [fUploadCheck state] == NSOnState;

    [fDefaults setBool: checkUpload forKey: @"CheckUpload"];
    
    [self setUploadLimit: nil];
    [fUploadField setEnabled: checkUpload];
}

- (void) setUploadLimit: (id) sender
{
    int uploadLimit = [fUploadField intValue];
    
    //if value entered is not an int or is less than 0 do not change
    if (![[fUploadField stringValue] isEqualToString:
            [NSString stringWithFormat: @"%d", uploadLimit]]
            || uploadLimit < 0)
    {
        NSBeep();
        uploadLimit = [fDefaults integerForKey: @"UploadLimit"];
        [fUploadField setIntValue: uploadLimit];
    }
    else
    {
        [fDefaults setInteger: uploadLimit forKey: @"UploadLimit"];
    }
    
    if ([fUploadCheck state] == NSOffState || uploadLimit == 0)
        uploadLimit = -1;
    tr_setUploadLimit( fHandle, uploadLimit );
}

- (void) setQuitMessage: (id) sender
{
    [fDefaults setBool: ( [fQuitCheck state] == NSOnState )
        forKey: @"CheckQuit"];
}

- (void) setRemoveMessage: (id) sender
{
    [fDefaults setBool: ( [fRemoveCheck state] == NSOnState )
        forKey: @"CheckRemove"];
}

- (void) setBadge: (id) sender
{   
    BOOL state = [sender state];
    
    if (sender == fBadgeDownloadRateCheck)
        [fDefaults setBool: state forKey: @"BadgeDownloadRate"];
    else if (sender == fBadgeUploadRateCheck)
        [fDefaults setBool: state forKey: @"BadgeUploadRate"];
    else;
}

- (void) setUpdate: (id) sender
{
    switch( [fUpdatePopUp indexOfSelectedItem] )
    {
        case UPDATE_DAILY:
            [fDefaults setObject: @"Daily" forKey: @"VersionCheck"];
            break;
        case UPDATE_WEEKLY:
            [fDefaults setObject: @"Weekly" forKey: @"VersionCheck"];
            break;
        case UPDATE_NEVER:
            [fDefaults setObject: @"Never" forKey: @"VersionCheck"];
            break;
    }
}

- (void) setDownloadLocation: (id) sender
{
    //Download folder
    switch( [fFolderPopUp indexOfSelectedItem] )
    {
        case DOWNLOAD_FOLDER:
            [fDefaults setObject: @"Constant" forKey: @"DownloadChoice"];
            break;
        case DOWNLOAD_TORRENT:
            [fDefaults setObject: @"Torrent" forKey: @"DownloadChoice"];
            break;
        case DOWNLOAD_ASK:
            [fDefaults setObject: @"Ask" forKey: @"DownloadChoice"];
            break;
    }
}

- (void) folderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt:                  @"Select"];
    [panel setAllowsMultipleSelection:        NO];
    [panel setCanChooseFiles:                 NO];
    [panel setCanChooseDirectories:          YES];

    [panel beginSheetForDirectory: NULL file: NULL types: NULL
        modalForWindow: fPrefsWindow modalDelegate: self didEndSelector:
        @selector( folderSheetClosed:returnCode:contextInfo: )
        contextInfo: NULL];
}

@end // @implementation PrefsController

@implementation PrefsController (Private)

- (void) showGeneralPref: (id) sender
{
    [self setPrefView: fGeneralView];
}

- (void) showNetworkPref: (id) sender
{
    [self setPrefView: fNetworkView];
}

- (void) setPrefView: (NSView *) view
{
    NSRect windowRect = [fPrefsWindow frame];
    int difference = [view frame].size.height - [[fPrefsWindow contentView] frame].size.height;

    windowRect.origin.y -= difference;
    windowRect.size.height += difference;
    
    [fPrefsWindow setTitle: [fToolbar selectedItemIdentifier]];
    [fPrefsWindow setContentView: fBlankView];
    [fPrefsWindow setFrame:windowRect display: YES animate: YES];
    [fPrefsWindow setContentView: view];
}

- (void) folderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code
    contextInfo: (void *) info
{
   if (code == NSOKButton)
   {
       [fDownloadFolder release];
       fDownloadFolder = [[openPanel filenames] objectAtIndex: 0];
       [fDownloadFolder retain];

       [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
       [fDefaults setObject: fDownloadFolder forKey: @"DownloadFolder"];
       [fDefaults setObject: @"Constant" forKey: @"DownloadChoice"];

       [self updatePopUp];
   }
   else
   {
       //reset if cancelled
       NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
       if( [downloadChoice isEqualToString: @"Constant"] )
       {
           [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
       }
       else if( [downloadChoice isEqualToString: @"Torrent"] )
       {
           [fFolderPopUp selectItemAtIndex: DOWNLOAD_TORRENT];
       }
       else
       {
           [fFolderPopUp selectItemAtIndex: DOWNLOAD_ASK];
       }
   }
}

- (void) updatePopUp
{
    NSMenuItem     * menuItem;
    NSImage        * image32, * image16;

    // Get the icon for the folder
    image32 = [[NSWorkspace sharedWorkspace] iconForFile:
                fDownloadFolder];
    image16 = [[NSImage alloc] initWithSize: NSMakeSize(16,16)];

    // 32x32 -> 16x16 scaling
    [image16 lockFocus];
    [[NSGraphicsContext currentContext]
        setImageInterpolation: NSImageInterpolationHigh];
    [image32 drawInRect: NSMakeRect(0,0,16,16)
        fromRect: NSMakeRect(0,0,32,32) operation: NSCompositeCopy
        fraction: 1.0];
    [image16 unlockFocus];

    // Update the menu item
    menuItem = (NSMenuItem *) [fFolderPopUp itemAtIndex: 0];
    [menuItem setTitle: [fDownloadFolder lastPathComponent]];
    [menuItem setImage: image16];

    [image16 release];
}

@end /* @implementation PrefsController (Private) */
