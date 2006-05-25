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

#import "PrefsController.h"
#import "StringAdditions.h"
#import "Utils.h"

#define DOWNLOAD_FOLDER     0
#define DOWNLOAD_TORRENT    2
#define DOWNLOAD_ASK        3

#define UPDATE_DAILY        0
#define UPDATE_WEEKLY       1
#define UPDATE_NEVER        2

#define TOOLBAR_GENERAL     @"General"
#define TOOLBAR_TRANSFERS    @"Transfers"
#define TOOLBAR_NETWORK     @"Network"

@interface PrefsController (Private)

- (void) showGeneralPref: (id) sender;
- (void) showTransfersPref: (id) sender;
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

- (void) dealloc
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

    [fToolbar setSelectedItemIdentifier: TOOLBAR_GENERAL];
    [self showGeneralPref: nil];

    fDefaults = [NSUserDefaults standardUserDefaults];
    fHandle = handle;
    
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
    
    [fUploadLimitItem setTitle:
                [NSString stringWithFormat: @"Limit (%d KB/s)", uploadLimit]];
    if (checkUpload)
        [fUploadLimitItem setState: NSOnState];
    else
        [fUploadNoLimitItem setState: NSOnState];
    
    tr_setUploadLimit( fHandle, checkUpload ? uploadLimit : -1 );

	//set download limit
    BOOL checkDownload = [fDefaults boolForKey: @"CheckDownload"];
    int downloadLimit = [fDefaults integerForKey: @"DownloadLimit"];
    
    [fDownloadCheck setState: checkDownload ? NSOnState : NSOffState];
    [fDownloadField setIntValue: downloadLimit];
    [fDownloadField setEnabled: checkDownload];
    
    [fDownloadLimitItem setTitle:
                [NSString stringWithFormat: @"Limit (%d KB/s)", downloadLimit]];
    if (checkDownload)
        [fDownloadLimitItem setState: NSOnState];
    else
        [fDownloadNoLimitItem setState: NSOnState];
    
    tr_setDownloadLimit( fHandle, checkDownload ? downloadLimit : -1 );
    
    //set ratio limit
    BOOL ratioCheck = [fDefaults boolForKey: @"RatioCheck"];
    float ratioLimit = [fDefaults floatForKey: @"RatioLimit"];

    [fRatioCheck setState: ratioCheck ? NSOnState : NSOffState];
    [fRatioField setEnabled: ratioCheck];
    [fRatioField setFloatValue: ratioLimit];
    
    [fRatioSetItem setTitle: [NSString stringWithFormat: @"Stop at Ratio (%.1f)", ratioLimit]];
    if (ratioCheck)
        [fRatioSetItem setState: NSOnState];
    else
        [fRatioNotSetItem setState: NSOnState];
    
    //set remove and quit prompts
    [fQuitCheck setState: [fDefaults boolForKey: @"CheckQuit"] ?
        NSOnState : NSOffState];
    [fRemoveCheck setState: [fDefaults boolForKey: @"CheckRemove"] ?
        NSOnState : NSOffState];

    //set dock badging
    [fBadgeDownloadRateCheck setState: [fDefaults boolForKey: @"BadgeDownloadRate"]];
    [fBadgeUploadRateCheck setState: [fDefaults boolForKey: @"BadgeUploadRate"]];
    
    //set auto start
    [fAutoStartCheck setState: [fDefaults boolForKey: @"AutoStartDownload"]];

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
    else if ([ident isEqualToString: TOOLBAR_TRANSFERS])
    {
        [item setLabel: TOOLBAR_TRANSFERS];
        [item setImage: [NSImage imageNamed: @"Transfers.png"]];
        [item setTarget: self];
        [item setAction: @selector( showTransfersPref: )];
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

- (NSArray *) toolbarSelectableItemIdentifiers: (NSToolbar *) toolbar
{
    return [self toolbarDefaultItemIdentifiers: nil];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) toolbar
{
    return [self toolbarAllowedItemIdentifiers: nil];
}

- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar *) toolbar
{
    return [NSArray arrayWithObjects:
            TOOLBAR_GENERAL, TOOLBAR_TRANSFERS,
            TOOLBAR_NETWORK, nil];
}

- (void) setPort: (id) sender
{
    int bindPort = [fPortField intValue];
    
    tr_setBindPort( fHandle, bindPort );
    [fDefaults setInteger: bindPort forKey: @"BindPort"];
}

