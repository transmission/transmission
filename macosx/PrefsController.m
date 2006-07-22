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

#import "PrefsController.h"
#import "StringAdditions.h"

#define MIN_PORT    1
#define MAX_PORT    65535

#define DOWNLOAD_FOLDER     0
#define DOWNLOAD_TORRENT    2
#define DOWNLOAD_ASK    3

#define START_YES_CHECK_TAG     0
#define START_WAIT_CHECK_TAG    1
#define START_NO_CHECK_TAG      2

#define UPDATE_DAILY    0
#define UPDATE_WEEKLY   1
#define UPDATE_NEVER    2

#define TOOLBAR_GENERAL     @"General"
#define TOOLBAR_TRANSFERS   @"Transfers"
#define TOOLBAR_BANDWIDTH   @"Bandwidth"
#define TOOLBAR_NETWORK     @"Network"

@interface PrefsController (Private)

- (void) showGeneralPref: (id) sender;
- (void) showTransfersPref: (id) sender;
- (void) showBandwidthPref: (id) sender;
- (void) showNetworkPref: (id) sender;

- (void) setPrefView: (NSView *) view;

- (void) folderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
- (void) updatePopUp;

- (void) importFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
- (void) updateImportPopUp;

@end

@implementation PrefsController

+ (void) initialize
{
    [[NSUserDefaults standardUserDefaults] registerDefaults:
        [NSDictionary dictionaryWithContentsOfFile:
        [[NSBundle mainBundle] pathForResource: @"Defaults" ofType: @"plist"]]];
}

- (void) dealloc
{
    [fDownloadFolder release];
    [super dealloc];
}

