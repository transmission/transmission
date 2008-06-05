/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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
#import "BlocklistDownloader.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "UKKQueue.h"

#define DOWNLOAD_FOLDER     0
#define DOWNLOAD_TORRENT    2

#define RPC_ACCESS_ALLOW    0
#define RPC_ACCESS_DENY    1

#define RPC_IP_ADD_TAG      0
#define RPC_IP_REMOVE_TAG   1

#define UPDATE_SECONDS 86400

#define TOOLBAR_GENERAL     @"TOOLBAR_GENERAL"
#define TOOLBAR_TRANSFERS   @"TOOLBAR_TRANSFERS"
#define TOOLBAR_BANDWIDTH   @"TOOLBAR_BANDWIDTH"
#define TOOLBAR_PEERS       @"TOOLBAR_PEERS"
#define TOOLBAR_NETWORK     @"TOOLBAR_NETWORK"
#define TOOLBAR_REMOTE      @"TOOLBAR_REMOTE"

@interface PrefsController (Private)

- (void) setPrefView: (id) sender;

- (void) folderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
- (void) incompleteFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
- (void) importFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;

@end

@implementation PrefsController

- (id) initWithHandle: (tr_handle *) handle
{
    if ((self = [super initWithWindowNibName: @"PrefsWindow"]))
    {
        fDefaults = [NSUserDefaults standardUserDefaults];
        fHandle = handle;
        
        //checks for old version speeds of -1
        if ([fDefaults integerForKey: @"UploadLimit"] < 0)
        {
            [fDefaults setInteger: 20 forKey: @"UploadLimit"];
            [fDefaults setBool: NO forKey: @"CheckUpload"];
        }
        if ([fDefaults integerForKey: @"DownloadLimit"] < 0)
        {
            [fDefaults setInteger: 20 forKey: @"DownloadLimit"];
            [fDefaults setBool: NO forKey: @"CheckDownload"];
        }
        
        //check for old version download location (before 1.1)
        NSString * choice;
        if ((choice = [fDefaults stringForKey: @"DownloadChoice"]))
        {
            [fDefaults setBool: [choice isEqualToString: @"Constant"] forKey: @"DownloadLocationConstant"];
            [fDefaults setBool: YES forKey: @"DownloadAsk"];
            
            [fDefaults removeObjectForKey: @"DownloadChoice"];
        }
        
        //set check for update to right value
        [self setCheckForUpdate: nil];
        
        //set auto import
        NSString * autoPath;
        if ([fDefaults boolForKey: @"AutoImport"] && (autoPath = [fDefaults stringForKey: @"AutoImportDirectory"]))
            [[UKKQueue sharedFileWatcher] addPath: [autoPath stringByExpandingTildeInPath]];
        
        //set encryption
        [self setEncryptionMode: nil];
        
        //actually set bandwidth limits
        [self applySpeedSettings: nil];
        
        //update rpc access list
        fRPCAccessArray = [[fDefaults arrayForKey: @"RPCAccessList"] mutableCopy];
        if (!fRPCAccessArray)
            fRPCAccessArray = [[NSMutableArray arrayWithObject: [NSDictionary dictionaryWithObjectsAndKeys: @"127.0.0.1", @"IP",
                                [NSNumber numberWithBool: YES], @"Allow", nil]] retain];
        [self updateRPCAccessList];
    }
    
    return self;
}

- (tr_handle *) handle
{
    return fHandle;
}

- (void) dealloc
{
    if (fPortStatusTimer)
        [fPortStatusTimer invalidate];
    if (fPortChecker)
    {
        [fPortChecker cancelProbe];
        [fPortChecker release];
    }
    
    [fRPCAccessArray release];
    
    [super dealloc];
}