- (void) setLimit: (id) sender
{
    NSString * key;
    NSMenuItem * menuItem;
    if (sender == fUploadField)
    {
        key = @"UploadLimit";
        menuItem = fUploadLimitItem;
    }
    else
    {
        key = @"DownloadLimit";
        menuItem = fDownloadLimitItem;
    }

    int limit = [sender intValue];
    [fDefaults setInteger: limit forKey: key];
    
    [menuItem setTitle: [NSString stringWithFormat: @"Limit (%d KB/s)", limit]];

    if( sender == fUploadField )
        tr_setUploadLimit( fHandle,
            ( [fUploadCheck state] == NSOffState ) ? -1 : limit );
    else
        tr_setDownloadLimit( fHandle,
            ( [fDownloadCheck state] == NSOffState ) ? -1 : limit );
}

- (void) setLimitCheck: (id) sender
{
    NSString * key;
    NSTextField * field;
    NSMenuItem * limitItem, * noLimitItem;
    if( sender == fUploadCheck )
    {
        key = @"CheckUpload";
        field = fUploadField;
        limitItem = fUploadLimitItem;
        noLimitItem = fUploadNoLimitItem;
    }
    else
    {
        key = @"CheckDownload";
        field = fDownloadField;
        limitItem = fDownloadLimitItem;
        noLimitItem = fDownloadNoLimitItem;
    }

    BOOL check = [sender state] == NSOnState;
    [limitItem setState: check ? NSOnState : NSOffState];
    [noLimitItem setState: !check ? NSOnState : NSOffState];
    
    [fDefaults setBool: check forKey: key];
    
    [field setIntValue: [field intValue]]; //set to valid value
    [self setLimit: field];
    
    [field setEnabled: check];
}

- (void) setLimitMenu: (id) sender
{
    NSButton * check = (sender == fUploadLimitItem || sender == fUploadNoLimitItem)
                        ? fUploadCheck : fDownloadCheck;
    int state = (sender == fUploadLimitItem || sender == fDownloadLimitItem)
                    ? NSOnState : NSOffState;
                
    [check setState: state];
    [self setLimitCheck: check];
}

- (void) setQuickSpeed: (id) sender
{
    NSString * title = [sender title];
    int limit = [[title substringToIndex: [title length] - [@" KB/s" length]] intValue];
    
    if ([sender menu] == fUploadMenu)
    {
        [fUploadField setIntValue: limit];
        [self setLimitMenu: fUploadLimitItem];
    }
    else
    {
        [fDownloadField setIntValue: limit];
        [self setLimitMenu: fDownloadLimitItem];
    }
}

- (void) setRatio: (id) sender
{
    float ratio = [sender floatValue];
    [fDefaults setFloat: ratio forKey: @"RatioLimit"];
    
    [fRatioSetItem setTitle: [NSString stringWithFormat: @"Stop at Ratio (%.1f)", ratio]];
}

- (void) setRatioCheck: (id) sender
{
    BOOL check = [sender state] == NSOnState;
    
    [fDefaults setBool: check forKey: @"RatioCheck"];
    
    [fRatioField setFloatValue: [fRatioField floatValue]]; //set to valid value
    [self setRatio: fRatioField];
    
    [fRatioField setEnabled: check];
    
    [fRatioSetItem setState: check ? NSOnState : NSOffState];
    [fRatioNotSetItem setState: !check ? NSOnState : NSOffState];
}

- (void) setRatioMenu: (id) sender
{
    int state = sender == fRatioSetItem ? NSOnState : NSOffState;
                
    [fRatioCheck setState: state];
    [self setRatioCheck: fRatioCheck];
}

- (void) setQuickRatio: (id) sender
{
    float limit = [[sender title] floatValue];

    [fRatioField setFloatValue: limit];
    [self setRatioMenu: fRatioSetItem];
}

- (void) setShowMessage: (id) sender
{
    if (sender == fQuitCheck)
        [fDefaults setBool: [sender state] forKey: @"CheckQuit"];
    else if (sender == fRemoveCheck)
        [fDefaults setBool: [fRemoveCheck state] forKey: @"CheckRemove"];
    else;
}

- (void) setBadge: (id) sender
{   
    if (sender == fBadgeDownloadRateCheck)
        [fDefaults setBool: [sender state] forKey: @"BadgeDownloadRate"];
    else if (sender == fBadgeUploadRateCheck)
        [fDefaults setBool: [sender state] forKey: @"BadgeUploadRate"];
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

- (void) setAutoStart: (id) sender
{
    [fDefaults setBool: [sender state] forKey: @"AutoStartDownload"];
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

- (void) showTransfersPref: (id) sender
{
    [self setPrefView: fTransfersView];
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
    [fPrefsWindow setFrame: windowRect display: YES animate: YES];
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