- (void) setPrefs: (tr_handle_t *) handle
{
    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Preferences Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: NO];
    [[self window] setToolbar: fToolbar];
    [fToolbar setDisplayMode: NSToolbarDisplayModeIconAndLabel];
    [fToolbar setSizeMode: NSToolbarSizeModeRegular];

    [fToolbar setSelectedItemIdentifier: TOOLBAR_GENERAL];
    [self showGeneralPref: nil];

    fDefaults = [NSUserDefaults standardUserDefaults];
    fHandle = handle;
    
    //set download folder
    NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
    fDownloadFolder = [[[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath] retain];
    if ([downloadChoice isEqualToString: @"Constant"])
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
    else if ([downloadChoice isEqualToString: @"Torrent"])
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_TORRENT];
    else
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_ASK];
    [self updatePopUp];
    
    //set auto import
    fImportFolder = [[[fDefaults stringForKey: @"AutoImportDirectory"] stringByExpandingTildeInPath] retain];
    [self updateImportPopUp];
 
    BOOL autoImport = [fDefaults boolForKey: @"AutoImport"];
    [fAutoImportCheck setState: autoImport];
    [fImportFolderPopUp setEnabled: autoImport];
    
    //set auto size
    [fAutoSizeCheck setState: [fDefaults boolForKey: @"AutoSize"]];
    
    //set bind port
    int bindPort = [fDefaults integerForKey: @"BindPort"];
    [fPortField setIntValue: bindPort];
    tr_setBindPort(fHandle, bindPort);
    
    //checks for old version upload speed of -1
    if ([fDefaults integerForKey: @"UploadLimit"] < 0)
    {
        [fDefaults setInteger: 20 forKey: @"UploadLimit"];
        [fDefaults setBool: NO forKey: @"CheckUpload"];
    }
    
    //set upload limit
    BOOL checkUpload = [fDefaults boolForKey: @"CheckUpload"];
    int uploadLimit = [fDefaults integerForKey: @"UploadLimit"];
    
    [fUploadCheck setState: checkUpload];
    [fUploadField setIntValue: uploadLimit];
    [fUploadField setEnabled: checkUpload];

	//set download limit
    BOOL checkDownload = [fDefaults boolForKey: @"CheckDownload"];
    int downloadLimit = [fDefaults integerForKey: @"DownloadLimit"];
    
    [fDownloadCheck setState: checkDownload];
    [fDownloadField setIntValue: downloadLimit];
    [fDownloadField setEnabled: checkDownload];
    
    //set speed limit
    int speedLimitUploadLimit = [fDefaults integerForKey: @"SpeedLimitUploadLimit"];
    [fSpeedLimitUploadField setIntValue: speedLimitUploadLimit];
    
    int speedLimitDownloadLimit = [fDefaults integerForKey: @"SpeedLimitDownloadLimit"];
    [fSpeedLimitDownloadField setIntValue: speedLimitDownloadLimit];
    
    //actually set bandwidth limits
    if ([fDefaults boolForKey: @"SpeedLimit"])
    {
        tr_setUploadLimit(fHandle, speedLimitUploadLimit);
        tr_setDownloadLimit(fHandle, speedLimitDownloadLimit);
    }
    else
    {
        tr_setUploadLimit(fHandle, checkUpload ? uploadLimit : -1);
        tr_setDownloadLimit(fHandle, checkDownload ? downloadLimit : -1);
    }
    
    //set ratio limit
    BOOL ratioCheck = [fDefaults boolForKey: @"RatioCheck"];
    [fRatioCheck setState: ratioCheck];
    [fRatioField setEnabled: ratioCheck];
    [fRatioField setFloatValue: [fDefaults floatForKey: @"RatioLimit"]];
    
    //set remove and quit prompts
    BOOL isQuitCheck = [fDefaults boolForKey: @"CheckQuit"],
        isRemoveCheck = [fDefaults boolForKey: @"CheckRemove"];
    
    [fQuitCheck setState: isQuitCheck];
    [fRemoveCheck setState: isRemoveCheck];
    
    [fQuitDownloadingCheck setState: [fDefaults boolForKey: @"CheckQuitDownloading"]];
    [fQuitDownloadingCheck setEnabled: isQuitCheck];
    [fRemoveDownloadingCheck setState: [fDefaults boolForKey: @"CheckRemoveDownloading"]];
    [fRemoveDownloadingCheck setEnabled: isRemoveCheck];

    //set dock badging
    [fBadgeDownloadRateCheck setState: [fDefaults boolForKey: @"BadgeDownloadRate"]];
    [fBadgeUploadRateCheck setState: [fDefaults boolForKey: @"BadgeUploadRate"]];
    
    //set start setting
    NSString * startSetting = [fDefaults stringForKey: @"StartSetting"];
    int tag;
    if ([startSetting isEqualToString: @"Start"])
        tag = START_YES_CHECK_TAG;
    else if ([startSetting isEqualToString: @"Wait"])
        tag = START_WAIT_CHECK_TAG;
    else
        tag = START_NO_CHECK_TAG;
    
    [fStartMatrix selectCellWithTag: tag];
    [fWaitToStartField setEnabled: tag == START_WAIT_CHECK_TAG];
    [fWaitToStartField setIntValue: [fDefaults integerForKey: @"WaitToStartNumber"]];
    
    //set private torrents
    BOOL copyTorrents = [fDefaults boolForKey: @"SavePrivateTorrent"];
    [fCopyTorrentCheck setState: copyTorrents];
    
    [fDeleteOriginalTorrentCheck setEnabled: copyTorrents];
    [fDeleteOriginalTorrentCheck setState: [fDefaults boolForKey: @"DeleteOriginalTorrent"]];

    //set update check
    NSString * updateCheck = [fDefaults stringForKey: @"UpdateCheck"];
    if ([updateCheck isEqualToString: @"Weekly"])
        [fUpdatePopUp selectItemAtIndex: UPDATE_WEEKLY];
    else if ([updateCheck isEqualToString: @"Never"])
        [fUpdatePopUp selectItemAtIndex: UPDATE_NEVER];
    else
        [fUpdatePopUp selectItemAtIndex: UPDATE_DAILY];
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
        [item setAction: @selector(showGeneralPref:)];
    }
    else if ([ident isEqualToString: TOOLBAR_TRANSFERS])
    {
        [item setLabel: TOOLBAR_TRANSFERS];
        [item setImage: [NSImage imageNamed: @"Transfers.png"]];
        [item setTarget: self];
        [item setAction: @selector(showTransfersPref:)];
    }
    else if ([ident isEqualToString: TOOLBAR_BANDWIDTH])
    {
        [item setLabel: TOOLBAR_BANDWIDTH];
        [item setImage: [NSImage imageNamed: @"Bandwidth.png"]];
        [item setTarget: self];
        [item setAction: @selector(showBandwidthPref:)];
    }
    else if ([ident isEqualToString: TOOLBAR_NETWORK])
    {
        [item setLabel: TOOLBAR_NETWORK];
        [item setImage: [NSImage imageNamed: @"Network.png"]];
        [item setTarget: self];
        [item setAction: @selector(showNetworkPref:)];
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
    return [self toolbarDefaultItemIdentifiers: toolbar];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) toolbar
{
    return [self toolbarAllowedItemIdentifiers: toolbar];
}

- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar *) toolbar
{
    return [NSArray arrayWithObjects:
            TOOLBAR_GENERAL, TOOLBAR_TRANSFERS,
            TOOLBAR_BANDWIDTH, TOOLBAR_NETWORK, nil];
}

- (void) setPort: (id) sender
{
    int bindPort = [sender intValue];
    if (![[NSString stringWithInt: bindPort] isEqualToString: [sender stringValue]]
            || bindPort < MIN_PORT || bindPort > MAX_PORT)
    {
        NSBeep();
        bindPort = [fDefaults integerForKey: @"BindPort"];
        [sender setIntValue: bindPort];
    }
    else
    {
        tr_setBindPort( fHandle, bindPort );
        [fDefaults setInteger: bindPort forKey: @"BindPort"];
    }
}

- (void) setLimit: (id) sender
{
    NSString * key;
    NSButton * check;
    NSString * type;
    if (sender == fUploadField)
    {
        key = @"UploadLimit";
        check = fUploadCheck;
        type = @"Upload";
    }
    else
    {
        key = @"DownloadLimit";
        check = fDownloadCheck;
        type = @"Download";
    }

    int limit = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%d", limit]] || limit < 0)
    {
        NSBeep();
        limit = [fDefaults integerForKey: key];
        [sender setIntValue: limit];
    }
    else
    {
        if (![fDefaults boolForKey: @"SpeedLimit"])
        {
            if (sender == fUploadField)
                tr_setUploadLimit(fHandle, [fUploadCheck state] ? limit : -1);
            else
                tr_setDownloadLimit(fHandle, [fDownloadCheck state] ? limit : -1);
        }
        
        [fDefaults setInteger: limit forKey: key];
    }
    
    NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys:
                                    [NSNumber numberWithBool: [check state]], @"Enable",
                                    [NSNumber numberWithInt: limit], @"Limit",
                                    type, @"Type", nil];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"LimitGlobalChange" object: dict];
}

- (void) setLimitCheck: (id) sender
{
    NSString * key;
    NSTextField * field;
    if (sender == fUploadCheck)
    {
        key = @"CheckUpload";
        field = fUploadField;
    }
    else
    {
        key = @"CheckDownload";
        field = fDownloadField;
    }
    
    BOOL check = [sender state] == NSOnState;
    [self setLimit: field];
    [field setEnabled: check];
    
    [fDefaults setBool: check forKey: key];
}

- (void) setLimitEnabled: (BOOL) enable type: (NSString *) type
{
    NSButton * check = [type isEqualToString: @"Upload"] ? fUploadCheck : fDownloadCheck;
    [check setState: enable ? NSOnState : NSOffState];
    [self setLimitCheck: check];
}

- (void) setQuickLimit: (int) limit type: (NSString *) type
{
    NSButton * check;
    if ([type isEqualToString: @"Upload"])
    {
        [fUploadField setIntValue: limit];
        check = fUploadCheck;
    }
    else
    {
        [fDownloadField setIntValue: limit];
        check = fDownloadCheck;
    }
    [check setState: NSOnState];
    [self setLimitCheck: check];
}

- (void) enableSpeedLimit: (BOOL) enable
{
    if ([fDefaults boolForKey: @"SpeedLimit"] != enable)
    {
        [fDefaults setBool: enable forKey: @"SpeedLimit"];
        
        if (enable)
        {
            tr_setUploadLimit(fHandle, [fDefaults integerForKey: @"SpeedLimitUploadLimit"]);
            tr_setDownloadLimit(fHandle, [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]);
        }
        else
        {
            tr_setUploadLimit(fHandle, [fUploadCheck state] ? [fDefaults integerForKey: @"UploadLimit"] : -1);
            tr_setDownloadLimit(fHandle, [fDownloadCheck state] ? [fDefaults integerForKey: @"DownloadLimit"] : -1);
        }
    }
}

- (void) setSpeedLimit: (id) sender
{
    NSString * key = sender == fSpeedLimitUploadField ? @"SpeedLimitUploadLimit" : @"SpeedLimitDownloadLimit";

    int limit = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%d", limit]] || limit < 0)
    {
        NSBeep();
        limit = [fDefaults integerForKey: key];
        [sender setIntValue: limit];
    }
    else
    {
        if ([fDefaults boolForKey: @"SpeedLimit"])
        {
            if (sender == fSpeedLimitUploadField)
                tr_setUploadLimit(fHandle, limit);
            else
                tr_setDownloadLimit(fHandle, limit);
        }
        
        [fDefaults setInteger: limit forKey: key];
    }
}