- (void) awakeFromNib
{
    fHasLoaded = YES;
    
    NSToolbar * toolbar = [[NSToolbar alloc] initWithIdentifier: @"Preferences Toolbar"];
    [toolbar setDelegate: self];
    [toolbar setAllowsUserCustomization: NO];
    [toolbar setDisplayMode: NSToolbarDisplayModeIconAndLabel];
    [toolbar setSizeMode: NSToolbarSizeModeRegular];
    [toolbar setSelectedItemIdentifier: TOOLBAR_GENERAL];
    [[self window] setToolbar: toolbar];
    [toolbar release];
    
    [self setPrefView: nil];
    
    if (![NSApp isOnLeopardOrBetter])
    {
        [fRPCAddRemoveControl sizeToFit];
        [fRPCAddRemoveControl setLabel: @"+" forSegment: RPC_IP_ADD_TAG];
        [fRPCAddRemoveControl setLabel: @"-" forSegment: RPC_IP_REMOVE_TAG];
    }
    
    //set download folder
    [fFolderPopUp selectItemAtIndex: [fDefaults boolForKey: @"DownloadLocationConstant"] ? DOWNLOAD_FOLDER : DOWNLOAD_TORRENT];
    
    //set stop ratio
    [self updateRatioStopField];
    
    //set limits
    [self updateLimitFields];
    
    //set speed limit
    [fSpeedLimitUploadField setIntValue: [fDefaults integerForKey: @"SpeedLimitUploadLimit"]];
    [fSpeedLimitDownloadField setIntValue: [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]];
    
    //set port
    [fPortField setIntValue: [fDefaults integerForKey: @"BindPort"]];
    fNatStatus = -1;
    
    [self updatePortStatus];
    fPortStatusTimer = [NSTimer scheduledTimerWithTimeInterval: 5.0 target: self
                        selector: @selector(updatePortStatus) userInfo: nil repeats: YES];
    
    //set peer connections
    [fPeersGlobalField setIntValue: [fDefaults integerForKey: @"PeersTotal"]];
    [fPeersTorrentField setIntValue: [fDefaults integerForKey: @"PeersTorrent"]];
    
    //set queue values
    [fQueueDownloadField setIntValue: [fDefaults integerForKey: @"QueueDownloadNumber"]];
    [fQueueSeedField setIntValue: [fDefaults integerForKey: @"QueueSeedNumber"]];
    [fStalledField setIntValue: [fDefaults integerForKey: @"StalledMinutes"]];
    
    //set blocklist
    [self updateBlocklistFields];
    
    //set rpc port
    [fRPCPortField setIntValue: [fDefaults integerForKey: @"RPCPort"]];
}

- (void) setUpdater: (SUUpdater *) updater
{
    fUpdater = updater;
}

