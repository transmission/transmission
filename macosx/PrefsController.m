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
#import "UKKQueue.h"

#define DOWNLOAD_FOLDER     0
#define DOWNLOAD_TORRENT    2
#define DOWNLOAD_ASK        3

#define UPDATE_DAILY    0
#define UPDATE_WEEKLY   1
#define UPDATE_NEVER    2

#define TOOLBAR_GENERAL     @"TOOLBAR_GENERAL"
#define TOOLBAR_TRANSFERS   @"TOOLBAR_TRANSFERS"
#define TOOLBAR_BANDWIDTH   @"TOOLBAR_BANDWIDTH"
#define TOOLBAR_NETWORK     @"TOOLBAR_NETWORK"

@interface PrefsController (Private)

- (void) setPrefView: (id) sender;

- (void) folderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
- (void) importFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;

@end

@implementation PrefsController

- (id) initWithWindowNibName: (NSString *) name handle: (tr_handle_t *) handle
{
    if ((self = [self initWithWindowNibName: name]))
    {
        fDefaults = [NSUserDefaults standardUserDefaults];
        fHandle = handle;
        
        //checks for old version upload speed of -1
        if ([fDefaults integerForKey: @"UploadLimit"] < 0)
        {
            [fDefaults setInteger: 20 forKey: @"UploadLimit"];
            [fDefaults setBool: NO forKey: @"CheckUpload"];
        }
        
        //set auto import
        if ([fDefaults boolForKey: @"AutoImport"])
            [[UKKQueue sharedFileWatcher] addPath:
                [[fDefaults stringForKey: @"AutoImportDirectory"] stringByExpandingTildeInPath]];
        
        //set bind port
        int bindPort = [fDefaults integerForKey: @"BindPort"];
        tr_setBindPort(fHandle, bindPort);
        
        //set NAT
        if ([fDefaults boolForKey: @"NatTraversal"])
            tr_natTraversalEnable(fHandle);
        
        //actually set bandwidth limits
        [self applySpeedSettings: nil];
        
        //set play sound
        NSMutableArray * sounds = [NSMutableArray array];
        NSEnumerator * soundEnumerator,
                    * soundDirectoriesEnumerator = [[NSArray arrayWithObjects: @"System/Library/Sounds",
                            [NSHomeDirectory() stringByAppendingPathComponent: @"Library/Sounds"], nil] objectEnumerator];
        NSString * soundPath, * sound;
        
        //get list of all sounds and sort alphabetically
        while ((soundPath = [soundDirectoriesEnumerator nextObject]))
            if (soundEnumerator = [[NSFileManager defaultManager] enumeratorAtPath: soundPath])
                while ((sound = [soundEnumerator nextObject]))
                {
                    sound = [sound stringByDeletingPathExtension];
                    if ([NSSound soundNamed: sound])
                        [sounds addObject: sound];
                }
        
        fSounds = [[sounds sortedArrayUsingSelector: @selector(caseInsensitiveCompare:)] retain];
    }
    return self;
}

- (void) dealloc
{
    if (fNatStatusTimer)
        [fNatStatusTimer invalidate];
    [fSounds release];
    
    [super dealloc];
}

- (void) awakeFromNib
{
    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Preferences Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: NO];
    [[self window] setToolbar: fToolbar];
    [fToolbar setDisplayMode: NSToolbarDisplayModeIconAndLabel];
    [fToolbar setSizeMode: NSToolbarSizeModeRegular];

    [fToolbar setSelectedItemIdentifier: TOOLBAR_GENERAL];
    [self setPrefView: nil];
    
    //set download folder
    NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
    if ([downloadChoice isEqualToString: @"Constant"])
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
    else if ([downloadChoice isEqualToString: @"Torrent"])
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_TORRENT];
    else
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_ASK];
    
    [self updatePortStatus];
    
    fNatStatus = -1;
    [self updateNatStatus];
    fNatStatusTimer = [NSTimer scheduledTimerWithTimeInterval: 5.0 target: self
                        selector: @selector(updateNatStatus) userInfo: nil repeats: YES];

    //set update check
    NSString * updateCheck = [fDefaults stringForKey: @"UpdateCheck"];
    if ([updateCheck isEqualToString: @"Weekly"])
        [fUpdatePopUp selectItemAtIndex: UPDATE_WEEKLY];
    else if ([updateCheck isEqualToString: @"Never"])
        [fUpdatePopUp selectItemAtIndex: UPDATE_NEVER];
    else
        [fUpdatePopUp selectItemAtIndex: UPDATE_DAILY];
}