- (void) setRatio: (id) sender
{
    float ratioLimit = [sender floatValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%.2f", ratioLimit]]
            || ratioLimit < 0)
    {
        NSBeep();
        ratioLimit = [fDefaults floatForKey: @"RatioLimit"];
        [sender setFloatValue: ratioLimit];
    }
    else
        [fDefaults setFloat: ratioLimit forKey: @"RatioLimit"];
    
    NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys:
                                [NSNumber numberWithBool: [fRatioCheck state]], @"Enable",
                                [NSNumber numberWithFloat: ratioLimit], @"Ratio", nil];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"RatioGlobalChange" object: dict];
}

- (void) setRatioCheck: (id) sender
{
    BOOL check = [sender state] == NSOnState;
    [self setRatio: fRatioField];
    [fRatioField setEnabled: check];
    
    [fDefaults setBool: check forKey: @"RatioCheck"];
}

- (void) setRatioEnabled: (BOOL) enable
{
    int state = enable ? NSOnState : NSOffState;
    
    [fRatioCheck setState: state];
    [self setRatioCheck: fRatioCheck];
}

- (void) setQuickRatio: (float) ratioLimit
{
    [fRatioField setFloatValue: ratioLimit];
    
    [fRatioCheck setState: NSOnState];
    [self setRatioCheck: fRatioCheck];
}

- (void) setShowMessage: (id) sender
{
    BOOL state = [sender state];

    if (sender == fQuitCheck)
    {
        [fDefaults setBool: state forKey: @"CheckQuit"];
        [fQuitDownloadingCheck setEnabled: state];
    }
    else if (sender == fRemoveCheck)
    {
        [fDefaults setBool: state forKey: @"CheckRemove"];
        [fRemoveDownloadingCheck setEnabled: state];
    }
    if (sender == fQuitDownloadingCheck)
        [fDefaults setBool: state forKey: @"CheckQuitDownloading"];
    else if (sender == fRemoveDownloadingCheck)
        [fDefaults setBool: state forKey: @"CheckRemoveDownloading"];
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
    int index = [fUpdatePopUp indexOfSelectedItem];
    NSTimeInterval seconds;
    if (index == UPDATE_DAILY)
    {
        [fDefaults setObject: @"Daily" forKey: @"UpdateCheck"];
        seconds = 86400;
    }
    else if (index == UPDATE_WEEKLY)
    {
        [fDefaults setObject: @"Weekly" forKey: @"UpdateCheck"];
        seconds = 604800;
    }
    else
    {
        [fDefaults setObject: @"Never" forKey: @"UpdateCheck"];
        seconds = 0;
    }

    [fDefaults setInteger: seconds forKey: @"SUScheduledCheckInterval"];
    [fUpdater scheduleCheckWithInterval: seconds];
}

- (void) setStartSetting: (id) sender
{
    NSString * startSetting;

    int tag = [[fStartMatrix selectedCell] tag];
    if (tag == START_YES_CHECK_TAG)
        startSetting = @"Start";
    else if (tag == START_WAIT_CHECK_TAG)
        startSetting = @"Wait";
    else
        startSetting = @"Manual";
    
    [fDefaults setObject: startSetting forKey: @"StartSetting"];
    
    [self setWaitToStart: fWaitToStartField];
    [fWaitToStartField setEnabled: tag == START_WAIT_CHECK_TAG];
}

- (void) setWaitToStart: (id) sender
{
    int waitNumber = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithInt: waitNumber]] || waitNumber < 1)
    {
        NSBeep();
        waitNumber = [fDefaults floatForKey: @"WaitToStartNumber"];
        [sender setIntValue: waitNumber];
    }
    else
        [fDefaults setInteger: waitNumber forKey: @"WaitToStartNumber"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"GlobalStartSettingChange" object: self];
}

- (void) setMoveTorrent: (id) sender
{
    int state = [sender state];
    if (sender == fCopyTorrentCheck)
    {
        [fDefaults setBool: state forKey: @"SavePrivateTorrent"];
        
        [fDeleteOriginalTorrentCheck setEnabled: state];
        if (state == NSOffState)
        {
            [fDeleteOriginalTorrentCheck setState: NSOffState];
            [fDefaults setBool: NO forKey: @"DeleteOriginalTorrent"];
        }
    }
    else
        [fDefaults setBool: state forKey: @"DeleteOriginalTorrent"];
}