- (NSToolbarItem *) toolbar: (NSToolbar *) toolbar itemForItemIdentifier: (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if ([ident isEqualToString: TOOLBAR_GENERAL])
    {
        [item setLabel: NSLocalizedString(@"General", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: [NSApp isOnLeopardOrBetter] ? NSImageNamePreferencesGeneral : @"Preferences.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_TRANSFERS])
    {
        [item setLabel: NSLocalizedString(@"Transfers", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Transfers.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_BANDWIDTH])
    {
        [item setLabel: NSLocalizedString(@"Bandwidth", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Bandwidth.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_PEERS])
    {
        [item setLabel: NSLocalizedString(@"Peers", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: [NSApp isOnLeopardOrBetter] ? NSImageNameUserGroup : @"Peers.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_NETWORK])
    {
        [item setLabel: NSLocalizedString(@"Network", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: [NSApp isOnLeopardOrBetter] ? NSImageNameNetwork : @"Network.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_REMOTE])
    {
        [item setLabel: NSLocalizedString(@"Remote", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: [NSApp isOnLeopardOrBetter] ? NSImageNameNetwork : @"Network.png"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else
    {
        [item release];
        return nil;
    }

    return [item autorelease];
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
    return [NSArray arrayWithObjects: TOOLBAR_GENERAL, TOOLBAR_TRANSFERS, TOOLBAR_BANDWIDTH,
                                        TOOLBAR_PEERS, TOOLBAR_NETWORK, TOOLBAR_REMOTE, nil];
}

//used by ipc
- (void) updatePortField
{
    [fPortField setIntValue: [fDefaults integerForKey: @"BindPort"]];
}

- (void) setPort: (id) sender
{
    int port = [sender intValue];
    [fDefaults setInteger: port forKey: @"BindPort"];
    tr_sessionSetPeerPort(fHandle, port);
    
    fPeerPort = -1;
    [self updatePortStatus];
}

- (void) setNat: (id) sender
{
    tr_sessionSetPortForwardingEnabled(fHandle, [fDefaults boolForKey: @"NatTraversal"]);
    
    fNatStatus = -1;
    [self updatePortStatus];
}

- (void) updatePortStatus
{
    const tr_port_forwarding fwd = tr_sessionGetPortForwarding(fHandle);
    const int port = tr_sessionGetPeerPort(fHandle);

    if (fNatStatus != fwd || fPeerPort != port )
    {
        fNatStatus = fwd;
        fPeerPort = port;
        
        [fPortStatusField setStringValue: [NSLocalizedString(@"Checking port status", "Preferences -> Network -> port status")
            stringByAppendingEllipsis]];
        [fPortStatusImage setImage: nil];
        [fPortStatusProgress startAnimation: self];
        
        if (fPortChecker)
        {
            [fPortChecker cancelProbe];
            [fPortChecker release];
        }
        fPortChecker = [[PortChecker alloc] initForPort: fPeerPort withDelegate: self];
    }
}

- (void) portCheckerDidFinishProbing: (PortChecker *) portChecker
{
    [fPortStatusProgress stopAnimation: self];
    switch ([fPortChecker status])
    {
        case PORT_STATUS_OPEN:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is open", "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"GreenDot.png"]];
            break;
        case PORT_STATUS_CLOSED:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is closed", "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"RedDot.png"]];
            break;
        case PORT_STATUS_STEALTH:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is stealth", "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"RedDot.png"]];
            break;
        case PORT_STATUS_ERROR:
            [fPortStatusField setStringValue: NSLocalizedString(@"Unable to check port status",
                                                "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: @"YellowDot.png"]];
            break;
    }
    [fPortChecker release];
    fPortChecker = nil;
}

- (NSArray *) sounds
{
    NSMutableArray * sounds = [NSMutableArray array];
    
    NSMutableArray * directories = [NSMutableArray arrayWithObjects: @"/System/Library/Sounds", @"/Library/Sounds", nil];
    if ([NSApp isOnLeopardOrBetter])
        [directories addObject: [NSHomeDirectory() stringByAppendingPathComponent: @"Library/Sounds"]];
    
    BOOL isDirectory;
    NSString * directory;
    NSEnumerator * enumerator = [directories objectEnumerator];
    while ((directory = [enumerator nextObject]))
        if ([[NSFileManager defaultManager] fileExistsAtPath: directory isDirectory: &isDirectory] && isDirectory)
        {
            NSString * sound;
            NSEnumerator * soundEnumerator = [[[NSFileManager defaultManager] directoryContentsAtPath: directory] objectEnumerator];
            while ((sound = [soundEnumerator nextObject]))
            {
                sound = [sound stringByDeletingPathExtension];
                if ([NSSound soundNamed: sound])
                    [sounds addObject: sound];
            }
        }
    
    return sounds;
}

- (void) setSound: (id) sender
{
    //play sound when selecting
    NSSound * sound;
    if ((sound = [NSSound soundNamed: [sender titleOfSelectedItem]]))
        [sound play];
}

- (void) setPeersGlobal: (id) sender
{
    int count = [sender intValue];
    [fDefaults setInteger: count forKey: @"PeersTotal"];
    tr_sessionSetPeerLimit(fHandle, count);
}

- (void) setPeersTorrent: (id) sender
{
    int count = [sender intValue];
    [fDefaults setInteger: count forKey: @"PeersTorrent"];
}

- (void) setPEX: (id) sender
{
    tr_sessionSetPexEnabled(fHandle, [fDefaults boolForKey: @"PEXGlobal"]);
}

- (void) setEncryptionMode: (id) sender
{
    tr_sessionSetEncryption(fHandle, [fDefaults boolForKey: @"EncryptionPrefer"] ? 
        ([fDefaults boolForKey: @"EncryptionRequire"] ? TR_ENCRYPTION_REQUIRED : TR_ENCRYPTION_PREFERRED) : TR_PLAINTEXT_PREFERRED);
}

- (void) setBlocklistEnabled: (id) sender
{
    BOOL enable = [sender state] == NSOnState;
    [fDefaults setBool: enable forKey: @"Blocklist"];
    tr_blocklistSetEnabled(fHandle, enable);
}

- (void) updateBlocklist: (id) sender
{
    [BlocklistDownloader downloadWithPrefsController: self];
}

- (void) updateBlocklistFields
{
    BOOL exists = tr_blocklistExists(fHandle);
    
    if (exists)
    {
        NSNumberFormatter * numberFormatter = [[NSNumberFormatter alloc] init];
        [numberFormatter setNumberStyle: NSNumberFormatterDecimalStyle];
        [numberFormatter setMaximumFractionDigits: 0];
        NSString * countString = [numberFormatter stringFromNumber: [NSNumber numberWithInt: tr_blocklistGetRuleCount(fHandle)]];
        [numberFormatter release];
        
        [fBlocklistMessageField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ IP address rules in list",
            "Prefs -> blocklist -> message"), countString]];
    }
    else 
        [fBlocklistMessageField setStringValue: NSLocalizedString(@"A blocklist must first be downloaded",
            "Prefs -> blocklist -> message")];
    
    [fBlocklistEnableCheck setEnabled: exists];
    [fBlocklistEnableCheck setState: exists && [fDefaults boolForKey: @"Blocklist"]];
}

- (void) applySpeedSettings: (id) sender
{
    if ([fDefaults boolForKey: @"SpeedLimit"])
    {
        tr_sessionSetSpeedLimitEnabled(fHandle, TR_UP, 1);
        tr_sessionSetSpeedLimit(fHandle, TR_UP, [fDefaults integerForKey: @"SpeedLimitUploadLimit"]);
        
        tr_sessionSetSpeedLimitEnabled(fHandle, TR_DOWN, 1);
        tr_sessionSetSpeedLimit(fHandle, TR_DOWN, [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]);
    }
    else
    {
        tr_sessionSetSpeedLimitEnabled(fHandle, TR_UP, [fDefaults boolForKey: @"CheckUpload"]);
        tr_sessionSetSpeedLimit(fHandle, TR_UP, [fDefaults integerForKey: @"UploadLimit"]);
        
        tr_sessionSetSpeedLimitEnabled(fHandle, TR_DOWN, [fDefaults boolForKey: @"CheckDownload"]);
        tr_sessionSetSpeedLimit(fHandle, TR_DOWN, [fDefaults integerForKey: @"DownloadLimit"]);
    }
}

- (void) applyRatioSetting: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
}

- (void) updateRatioStopField
{
    if (!fHasLoaded)
        return;
    
    [fRatioStopField setFloatValue: [fDefaults floatForKey: @"RatioLimit"]];
    
    [self applyRatioSetting: nil];
}

- (void) setRatioStop: (id) sender
{
    [fDefaults setFloat: [sender floatValue] forKey: @"RatioLimit"];
    [self applyRatioSetting: nil];
}

- (void) updateLimitFields
{
    if (!fHasLoaded)
        return;
    
    [fUploadField setIntValue: [fDefaults integerForKey: @"UploadLimit"]];
    [fDownloadField setIntValue: [fDefaults integerForKey: @"DownloadLimit"]];
}

- (void) setGlobalLimit: (id) sender
{
    [fDefaults setInteger: [sender intValue] forKey: sender == fUploadField ? @"UploadLimit" : @"DownloadLimit"];
    [self applySpeedSettings: self];
}

- (void) setSpeedLimit: (id) sender
{
    [fDefaults setInteger: [sender intValue] forKey: sender == fSpeedLimitUploadField
                                                        ? @"SpeedLimitUploadLimit" : @"SpeedLimitDownloadLimit"];
    [self applySpeedSettings: self];
}

- (void) setAutoSpeedLimit: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoSpeedLimitChange" object: self];
}

- (BOOL) control: (NSControl *) control textShouldBeginEditing: (NSText *) fieldEditor
{
    [fInitialString release];
    fInitialString = [[control stringValue] retain];
    
    return YES;
}

- (BOOL) control: (NSControl *) control didFailToFormatString: (NSString *) string errorDescription: (NSString *) error
{
    NSBeep();
    if (fInitialString)
    {
        [control setStringValue: fInitialString];
        [fInitialString release];
        fInitialString = nil;
    }
    return NO;
}

- (void) setBadge: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"DockBadgeChange" object: self];
}

- (void) resetWarnings: (id) sender
{
    [fDefaults setBool: YES forKey: @"WarningDuplicate"];
    [fDefaults setBool: YES forKey: @"WarningRemainingSpace"];
    [fDefaults setBool: YES forKey: @"WarningFolderDataSameName"];
    [fDefaults setBool: YES forKey: @"WarningResetStats"];
    [fDefaults setBool: YES forKey: @"WarningCreatorBlankAddress"];
    [fDefaults setBool: YES forKey: @"WarningRemoveBuiltInTracker"];
}

- (void) setCheckForUpdate: (id) sender
{
    NSTimeInterval seconds = [fDefaults boolForKey: @"CheckForUpdates"] ? UPDATE_SECONDS : 0;
    [fDefaults setInteger: seconds forKey: @"SUScheduledCheckInterval"];
    if (fUpdater)
        [fUpdater scheduleCheckWithInterval: seconds];
}

- (void) setQueue: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
}

- (void) setQueueNumber: (id) sender
{
    [fDefaults setInteger: [sender intValue] forKey: sender == fQueueDownloadField ? @"QueueDownloadNumber" : @"QueueSeedNumber"];
    [self setQueue: nil];
}

- (void) setStalled: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
}

- (void) setStalledMinutes: (id) sender
{
    [fDefaults setInteger: [sender intValue] forKey: @"StalledMinutes"];
    [self setStalled: nil];
}

- (void) setDownloadLocation: (id) sender
{
    [fDefaults setBool: [fFolderPopUp indexOfSelectedItem] == DOWNLOAD_FOLDER forKey: @"DownloadLocationConstant"];
}

- (void) folderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Preferences -> Open panel prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetForDirectory: nil file: nil types: nil
        modalForWindow: [self window] modalDelegate: self didEndSelector:
        @selector(folderSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) incompleteFolderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Preferences -> Open panel prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetForDirectory: nil file: nil types: nil
        modalForWindow: [self window] modalDelegate: self didEndSelector:
        @selector(incompleteFolderSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) setAutoImport: (id) sender
{
    NSString * path;
    if ((path = [fDefaults stringForKey: @"AutoImportDirectory"]))
    {
        path = [path stringByExpandingTildeInPath];
        if ([fDefaults boolForKey: @"AutoImport"])
            [[UKKQueue sharedFileWatcher] addPath: path];
        else
            [[UKKQueue sharedFileWatcher] removePathFromQueue: path];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoImportSettingChange" object: self];
    }
    else
        [self importFolderSheetShow: nil];
}

- (void) importFolderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Preferences -> Open panel prompt")];
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

- (void) setRPCEnabled: (id) sender
{
    tr_sessionSetRPCEnabled(fHandle, [fDefaults boolForKey: @"RPC"]);
}

- (void) setRPCPort: (id) sender
{
    int port = [sender intValue];
    [fDefaults setInteger: port forKey: @"RPCPort"];
    tr_sessionSetRPCPort(fHandle, port);
}

- (void) updateRPCAccessList
{
    NSMutableArray * components = [NSMutableArray arrayWithCapacity: [fRPCAccessArray count]];
    
    NSEnumerator * enumerator = [fRPCAccessArray objectEnumerator];
    NSDictionary * dict;
    while ((dict = [enumerator nextObject]))
        [components addObject: [NSString stringWithFormat: @"%c%@", [[dict objectForKey: @"Allow"] boolValue] ? '+' : '-',
                                [dict objectForKey: @"IP"]]];
    
    NSString * string = [components componentsJoinedByString: @","];
    
    char * error = NULL;
    if (tr_sessionSetRPCACL(fHandle, [string UTF8String], &error))
    {
        NSLog([NSString stringWithUTF8String: error]);
        tr_free(error);
    }
}

- (void) addRemoveRPCIP: (id) sender
{
    //don't allow add/remove when currently adding - it leads to weird results
    if ([fRPCAccessTable editedRow] != -1)
        return;
    
    if ([[sender cell] tagForSegment: [sender selectedSegment]] == RPC_IP_REMOVE_TAG)
    {
        [fRPCAccessArray removeObjectsAtIndexes: [fRPCAccessTable selectedRowIndexes]];
        [fRPCAccessTable deselectAll: self];
        [fRPCAccessTable reloadData];
        
        [fDefaults setObject: fRPCAccessArray forKey: @"RPCAccessList"];
        [self updateRPCAccessList];
    }
    else
    {
        [fRPCAccessArray addObject: [NSDictionary dictionaryWithObjectsAndKeys: @"", @"IP",
                                        [NSNumber numberWithBool: YES], @"Allow", nil]];
        [fRPCAccessTable reloadData];
        
        int row = [fRPCAccessArray count] - 1;
        [fRPCAccessTable selectRow: row byExtendingSelection: NO];
        [fRPCAccessTable editColumn: 0 row: row withEvent: nil select: YES];
    }
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableView
{
    return [fRPCAccessArray count];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    NSDictionary * dict = [fRPCAccessArray objectAtIndex: row];
    
    NSString * ident = [tableColumn identifier];
    if ([ident isEqualToString: @"Permission"])
    {
        int allow = [[dict objectForKey: @"Allow"] boolValue] ? RPC_ACCESS_ALLOW : RPC_ACCESS_DENY;
        return [NSNumber numberWithInt: allow];
    }
    else
        return [dict objectForKey: @"IP"];
}

- (void) tableView: (NSTableView *) tableView setObjectValue: (id) object forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    NSDictionary * oldDict = [fRPCAccessArray objectAtIndex: row];
    
    NSString * ident = [tableColumn identifier];
    if ([ident isEqualToString: @"Permission"])
    {
        NSNumber * allow = [NSNumber numberWithBool: [object intValue] == RPC_ACCESS_ALLOW];
        NSDictionary * newDict = [NSDictionary dictionaryWithObjectsAndKeys: [oldDict objectForKey: @"IP"], @"IP", allow, @"Allow", nil];
        [fRPCAccessArray replaceObjectAtIndex: row withObject: newDict];
    }
    else
    {
        NSArray * components = [object componentsSeparatedByString: @"."];
        NSMutableArray * newComponents = [NSMutableArray arrayWithCapacity: 4];
        
        //create better-formatted ip string
        if ([components count] == 4)
        {
            NSEnumerator * enumerator = [components objectEnumerator];
            NSString * component;
            while ((component = [enumerator nextObject]))
            {
                if ([component isEqualToString: @"*"])
                    [newComponents addObject: component];
                else
                    [newComponents addObject: [[NSNumber numberWithInt: [component intValue]] stringValue]];
            }
        }
        
        NSString * newIP = [newComponents componentsJoinedByString: @"."];
        
        //verify ip string
        if (!tr_sessionTestRPCACL(fHandle, [[@"+" stringByAppendingString: newIP] UTF8String], NULL))
        {
            NSDictionary * newDict = [NSDictionary dictionaryWithObjectsAndKeys: newIP, @"IP",
                                        [oldDict objectForKey: @"Allow"], @"Allow", nil];
            [fRPCAccessArray replaceObjectAtIndex: row withObject: newDict];
            
            NSSortDescriptor * descriptor = [[[NSSortDescriptor alloc] initWithKey: @"IP" ascending: YES selector: @selector(compareIP:)]
                                                autorelease];
            [fRPCAccessArray sortUsingDescriptors: [NSArray arrayWithObject: descriptor]];
        }
        else
        {
            NSBeep();
            
            if ([[oldDict objectForKey: @"IP"] isEqualToString: @""])
                [fRPCAccessArray removeObjectAtIndex: row];
        }
        
        [fRPCAccessTable deselectAll: self];
        [fRPCAccessTable reloadData];
    }
    
    [fDefaults setObject: fRPCAccessArray forKey: @"RPCAccessList"];
    [self updateRPCAccessList];
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [fRPCAddRemoveControl setEnabled: [fRPCAccessTable numberOfSelectedRows] > 0 forSegment: RPC_IP_REMOVE_TAG];
}

- (void) helpForPeers: (id) sender
{
    [[NSHelpManager sharedHelpManager] openHelpAnchor: @"PeersPrefs"
        inBook: [[NSBundle mainBundle] objectForInfoDictionaryKey: @"CFBundleHelpBookName"]];
}

- (void) helpForNetwork: (id) sender
{
    [[NSHelpManager sharedHelpManager] openHelpAnchor: @"NetworkPrefs"
        inBook: [[NSBundle mainBundle] objectForInfoDictionaryKey: @"CFBundleHelpBookName"]];
}

- (void) rpcUpdatePrefs
{
    //encryption
    tr_encryption_mode encryptionMode = tr_sessionGetEncryption(fHandle);
    [fDefaults setBool: encryptionMode != TR_PLAINTEXT_PREFERRED forKey: @"EncryptionPrefer"];
    [fDefaults setBool: encryptionMode == TR_ENCRYPTION_PREFERRED forKey: @"EncryptionPrefer"];
    
    //download directory
    #warning missing!
    
    //peers
    uint16_t peersTotal = tr_sessionGetPeerLimit(fHandle);
    [fDefaults setInteger: peersTotal forKey: @"PeersTotal"];
    
    //pex
    BOOL pex = tr_sessionIsPexEnabled(fHandle);
    [fDefaults setBool: pex forKey: @"PEXGlobal"];
    
    //port
    int port = tr_sessionGetPeerPort(fHandle);
    [fDefaults setInteger: port forKey: @"BindPort"];
    
    BOOL nat = tr_sessionIsPortForwardingEnabled(fHandle);
    [fDefaults setBool: nat forKey: @"NatTraversal"];
    
    fPeerPort = -1;
    fNatStatus = -1;
    [self updatePortStatus];
    
    //speed limit - down
    BOOL downLimitEnabled = tr_sessionIsSpeedLimitEnabled(fHandle, TR_DOWN);
    [fDefaults setBool: downLimitEnabled forKey: @"CheckDownload"];
    
    int downLimit = tr_sessionGetSpeedLimit(fHandle, TR_DOWN);
    [fDefaults setInteger: downLimit forKey: @"DownloadLimit"];
    
    //speed limit - up
    BOOL upLimitEnabled = tr_sessionIsSpeedLimitEnabled(fHandle, TR_UP);
    [fDefaults setBool: upLimitEnabled forKey: @"CheckUpload"];
    
    int upLimit = tr_sessionGetSpeedLimit(fHandle, TR_UP);
    [fDefaults setInteger: upLimit forKey: @"UploadLimit"];
    
    //update gui if loaded
    if (fHasLoaded)
    {
        //encryption handled by bindings
        
        [fPeersGlobalField setIntValue: peersTotal];
        
        //pex handled by bindings
        
        [fPortField setIntValue: port];
        //port forwarding (nat) handled by bindings
        
        //limit check handled by bindings
        [fDownloadField setIntValue: downLimit];
        
        //limit check handled by bindings
        [fUploadField setIntValue: upLimit];
    }
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
        else if ([identifier isEqualToString: TOOLBAR_PEERS])
            view = fPeersView;
        else if ([identifier isEqualToString: TOOLBAR_NETWORK])
            view = fNetworkView;
        else if ([identifier isEqualToString: TOOLBAR_REMOTE])
            view = fRemoteView;
        else; //general view already selected
    }
    
    NSWindow * window = [self window];
    if ([window contentView] == view)
        return;
    
    NSRect windowRect = [window frame];
    float difference = ([view frame].size.height - [[window contentView] frame].size.height) * [window userSpaceScaleFactor];
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
        NSToolbar * toolbar = [window toolbar];
        NSString * itemIdentifier = [toolbar selectedItemIdentifier];
        NSEnumerator * enumerator = [[toolbar items] objectEnumerator];
        NSToolbarItem * item;
        while ((item = [enumerator nextObject]))
            if ([[item itemIdentifier] isEqualToString: itemIdentifier])
            {
                [window setTitle: [item label]];
                break;
            }
    }
    
    //for network view make sure progress indicator hides itself (get around a Tiger bug)
    if (![NSApp isOnLeopardOrBetter] && view == fNetworkView && [fPortStatusImage image])
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
        [fFolderPopUp selectItemAtIndex: [fDefaults boolForKey: @"DownloadLocationConstant"] ? DOWNLOAD_FOLDER : DOWNLOAD_TORRENT];
    }
}

- (void) incompleteFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    if (code == NSOKButton)
        [fDefaults setObject: [[openPanel filenames] objectAtIndex: 0] forKey: @"IncompleteDownloadFolder"];
    [fIncompleteFolderPopUp selectItemAtIndex: 0];
}

- (void) importFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    NSString * path = [fDefaults stringForKey: @"AutoImportDirectory"];
    if (code == NSOKButton)
    {
        UKKQueue * sharedQueue = [UKKQueue sharedFileWatcher];
        if (path)
            [sharedQueue removePathFromQueue: [path stringByExpandingTildeInPath]];
        
        path = [[openPanel filenames] objectAtIndex: 0];
        [fDefaults setObject: path forKey: @"AutoImportDirectory"];
        [sharedQueue addPath: [path stringByExpandingTildeInPath]];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoImportSettingChange" object: self];
    }
    else if (!path)
        [fDefaults setBool: NO forKey: @"AutoImport"];
    
    [fImportFolderPopUp selectItemAtIndex: 0];
}

@end