- (void) setUpdater: (SUUpdater *) updater
{
    fUpdater = updater;
}

- (NSToolbarItem *) toolbar: (NSToolbar *) toolbar itemForItemIdentifier: (NSString *) ident
                    willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item;
    item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if ([ident isEqualToString: TOOLBAR_GENERAL])
    {
        [item setLabel: NSLocalizedString(@"General", "Preferences -> General toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Preferences.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
    }
    else if ([ident isEqualToString: TOOLBAR_TRANSFERS])
    {
        [item setLabel: NSLocalizedString(@"Transfers", "Preferences -> Transfers toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Transfers.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
    }
    else if ([ident isEqualToString: TOOLBAR_BANDWIDTH])
    {
        [item setLabel: NSLocalizedString(@"Bandwidth", "Preferences -> Bandwidth toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Bandwidth.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
    }
    else if ([ident isEqualToString: TOOLBAR_NETWORK])
    {
        [item setLabel: NSLocalizedString(@"Network", "Preferences -> Network toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Network.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
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
    return [NSArray arrayWithObjects: TOOLBAR_GENERAL, TOOLBAR_TRANSFERS,
                                        TOOLBAR_BANDWIDTH, TOOLBAR_NETWORK, nil];
}

- (void) setPort: (id) sender
{
    tr_setBindPort(fHandle, [fDefaults integerForKey: @"BindPort"]);
    [self updateNatStatus];
    [self updatePortStatus];
}

- (void) updatePortStatus
{
    long sytemVersion;
    [fPortStatusField setStringValue: @""];
    [fPortStatusImage setImage: nil];
    
    Gestalt('sysv', & sytemVersion);
    if (sytemVersion >= 0x1040)
    {
        //NSXML features are unfortunately only available since Mac OS X v10.4
        PortChecker * checker = [[PortChecker alloc] initWithDelegate: self];

        [fPortStatusField setStringValue: [NSLocalizedString(@"Checking port status",
                                            "Preferences -> Network -> port status") stringByAppendingEllipsis]];
        [fPortStatusProgress startAnimation: self];
        
        [checker probePort: [fDefaults integerForKey: @"BindPort"]];
    }
}

- (void) portCheckerDidFinishProbing: (PortChecker *) portChecker
{
    [fPortStatusProgress stopAnimation: self];
    switch ([portChecker status])
    {
        case PORT_STATUS_OPEN:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is open", "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"GreenDot.tiff"]];
            break;
        case PORT_STATUS_STEALTH:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is stealth", "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"RedDot.tiff"]];
            break;
        case PORT_STATUS_CLOSED:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is closed", "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"RedDot.tiff"]];
            break;
        case PORT_STATUS_ERROR:
            [fPortStatusField setStringValue: NSLocalizedString(@"Unable to check port status",
                                                "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"YellowDot.tiff"]];
            break;
    }
    [portChecker release];
}

- (void) setNat: (id) sender
{
    [fDefaults boolForKey: @"NatTraversal"] ? tr_natTraversalEnable(fHandle) : tr_natTraversalDisable(fHandle);
    [self updateNatStatus];
}

- (void) updateNatStatus
{
    int status = tr_natTraversalStatus(fHandle);
    if (fNatStatus == status)
        return;
    fNatStatus = status;
    
    if (status == 2)
    {
        [fNatStatusField setStringValue: NSLocalizedString(@"Port successfully mapped",
                                            "Preferences -> Network -> port map status")];
        [fNatStatusImage setImage: [NSImage imageNamed: @"GreenDot.tiff"]];
    }
    else if (status == 3 || status == 4)
    {
        [fNatStatusField setStringValue: NSLocalizedString(@"Error mapping port",
                                            "Preferences -> Network -> port map status")];
        [fNatStatusImage setImage: [NSImage imageNamed: @"RedDot.tiff"]];
    }
    else
    {
        [fNatStatusField setStringValue: @""];
        [fNatStatusImage setImage: nil];
    }
    
    [self updatePortStatus];
}

- (void) applySpeedSettings: (id) sender
{
    if ([fDefaults boolForKey: @"SpeedLimit"])
    {
        tr_setUploadLimit(fHandle, [fDefaults integerForKey: @"SpeedLimitUploadLimit"]);
        tr_setDownloadLimit(fHandle, [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]);
    }
    else
    {
        tr_setUploadLimit(fHandle, [fDefaults boolForKey: @"CheckUpload"]
                                        ? [fDefaults integerForKey: @"UploadLimit"] : -1);
        tr_setDownloadLimit(fHandle, [fDefaults boolForKey: @"CheckDownload"]
                                        ? [fDefaults integerForKey: @"DownloadLimit"] : -1);
    }
}

- (void) setAutoSpeedLimitCheck: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoSpeedLimitChange" object: self];
}

#warning check if same value
- (void) setAutoSpeedLimitHour: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoSpeedLimitChange" object: self];
}

- (void) setBadge: (id) sender
{   
    [[NSNotificationCenter defaultCenter] postNotificationName: @"DockBadgeChange" object: self];
}

- (void) setSound: (id) sender
{
    //play sound when selecting
    NSString * soundName = [sender titleOfSelectedItem];
    NSSound * sound;
    if ((sound = [NSSound soundNamed: soundName]))
        [sound play];
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
    
    if (fUpdater)
        [fUpdater scheduleCheckWithInterval: seconds];
}

#warning out of range/wrong value
- (void) setQueueNumber: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"GlobalStartSettingChange" object: self];
}