- (void) setDownloadLocation: (id) sender
{
    //Download folder
    switch ([fFolderPopUp indexOfSelectedItem])
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

- (void) checkUpdate
{
    [fUpdater checkForUpdates: nil];
}

- (void) folderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: @"Select"];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetForDirectory: nil file: nil types: nil
        modalForWindow: [self window] modalDelegate: self didEndSelector:
        @selector(folderSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) setAutoImport: (id) sender
{
    int state = [fAutoImportCheck state];
    [fDefaults setBool: state forKey: @"AutoImport"];
    [fImportFolderPopUp setEnabled: state];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoImportSettingChange" object: self];
}

- (void) importFolderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: @"Select"];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetForDirectory: nil file: nil types: nil
        modalForWindow: [self window] modalDelegate: self didEndSelector:
        @selector(importFolderSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) setAutoSize: (id) sender
{
    [fDefaults setBool: [sender state] forKey: @"AutoSize"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoSizeSettingChange" object: self];
}

- (void) windowWillClose: (NSNotification *) notification
{
    [[self window] makeFirstResponder: nil];
}

@end

@implementation PrefsController (Private)

- (void) showGeneralPref: (id) sender
{
    [self setPrefView: fGeneralView];
}

- (void) showTransfersPref: (id) sender
{
    [self setPrefView: fTransfersView];
}

- (void) showBandwidthPref: (id) sender
{
    [self setPrefView: fBandwidthView];
}

- (void) showNetworkPref: (id) sender
{
    [self setPrefView: fNetworkView];
}

- (void) setPrefView: (NSView *) view
{
    NSWindow * window = [self window];
    
    NSRect windowRect = [window frame];
    int difference = [view frame].size.height - [[window contentView] frame].size.height;
    windowRect.origin.y -= difference;
    windowRect.size.height += difference;

    [window setTitle: [fToolbar selectedItemIdentifier]];
    
    [window setContentView: view];
    [view setHidden: YES];
    [window setFrame: windowRect display: YES animate: YES];
    [view setHidden: NO];
}

- (void) folderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    if (code == NSOKButton)
    {
        [fDownloadFolder release];
        fDownloadFolder = [[[openPanel filenames] objectAtIndex: 0] retain];
        
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
        [fDefaults setObject: fDownloadFolder forKey: @"DownloadFolder"];
        [fDefaults setObject: @"Constant" forKey: @"DownloadChoice"];
        
        [self updatePopUp];
    }
    else
    {
        //reset if cancelled
        NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
        if ([downloadChoice isEqualToString: @"Constant"])
            [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
        else if ([downloadChoice isEqualToString: @"Torrent"])
            [fFolderPopUp selectItemAtIndex: DOWNLOAD_TORRENT];
        else
            [fFolderPopUp selectItemAtIndex: DOWNLOAD_ASK];
    }
}

- (void) updatePopUp
{
    //get and resize the icon
    NSImage * icon = [[NSWorkspace sharedWorkspace] iconForFile: fDownloadFolder];
    [icon setScalesWhenResized: YES];
    [icon setSize: NSMakeSize(16.0, 16.0)];

    //update menu item
    NSMenuItem * menuItem = (NSMenuItem *) [fFolderPopUp itemAtIndex: 0];
    [menuItem setTitle: [fDownloadFolder lastPathComponent]];
    [menuItem setImage: icon];
}

- (void) importFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
   if (code == NSOKButton)
   {
       [fImportFolder release];
       fImportFolder = [[[openPanel filenames] objectAtIndex: 0] retain];

       [fDefaults setObject: fImportFolder forKey: @"AutoImportDirectory"];

       [self updateImportPopUp];
       
       [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoImportSettingChange" object: self];
   }
   [fImportFolderPopUp selectItemAtIndex: 0];
}

- (void) updateImportPopUp
{
    //get and resize the icon
    NSImage * icon = [[NSWorkspace sharedWorkspace] iconForFile: fImportFolder];
    [icon setScalesWhenResized: YES];
    [icon setSize: NSMakeSize(16.0, 16.0)];

    //update menu item
    NSMenuItem * menuItem = (NSMenuItem *) [fImportFolderPopUp itemAtIndex: 0];
    [menuItem setTitle: [fImportFolder lastPathComponent]];
    [menuItem setImage: icon];
}

@end