- (void) setDownloadLocation: (id) sender
{
    //download folder
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
    if ([fDefaults boolForKey: @"AutoImport"])
        [[UKKQueue sharedFileWatcher] addPath:
            [[fDefaults stringForKey: @"AutoImportDirectory"] stringByExpandingTildeInPath]];
    else
        [[UKKQueue sharedFileWatcher] removePathFromQueue:
            [[fDefaults stringForKey: @"AutoImportDirectory"] stringByExpandingTildeInPath]];
    
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
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoSizeSettingChange" object: self];
}

- (void) helpForNetwork: (id) sender
{
    [[NSHelpManager sharedHelpManager] openHelpAnchor: @"PortForwarding"
        inBook: [[NSBundle mainBundle] objectForInfoDictionaryKey: @"CFBundleHelpBookName"]];
}

@end

@implementation PrefsController (Private)

- (void) setPrefView: (id) sender
{
    NSView * view = fGeneralView;
    if (sender)
    {
        NSString * identifier = [sender itemIdentifier];
        if ([identifier isEqualToString: TOOLBAR_TRANSFERS])
            view = fTransfersView;
        else if ([identifier isEqualToString: TOOLBAR_BANDWIDTH])
            view = fBandwidthView;
        else if ([identifier isEqualToString: TOOLBAR_NETWORK])
            view = fNetworkView;
        else;
    }
    
    NSWindow * window = [self window];
    if ([window contentView] == view)
        return;
    
    NSRect windowRect = [window frame];
    int difference = [view frame].size.height - [[window contentView] frame].size.height;
    windowRect.origin.y -= difference;
    windowRect.size.height += difference;
    
    [view setHidden: YES];
    [window setContentView: view];
    [window setFrame: windowRect display: YES animate: YES];
    [view setHidden: NO];
    
    //set title label
    if (sender)
        [window setTitle: [sender label]];
    else
    {
        NSToolbarItem * item;
        NSEnumerator * enumerator = [[fToolbar items] objectEnumerator];
        while ((item = [enumerator nextObject]))
            if ([[item itemIdentifier] isEqualToString: [fToolbar selectedItemIdentifier]])
            {
                [window setTitle: [item label]];
                break;
            }
    }
    
    //for network view make sure progress indicator hides itself
    if (view == fNetworkView && [fPortStatusImage image])
        [fPortStatusProgress setDisplayedWhenStopped: NO];
}

- (void) folderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    if (code == NSOKButton)
    {
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
        [fDefaults setObject: [[openPanel filenames] objectAtIndex: 0] forKey: @"DownloadFolder"];
        [fDefaults setObject: @"Constant" forKey: @"DownloadChoice"];
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

- (void) importFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    if (code == NSOKButton)
    {
        UKKQueue * sharedQueue = [UKKQueue sharedFileWatcher];
        [sharedQueue removePathFromQueue: [[fDefaults stringForKey: @"AutoImportDirectory"] stringByExpandingTildeInPath]];
        
        [fDefaults setObject: [[openPanel filenames] objectAtIndex: 0] forKey: @"AutoImportDirectory"];
        
        [sharedQueue addPath: [[fDefaults stringForKey: @"AutoImportDirectory"] stringByExpandingTildeInPath]];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoImportSettingChange" object: self];
    }
    [fImportFolderPopUp selectItemAtIndex: 0];
}

@end
