/******************************************************************************
 * Copyright (c) 2005-2019 Transmission authors and contributors
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

#import <Foundation/Foundation.h>

#import <Sparkle/Sparkle.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#import "VDKQueue.h"

#import "PrefsController.h"
#import "BlocklistDownloaderViewController.h"
#import "BlocklistScheduler.h"
#import "Controller.h"
#import "PortChecker.h"
#import "BonjourController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

#define DOWNLOAD_FOLDER     0
#define DOWNLOAD_TORRENT    2

#define RPC_IP_ADD_TAG      0
#define RPC_IP_REMOVE_TAG   1

#define TOOLBAR_GENERAL     @"TOOLBAR_GENERAL"
#define TOOLBAR_TRANSFERS   @"TOOLBAR_TRANSFERS"
#define TOOLBAR_GROUPS      @"TOOLBAR_GROUPS"
#define TOOLBAR_BANDWIDTH   @"TOOLBAR_BANDWIDTH"
#define TOOLBAR_PEERS       @"TOOLBAR_PEERS"
#define TOOLBAR_NETWORK     @"TOOLBAR_NETWORK"
#define TOOLBAR_REMOTE      @"TOOLBAR_REMOTE"

#define RPC_KEYCHAIN_SERVICE    "Transmission:Remote"
#define RPC_KEYCHAIN_NAME       "Remote"

#define WEBUI_URL   @"http://localhost:%ld/"

@interface PrefsController (Private)

- (void) setPrefView: (id) sender;

- (void) setKeychainPassword: (const char *) password forService: (const char *) service username: (const char *) username;

@end

@implementation PrefsController

- (id) initWithHandle: (tr_session *) handle
{
    if ((self = [super initWithWindowNibName: @"PrefsWindow"]))
    {
        fHandle = handle;

        fDefaults = [NSUserDefaults standardUserDefaults];

        //check for old version download location (before 1.1)
        NSString * choice;
        if ((choice = [fDefaults stringForKey: @"DownloadChoice"]))
        {
            [fDefaults setBool: [choice isEqualToString: @"Constant"] forKey: @"DownloadLocationConstant"];
            [fDefaults setBool: YES forKey: @"DownloadAsk"];

            [fDefaults removeObjectForKey: @"DownloadChoice"];
        }

        //check for old version blocklist (before 2.12)
        NSDate * blocklistDate;
        if ((blocklistDate = [fDefaults objectForKey: @"BlocklistLastUpdate"]))
        {
            [fDefaults setObject: blocklistDate forKey: @"BlocklistNewLastUpdateSuccess"];
            [fDefaults setObject: blocklistDate forKey: @"BlocklistNewLastUpdate"];
            [fDefaults removeObjectForKey: @"BlocklistLastUpdate"];

            NSURL * blocklistDir = [[[NSFileManager defaultManager] URLsForDirectory: NSApplicationDirectory inDomains: NSUserDomainMask][0] URLByAppendingPathComponent: @"Transmission/blocklists/"];
            [[NSFileManager defaultManager] moveItemAtURL: [blocklistDir URLByAppendingPathComponent: @"level1.bin"]
                toURL: [blocklistDir URLByAppendingPathComponent: [NSString stringWithUTF8String: DEFAULT_BLOCKLIST_FILENAME]]
                error: nil];
        }

        //save a new random port
        if ([fDefaults boolForKey: @"RandomPort"])
            [fDefaults setInteger: tr_sessionGetPeerPort(fHandle) forKey: @"BindPort"];

        //set auto import
        NSString * autoPath;
        if ([fDefaults boolForKey: @"AutoImport"] && (autoPath = [fDefaults stringForKey: @"AutoImportDirectory"]))
            [[(Controller *)[NSApp delegate] fileWatcherQueue] addPath: [autoPath stringByExpandingTildeInPath] notifyingAbout: VDKQueueNotifyAboutWrite];

        //set special-handling of magnet link add window checkbox
        [self updateShowAddMagnetWindowField];

        //set blocklist scheduler
        [[BlocklistScheduler scheduler] updateSchedule];

        //set encryption
        [self setEncryptionMode: nil];

        //update rpc whitelist
        [self updateRPCPassword];

        fRPCWhitelistArray = [[fDefaults arrayForKey: @"RPCWhitelist"] mutableCopy];
        if (!fRPCWhitelistArray)
            fRPCWhitelistArray = [NSMutableArray arrayWithObject: @"127.0.0.1"];
        [self updateRPCWhitelist];

        //reset old Sparkle settings from previous versions
        [fDefaults removeObjectForKey: @"SUScheduledCheckInterval"];
        if ([fDefaults objectForKey: @"CheckForUpdates"])
        {
            [[SUUpdater sharedUpdater] setAutomaticallyChecksForUpdates: [fDefaults boolForKey: @"CheckForUpdates"]];
            [fDefaults removeObjectForKey: @"CheckForUpdates"];
        }

        [self setAutoUpdateToBeta: nil];
    }

    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];

    [fPortStatusTimer invalidate];
    if (fPortChecker)
    {
        [fPortChecker cancelProbe];
    }
}

- (void) awakeFromNib
{
    fHasLoaded = YES;

    [[self window] setRestorationClass: [self class]];

    NSToolbar * toolbar = [[NSToolbar alloc] initWithIdentifier: @"Preferences Toolbar"];
    [toolbar setDelegate: self];
    [toolbar setAllowsUserCustomization: NO];
    [toolbar setDisplayMode: NSToolbarDisplayModeIconAndLabel];
    [toolbar setSizeMode: NSToolbarSizeModeRegular];
    [toolbar setSelectedItemIdentifier: TOOLBAR_GENERAL];
    [[self window] setToolbar: toolbar];

    [self setPrefView: nil];

    //set download folder
    [fFolderPopUp selectItemAtIndex: [fDefaults boolForKey: @"DownloadLocationConstant"] ? DOWNLOAD_FOLDER : DOWNLOAD_TORRENT];

    //set stop ratio
    [fRatioStopField setFloatValue: [fDefaults floatForKey: @"RatioLimit"]];

    //set idle seeding minutes
    [fIdleStopField setIntegerValue: [fDefaults integerForKey: @"IdleLimitMinutes"]];

    //set limits
    [self updateLimitFields];

    //set speed limit
    [fSpeedLimitUploadField setIntValue: [fDefaults integerForKey: @"SpeedLimitUploadLimit"]];
    [fSpeedLimitDownloadField setIntValue: [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]];

    //set port
    [fPortField setIntValue: [fDefaults integerForKey: @"BindPort"]];
    fNatStatus = -1;

    [self updatePortStatus];
    fPortStatusTimer = [NSTimer scheduledTimerWithTimeInterval: 5.0 target: self selector: @selector(updatePortStatus) userInfo: nil repeats: YES];

    //set peer connections
    [fPeersGlobalField setIntValue: [fDefaults integerForKey: @"PeersTotal"]];
    [fPeersTorrentField setIntValue: [fDefaults integerForKey: @"PeersTorrent"]];

    //set queue values
    [fQueueDownloadField setIntValue: [fDefaults integerForKey: @"QueueDownloadNumber"]];
    [fQueueSeedField setIntValue: [fDefaults integerForKey: @"QueueSeedNumber"]];
    [fStalledField setIntValue: [fDefaults integerForKey: @"StalledMinutes"]];

    //set blocklist
    NSString * blocklistURL = [fDefaults stringForKey: @"BlocklistURL"];
    if (blocklistURL)
        [fBlocklistURLField setStringValue: blocklistURL];

    [self updateBlocklistButton];
    [self updateBlocklistFields];

    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateLimitFields)
                                                 name: @"UpdateSpeedLimitValuesOutsidePrefs" object: nil];

    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateRatioStopField)
                                                 name: @"UpdateRatioStopValueOutsidePrefs" object: nil];

    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateLimitStopField)
                                                 name: @"UpdateIdleStopValueOutsidePrefs" object: nil];

    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateBlocklistFields)
        name: @"BlocklistUpdated" object: nil];

    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateBlocklistURLField)
        name: NSControlTextDidChangeNotification object: fBlocklistURLField];

    //set rpc port
    [fRPCPortField setIntValue: [fDefaults integerForKey: @"RPCPort"]];

    //set rpc password
    if (fRPCPassword)
        [fRPCPasswordField setStringValue: fRPCPassword];
}

- (NSToolbarItem *) toolbar: (NSToolbar *) toolbar itemForItemIdentifier: (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if ([ident isEqualToString: TOOLBAR_GENERAL])
    {
        [item setLabel: NSLocalizedString(@"General", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: NSImageNamePreferencesGeneral]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_TRANSFERS])
    {
        [item setLabel: NSLocalizedString(@"Transfers", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Transfers"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_GROUPS])
    {
        [item setLabel: NSLocalizedString(@"Groups", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Groups"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_BANDWIDTH])
    {
        [item setLabel: NSLocalizedString(@"Bandwidth", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Bandwidth"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_PEERS])
    {
        [item setLabel: NSLocalizedString(@"Peers", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: NSImageNameUserGroup]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_NETWORK])
    {
        [item setLabel: NSLocalizedString(@"Network", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: NSImageNameNetwork]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else if ([ident isEqualToString: TOOLBAR_REMOTE])
    {
        [item setLabel: NSLocalizedString(@"Remote", "Preferences -> toolbar item title")];
        [item setImage: [NSImage imageNamed: @"Remote"]];
        [item setTarget: self];
        [item setAction: @selector(setPrefView:)];
        [item setAutovalidates: NO];
    }
    else
    {
        return nil;
    }

    return item;
}

- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar *) toolbar
{
    return @[TOOLBAR_GENERAL, TOOLBAR_TRANSFERS, TOOLBAR_GROUPS, TOOLBAR_BANDWIDTH,
                                        TOOLBAR_PEERS, TOOLBAR_NETWORK, TOOLBAR_REMOTE];
}

- (NSArray *) toolbarSelectableItemIdentifiers: (NSToolbar *) toolbar
{
    return [self toolbarAllowedItemIdentifiers: toolbar];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) toolbar
{
    return [self toolbarAllowedItemIdentifiers: toolbar];
}

+ (void) restoreWindowWithIdentifier: (NSString *) identifier state: (NSCoder *) state completionHandler: (void (^)(NSWindow *, NSError *)) completionHandler
{
    NSWindow * window = [[(Controller *)[NSApp delegate] prefsController] window];
    completionHandler(window, nil);
}

//for a beta release, always use the beta appcast
#if defined(TR_BETA_RELEASE)
#define SPARKLE_TAG YES
#else
#define SPARKLE_TAG [fDefaults boolForKey: @"AutoUpdateBeta"]
#endif
- (void) setAutoUpdateToBeta: (id) sender
{
    // TODO: Support beta releases (if/when necessary)
}

- (void) setPort: (id) sender
{
    const tr_port port = [sender intValue];
    [fDefaults setInteger: port forKey: @"BindPort"];
    tr_sessionSetPeerPort(fHandle, port);

    fPeerPort = -1;
    [self updatePortStatus];
}

- (void) randomPort: (id) sender
{
    const tr_port port = tr_sessionSetPeerPortRandom(fHandle);
    [fDefaults setInteger: port forKey: @"BindPort"];
    [fPortField setIntValue: port];

    fPeerPort = -1;
    [self updatePortStatus];
}

- (void) setRandomPortOnStart: (id) sender
{
    tr_sessionSetPeerPortRandomOnStart(fHandle, [(NSButton *)sender state] == NSOnState);
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
    BOOL natStatusChanged = (fNatStatus != fwd);
    BOOL peerPortChanged = (fPeerPort != port);

    if (natStatusChanged || peerPortChanged)
    {
        fNatStatus = fwd;
        fPeerPort = port;

        [fPortStatusField setStringValue: @""];
        [fPortStatusImage setImage: nil];
        [fPortStatusProgress startAnimation: self];

        if (fPortChecker)
        {
            [fPortChecker cancelProbe];
        }
        BOOL delay = natStatusChanged || tr_sessionIsPortForwardingEnabled(fHandle);
        fPortChecker = [[PortChecker alloc] initForPort: fPeerPort delay: delay withDelegate: self];
    }
}

- (void) portCheckerDidFinishProbing: (PortChecker *) portChecker
{
    [fPortStatusProgress stopAnimation: self];
    switch ([fPortChecker status])
    {
        case PORT_STATUS_OPEN:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is open", "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: NSImageNameStatusAvailable]];
            break;
        case PORT_STATUS_CLOSED:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port is closed", "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: NSImageNameStatusUnavailable]];
            break;
        case PORT_STATUS_ERROR:
            [fPortStatusField setStringValue: NSLocalizedString(@"Port check site is down", "Preferences -> Network -> port status")];
            [fPortStatusImage setImage: [NSImage imageNamed: NSImageNameStatusPartiallyAvailable]];
            break;
        default:
            NSAssert1(NO, @"Port checker returned invalid status: %d", [fPortChecker status]);
            break;
    }
    fPortChecker = nil;
}

- (NSArray *) sounds
{
    NSMutableArray * sounds = [NSMutableArray array];

    NSArray * directories = NSSearchPathForDirectoriesInDomains(NSAllLibrariesDirectory, NSUserDomainMask | NSLocalDomainMask | NSSystemDomainMask, YES);

    for (__strong NSString * directory in directories)
    {
        directory = [directory stringByAppendingPathComponent: @"Sounds"];

        BOOL isDirectory;
        if ([[NSFileManager defaultManager] fileExistsAtPath: directory isDirectory: &isDirectory] && isDirectory)
        {
            NSArray * directoryContents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath: directory error: NULL];
            for (__strong NSString * sound in directoryContents)
            {
                sound = [sound stringByDeletingPathExtension];
                if ([NSSound soundNamed: sound])
                    [sounds addObject: sound];
            }
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

- (void) setUTP: (id) sender
{
    tr_sessionSetUTPEnabled(fHandle, [fDefaults boolForKey: @"UTPGlobal"]);
}

- (void) setPeersGlobal: (id) sender
{
    const int count = [sender intValue];
    [fDefaults setInteger: count forKey: @"PeersTotal"];
    tr_sessionSetPeerLimit(fHandle, count);
}

- (void) setPeersTorrent: (id) sender
{
    const int count = [sender intValue];
    [fDefaults setInteger: count forKey: @"PeersTorrent"];
    tr_sessionSetPeerLimitPerTorrent(fHandle, count);
}

- (void) setPEX: (id) sender
{
    tr_sessionSetPexEnabled(fHandle, [fDefaults boolForKey: @"PEXGlobal"]);
}

- (void) setDHT: (id) sender
{
    tr_sessionSetDHTEnabled(fHandle, [fDefaults boolForKey: @"DHTGlobal"]);
}

- (void) setLPD: (id) sender
{
    tr_sessionSetLPDEnabled(fHandle, [fDefaults boolForKey: @"LocalPeerDiscoveryGlobal"]);
}

- (void) setEncryptionMode: (id) sender
{
    const tr_encryption_mode mode = [fDefaults boolForKey: @"EncryptionPrefer"] ?
        ([fDefaults boolForKey: @"EncryptionRequire"] ? TR_ENCRYPTION_REQUIRED : TR_ENCRYPTION_PREFERRED) : TR_CLEAR_PREFERRED;
    tr_sessionSetEncryption(fHandle, mode);
}

- (void) setBlocklistEnabled: (id) sender
{
    tr_blocklistSetEnabled(fHandle, [fDefaults boolForKey: @"BlocklistNew"]);

    [[BlocklistScheduler scheduler] updateSchedule];

    [self updateBlocklistButton];
}

- (void) updateBlocklist: (id) sender
{
    [BlocklistDownloaderViewController downloadWithPrefsController: self];
}

- (void) setBlocklistAutoUpdate: (id) sender
{
    [[BlocklistScheduler scheduler] updateSchedule];
}

- (void) updateBlocklistFields
{
    const BOOL exists = tr_blocklistExists(fHandle);

    if (exists)
    {
        NSString * countString = [NSString formattedUInteger: tr_blocklistGetRuleCount(fHandle)];
        [fBlocklistMessageField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ IP address rules in list",
            "Prefs -> blocklist -> message"), countString]];
    }
    else
        [fBlocklistMessageField setStringValue: NSLocalizedString(@"A blocklist must first be downloaded",
            "Prefs -> blocklist -> message")];

    NSString * updatedDateString;
    if (exists)
    {
        NSDate * updatedDate = [fDefaults objectForKey: @"BlocklistNewLastUpdateSuccess"];

        if (updatedDate)
            updatedDateString = [NSDateFormatter localizedStringFromDate: updatedDate dateStyle: NSDateFormatterFullStyle timeStyle: NSDateFormatterShortStyle];
        else
            updatedDateString = NSLocalizedString(@"N/A", "Prefs -> blocklist -> message");
    }
    else
        updatedDateString = NSLocalizedString(@"Never", "Prefs -> blocklist -> message");

    [fBlocklistDateField setStringValue: [NSString stringWithFormat: @"%@: %@",
        NSLocalizedString(@"Last updated", "Prefs -> blocklist -> message"), updatedDateString]];
}

- (void) updateBlocklistURLField
{
    NSString * blocklistString = [fBlocklistURLField stringValue];

    [fDefaults setObject: blocklistString forKey: @"BlocklistURL"];
    tr_blocklistSetURL(fHandle, [blocklistString UTF8String]);

    [self updateBlocklistButton];
}

- (void) updateBlocklistButton
{
    NSString * blocklistString = [fDefaults objectForKey: @"BlocklistURL"];
    const BOOL enable = (blocklistString && ![blocklistString isEqualToString: @""])
                            && [fDefaults boolForKey: @"BlocklistNew"];
    [fBlocklistButton setEnabled: enable];
}

- (void) setAutoStartDownloads: (id) sender
{
    tr_sessionSetPaused(fHandle, ![fDefaults boolForKey: @"AutoStartDownload"]);
}

- (void) applySpeedSettings: (id) sender
{
    tr_sessionLimitSpeed(fHandle, TR_UP, [fDefaults boolForKey: @"CheckUpload"]);
    tr_sessionSetSpeedLimit_KBps(fHandle, TR_UP, [fDefaults integerForKey: @"UploadLimit"]);

    tr_sessionLimitSpeed(fHandle, TR_DOWN, [fDefaults boolForKey: @"CheckDownload"]);
    tr_sessionSetSpeedLimit_KBps(fHandle, TR_DOWN, [fDefaults integerForKey: @"DownloadLimit"]);

    [[NSNotificationCenter defaultCenter] postNotificationName: @"SpeedLimitUpdate" object: nil];
}

- (void) applyAltSpeedSettings
{
    tr_sessionSetAltSpeed_KBps(fHandle, TR_UP, [fDefaults integerForKey: @"SpeedLimitUploadLimit"]);
    tr_sessionSetAltSpeed_KBps(fHandle, TR_DOWN, [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]);

    [[NSNotificationCenter defaultCenter] postNotificationName: @"SpeedLimitUpdate" object: nil];
}

- (void) applyRatioSetting: (id) sender
{
    tr_sessionSetRatioLimited(fHandle, [fDefaults boolForKey: @"RatioCheck"]);
    tr_sessionSetRatioLimit(fHandle, [fDefaults floatForKey: @"RatioLimit"]);

    //reload main table for seeding progress
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];

    //reload global settings in inspector
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGlobalOptions" object: nil];
}

- (void) setRatioStop: (id) sender
{
    [fDefaults setFloat: [sender floatValue] forKey: @"RatioLimit"];

    [self applyRatioSetting: nil];
}

- (void) updateRatioStopField
{
    if (fHasLoaded)
        [fRatioStopField setFloatValue: [fDefaults floatForKey: @"RatioLimit"]];
}

- (void) updateRatioStopFieldOld
{
    [self updateRatioStopField];

    [self applyRatioSetting: nil];
}

- (void) applyIdleStopSetting: (id) sender
{
    tr_sessionSetIdleLimited(fHandle, [fDefaults boolForKey: @"IdleLimitCheck"]);
    tr_sessionSetIdleLimit(fHandle, [fDefaults integerForKey: @"IdleLimitMinutes"]);

    //reload main table for remaining seeding time
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];

    //reload global settings in inspector
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGlobalOptions" object: nil];
}

- (void) setIdleStop: (id) sender
{
    [fDefaults setInteger: [sender integerValue] forKey: @"IdleLimitMinutes"];

    [self applyIdleStopSetting: nil];
}

- (void) updateLimitStopField
{
    if (fHasLoaded)
        [fIdleStopField setIntegerValue: [fDefaults integerForKey: @"IdleLimitMinutes"]];
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
    [self applyAltSpeedSettings];
}

- (void) setAutoSpeedLimit: (id) sender
{
    tr_sessionUseAltSpeedTime(fHandle, [fDefaults boolForKey: @"SpeedLimitAuto"]);
}

- (void) setAutoSpeedLimitTime: (id) sender
{
    tr_sessionSetAltSpeedBegin(fHandle, [PrefsController dateToTimeSum: [fDefaults objectForKey: @"SpeedLimitAutoOnDate"]]);
    tr_sessionSetAltSpeedEnd(fHandle, [PrefsController dateToTimeSum: [fDefaults objectForKey: @"SpeedLimitAutoOffDate"]]);
}

- (void) setAutoSpeedLimitDay: (id) sender
{
    tr_sessionSetAltSpeedDay(fHandle, [[sender selectedItem] tag]);
}

+ (NSInteger) dateToTimeSum: (NSDate *) date
{
    NSCalendar * calendar = [NSCalendar currentCalendar];
    NSDateComponents * components = [calendar components: NSHourCalendarUnit | NSMinuteCalendarUnit fromDate: date];
    return [components hour] * 60 + [components minute];
}

+ (NSDate *) timeSumToDate: (NSInteger) sum
{
    NSDateComponents * comps = [[NSDateComponents alloc] init];
    [comps setHour: sum / 60];
    [comps setMinute: sum % 60];

    return [[NSCalendar currentCalendar] dateFromComponents: comps];
}

- (BOOL) control: (NSControl *) control textShouldBeginEditing: (NSText *) fieldEditor
{
    fInitialString = [control stringValue];

    return YES;
}

- (BOOL) control: (NSControl *) control didFailToFormatString: (NSString *) string errorDescription: (NSString *) error
{
    NSBeep();
    if (fInitialString)
    {
        [control setStringValue: fInitialString];
        fInitialString = nil;
    }
    return NO;
}

- (void) setBadge: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: self];
}

- (IBAction) openNotificationSystemPrefs: (NSButton *) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL fileURLWithPath:@"/System/Library/PreferencePanes/Notifications.prefPane"]];
}

- (void) resetWarnings: (id) sender
{
    [fDefaults removeObjectForKey: @"WarningDuplicate"];
    [fDefaults removeObjectForKey: @"WarningRemainingSpace"];
    [fDefaults removeObjectForKey: @"WarningFolderDataSameName"];
    [fDefaults removeObjectForKey: @"WarningResetStats"];
    [fDefaults removeObjectForKey: @"WarningCreatorBlankAddress"];
    [fDefaults removeObjectForKey: @"WarningCreatorPrivateBlankAddress"];
    [fDefaults removeObjectForKey: @"WarningRemoveTrackers"];
    [fDefaults removeObjectForKey: @"WarningInvalidOpen"];
    [fDefaults removeObjectForKey: @"WarningRemoveCompleted"];
    [fDefaults removeObjectForKey: @"WarningDonate"];
    //[fDefaults removeObjectForKey: @"WarningLegal"];
}

- (void) setDefaultForMagnets: (id) sender
{
    NSString * bundleID = [[NSBundle mainBundle] bundleIdentifier];
    const OSStatus result = LSSetDefaultHandlerForURLScheme((CFStringRef)@"magnet", (__bridge CFStringRef)bundleID);
    if (result != noErr)
        NSLog(@"Failed setting default magnet link handler");
}

- (void) setQueue: (id) sender
{
    //let's just do both - easier that way
    tr_sessionSetQueueEnabled(fHandle, TR_DOWN, [fDefaults boolForKey: @"Queue"]);
    tr_sessionSetQueueEnabled(fHandle, TR_UP, [fDefaults boolForKey: @"QueueSeed"]);

    //handle if any transfers switch from queued to paused
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateQueue" object: self];
}

- (void) setQueueNumber: (id) sender
{
    const NSInteger number = [sender intValue];
    const BOOL seed = sender == fQueueSeedField;

    [fDefaults setInteger: number forKey: seed ? @"QueueSeedNumber" : @"QueueDownloadNumber"];

    tr_sessionSetQueueSize(fHandle, seed ? TR_UP : TR_DOWN, number);
}

- (void) setStalled: (id) sender
{
    tr_sessionSetQueueStalledEnabled(fHandle, [fDefaults boolForKey: @"CheckStalled"]);

    //reload main table for stalled status
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
}

- (void) setStalledMinutes: (id) sender
{
    const NSInteger min = [sender intValue];
    [fDefaults setInteger: min forKey: @"StalledMinutes"];
    tr_sessionSetQueueStalledMinutes(fHandle, min);

    //reload main table for stalled status
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: self];
}

- (void) setDownloadLocation: (id) sender
{
    [fDefaults setBool: [fFolderPopUp indexOfSelectedItem] == DOWNLOAD_FOLDER forKey: @"DownloadLocationConstant"];
    [self updateShowAddMagnetWindowField];
}

- (void) folderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Preferences -> Open panel prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetModalForWindow: [self window] completionHandler: ^(NSInteger result) {
        if (result == NSFileHandlingPanelOKButton)
        {
            [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];

            NSString * folder = [[panel URLs][0] path];
            [fDefaults setObject: folder forKey: @"DownloadFolder"];
            [fDefaults setBool: YES forKey: @"DownloadLocationConstant"];
            [self updateShowAddMagnetWindowField];

            assert(folder.length > 0);
            tr_sessionSetDownloadDir(fHandle, [folder fileSystemRepresentation]);
        }
        else
        {
            //reset if cancelled
            [fFolderPopUp selectItemAtIndex: [fDefaults boolForKey: @"DownloadLocationConstant"] ? DOWNLOAD_FOLDER : DOWNLOAD_TORRENT];
        }
    }];
}

- (void) incompleteFolderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Preferences -> Open panel prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetModalForWindow: [self window] completionHandler: ^(NSInteger result) {
        if (result == NSFileHandlingPanelOKButton)
        {
            NSString * folder = [[panel URLs][0] path];
            [fDefaults setObject: folder forKey: @"IncompleteDownloadFolder"];

            assert(folder.length > 0);
            tr_sessionSetIncompleteDir(fHandle, [folder fileSystemRepresentation]);
        }
        [fIncompleteFolderPopUp selectItemAtIndex: 0];
    }];
}

- (void) doneScriptSheetShow:(id)sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Preferences -> Open panel prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: NO];
    [panel setCanCreateDirectories: NO];

    [panel beginSheetModalForWindow: [self window] completionHandler: ^(NSInteger result) {
        if (result == NSFileHandlingPanelOKButton)
        {
            NSString * filePath = [[panel URLs][0] path];

            assert(filePath.length > 0);

            [fDefaults setObject: filePath forKey: @"DoneScriptPath"];
            tr_sessionSetTorrentDoneScript(fHandle, [filePath fileSystemRepresentation]);

            [fDefaults setBool: YES forKey: @"DoneScriptEnabled"];
            tr_sessionSetTorrentDoneScriptEnabled(fHandle, YES);
        }
        [fDoneScriptPopUp selectItemAtIndex: 0];
    }];
}

- (void) setUseIncompleteFolder: (id) sender
{
    tr_sessionSetIncompleteDirEnabled(fHandle, [fDefaults boolForKey: @"UseIncompleteDownloadFolder"]);
}

- (void) setRenamePartialFiles: (id) sender
{
    tr_sessionSetIncompleteFileNamingEnabled(fHandle, [fDefaults boolForKey: @"RenamePartialFiles"]);
}

- (void) setShowAddMagnetWindow: (id) sender
{
    [fDefaults setBool: ([fShowMagnetAddWindowCheck state] == NSOnState) forKey: @"MagnetOpenAsk"];
}

- (void) updateShowAddMagnetWindowField
{
    if (![fDefaults boolForKey: @"DownloadLocationConstant"])
    {
        //always show the add window for magnet links when the download location is the same as the torrent file
        [fShowMagnetAddWindowCheck setState: NSOnState];
        [fShowMagnetAddWindowCheck setEnabled: NO];
    }
    else
    {
        [fShowMagnetAddWindowCheck setState: [fDefaults boolForKey: @"MagnetOpenAsk"]];
        [fShowMagnetAddWindowCheck setEnabled: YES];
    }
}

- (void) setDoneScriptEnabled: (id) sender
{
    if ([fDefaults boolForKey: @"DoneScriptEnabled"] && ![[NSFileManager defaultManager] fileExistsAtPath: [fDefaults stringForKey:@"DoneScriptPath"]])
    {
        // enabled is set but script file doesn't exist, so prompt for one and disable until they pick one
        [fDefaults setBool: NO forKey: @"DoneScriptEnabled"];
        [self doneScriptSheetShow: sender];
    }
    tr_sessionSetTorrentDoneScriptEnabled(fHandle, [fDefaults boolForKey: @"DoneScriptEnabled"]);
}

- (void) setAutoImport: (id) sender
{
    NSString * path;
    if ((path = [fDefaults stringForKey: @"AutoImportDirectory"]))
    {
        VDKQueue * watcherQueue = [(Controller *)[NSApp delegate] fileWatcherQueue];
        if ([fDefaults boolForKey: @"AutoImport"])
        {
            path = [path stringByExpandingTildeInPath];
            [watcherQueue addPath: path notifyingAbout: VDKQueueNotifyAboutWrite];
        }
        else
            [watcherQueue removeAllPaths];

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

    [panel beginSheetModalForWindow: [self window] completionHandler: ^(NSInteger result) {
        if (result == NSFileHandlingPanelOKButton)
        {
            VDKQueue * watcherQueue = [(Controller *)[NSApp delegate] fileWatcherQueue];
            [watcherQueue removeAllPaths];

            NSString * path = [[panel URLs][0] path];
            [fDefaults setObject: path forKey: @"AutoImportDirectory"];
            [watcherQueue addPath: [path stringByExpandingTildeInPath] notifyingAbout: VDKQueueNotifyAboutWrite];

            [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoImportSettingChange" object: self];
        }
        else
        {
            NSString * path = [fDefaults stringForKey: @"AutoImportDirectory"];
            if (!path)
                [fDefaults setBool: NO forKey: @"AutoImport"];
        }

        [fImportFolderPopUp selectItemAtIndex: 0];
    }];
}

- (void) setAutoSize: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoSizeSettingChange" object: self];
}

- (void) setRPCEnabled: (id) sender
{
    BOOL enable = [fDefaults boolForKey: @"RPC"];
    tr_sessionSetRPCEnabled(fHandle, enable);

    [self setRPCWebUIDiscovery: nil];
}

- (void) linkWebUI: (id) sender
{
    NSString * urlString = [NSString stringWithFormat: WEBUI_URL, [fDefaults integerForKey: @"RPCPort"]];
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: urlString]];
}

- (void) setRPCAuthorize: (id) sender
{
    tr_sessionSetRPCPasswordEnabled(fHandle, [fDefaults boolForKey: @"RPCAuthorize"]);
}

- (void) setRPCUsername: (id) sender
{
    tr_sessionSetRPCUsername(fHandle, [[fDefaults stringForKey: @"RPCUsername"] UTF8String]);
}

- (void) setRPCPassword: (id) sender
{
    fRPCPassword = [[sender stringValue] copy];

    const char * password = [[sender stringValue] UTF8String];
    [self setKeychainPassword: password forService: RPC_KEYCHAIN_SERVICE username: RPC_KEYCHAIN_NAME];

    tr_sessionSetRPCPassword(fHandle, password);
}

- (void) updateRPCPassword
{
    UInt32 passwordLength;
    const char * password = nil;
    SecKeychainFindGenericPassword(NULL, strlen(RPC_KEYCHAIN_SERVICE), RPC_KEYCHAIN_SERVICE,
        strlen(RPC_KEYCHAIN_NAME), RPC_KEYCHAIN_NAME, &passwordLength, (void **)&password, NULL);

    if (password != NULL)
    {
        char fullPassword[passwordLength+1];
        strncpy(fullPassword, password, passwordLength);
        fullPassword[passwordLength] = '\0';
        SecKeychainItemFreeContent(NULL, (void *)password);

        tr_sessionSetRPCPassword(fHandle, fullPassword);

        fRPCPassword = [[NSString alloc] initWithUTF8String: fullPassword];
        [fRPCPasswordField setStringValue: fRPCPassword];
    }
    else
        fRPCPassword = nil;
}

- (void) setRPCPort: (id) sender
{
    int port = [sender intValue];
    [fDefaults setInteger: port forKey: @"RPCPort"];
    tr_sessionSetRPCPort(fHandle, port);

    [self setRPCWebUIDiscovery: nil];
}

- (void) setRPCUseWhitelist: (id) sender
{
    tr_sessionSetRPCWhitelistEnabled(fHandle, [fDefaults boolForKey: @"RPCUseWhitelist"]);
}

- (void) setRPCWebUIDiscovery: (id) sender
{
    if ([fDefaults boolForKey:@"RPC"] && [fDefaults boolForKey: @"RPCWebDiscovery"])
        [[BonjourController defaultController] startWithPort: [fDefaults integerForKey: @"RPCPort"]];
    else
    {
        if ([BonjourController defaultControllerExists])
            [[BonjourController defaultController] stop];
    }
}

- (void) updateRPCWhitelist
{
    NSString * string = [fRPCWhitelistArray componentsJoinedByString: @","];
    tr_sessionSetRPCWhitelist(fHandle, [string UTF8String]);
}

- (void) addRemoveRPCIP: (id) sender
{
    //don't allow add/remove when currently adding - it leads to weird results
    if ([fRPCWhitelistTable editedRow] != -1)
        return;

    if ([[sender cell] tagForSegment: [sender selectedSegment]] == RPC_IP_REMOVE_TAG)
    {
        [fRPCWhitelistArray removeObjectsAtIndexes: [fRPCWhitelistTable selectedRowIndexes]];
        [fRPCWhitelistTable deselectAll: self];
        [fRPCWhitelistTable reloadData];

        [fDefaults setObject: fRPCWhitelistArray forKey: @"RPCWhitelist"];
        [self updateRPCWhitelist];
    }
    else
    {
        [fRPCWhitelistArray addObject: @""];
        [fRPCWhitelistTable reloadData];

        const int row = [fRPCWhitelistArray count] - 1;
        [fRPCWhitelistTable selectRowIndexes: [NSIndexSet indexSetWithIndex: row] byExtendingSelection: NO];
        [fRPCWhitelistTable editColumn: 0 row: row withEvent: nil select: YES];
    }
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableView
{
    return [fRPCWhitelistArray count];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    return fRPCWhitelistArray[row];
}

- (void) tableView: (NSTableView *) tableView setObjectValue: (id) object forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    NSArray * components = [object componentsSeparatedByString: @"."];
    NSMutableArray * newComponents = [NSMutableArray arrayWithCapacity: 4];

    //create better-formatted ip string
    BOOL valid = false;
    if ([components count] == 4)
    {
        valid = true;
        for (NSString * component in components)
        {
            if ([component isEqualToString: @"*"])
                [newComponents addObject: component];
            else
            {
                int num = [component intValue];
                if (num >= 0 && num < 256)
                    [newComponents addObject: [@(num) stringValue]];
                else
                {
                    valid = false;
                    break;
                }
            }
        }
    }

    NSString * newIP;
    if (valid)
    {
        newIP = [newComponents componentsJoinedByString: @"."];

        //don't allow the same ip address
        if ([fRPCWhitelistArray containsObject: newIP] && ![fRPCWhitelistArray[row] isEqualToString: newIP])
            valid = false;
    }

    if (valid)
    {
        fRPCWhitelistArray[row] = newIP;
        [fRPCWhitelistArray sortUsingSelector: @selector(compareNumeric:)];
    }
    else
    {
        NSBeep();
        if ([fRPCWhitelistArray[row] isEqualToString: @""])
            [fRPCWhitelistArray removeObjectAtIndex: row];
    }

    [fRPCWhitelistTable deselectAll: self];
    [fRPCWhitelistTable reloadData];

    [fDefaults setObject: fRPCWhitelistArray forKey: @"RPCWhitelist"];
    [self updateRPCWhitelist];
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [fRPCAddRemoveControl setEnabled: [fRPCWhitelistTable numberOfSelectedRows] > 0 forSegment: RPC_IP_REMOVE_TAG];
}

- (void) helpForScript: (id) sender
{
    [[NSHelpManager sharedHelpManager] openHelpAnchor: @"script"
        inBook: [[NSBundle mainBundle] objectForInfoDictionaryKey: @"CFBundleHelpBookName"]];
}

- (void) helpForPeers: (id) sender
{
    [[NSHelpManager sharedHelpManager] openHelpAnchor: @"peers"
        inBook: [[NSBundle mainBundle] objectForInfoDictionaryKey: @"CFBundleHelpBookName"]];
}

- (void) helpForNetwork: (id) sender
{
    [[NSHelpManager sharedHelpManager] openHelpAnchor: @"network"
        inBook: [[NSBundle mainBundle] objectForInfoDictionaryKey: @"CFBundleHelpBookName"]];
}

- (void) helpForRemote: (id) sender
{
    [[NSHelpManager sharedHelpManager] openHelpAnchor: @"remote"
        inBook: [[NSBundle mainBundle] objectForInfoDictionaryKey: @"CFBundleHelpBookName"]];
}

- (void) rpcUpdatePrefs
{
    //encryption
    const tr_encryption_mode encryptionMode = tr_sessionGetEncryption(fHandle);
    [fDefaults setBool: encryptionMode != TR_CLEAR_PREFERRED forKey: @"EncryptionPrefer"];
    [fDefaults setBool: encryptionMode == TR_ENCRYPTION_REQUIRED forKey: @"EncryptionRequire"];

    //download directory
    NSString * downloadLocation = [@(tr_sessionGetDownloadDir(fHandle)) stringByStandardizingPath];
    [fDefaults setObject: downloadLocation forKey: @"DownloadFolder"];

    NSString * incompleteLocation = [@(tr_sessionGetIncompleteDir(fHandle)) stringByStandardizingPath];
    [fDefaults setObject: incompleteLocation forKey: @"IncompleteDownloadFolder"];

    const BOOL useIncomplete = tr_sessionIsIncompleteDirEnabled(fHandle);
    [fDefaults setBool: useIncomplete forKey: @"UseIncompleteDownloadFolder"];

    const BOOL usePartialFileRanaming = tr_sessionIsIncompleteFileNamingEnabled(fHandle);
    [fDefaults setBool: usePartialFileRanaming forKey: @"RenamePartialFiles"];

    //utp
    const BOOL utp = tr_sessionIsUTPEnabled(fHandle);
    [fDefaults setBool: utp forKey: @"UTPGlobal"];

    //peers
    const uint16_t peersTotal = tr_sessionGetPeerLimit(fHandle);
    [fDefaults setInteger: peersTotal forKey: @"PeersTotal"];

    const uint16_t peersTorrent = tr_sessionGetPeerLimitPerTorrent(fHandle);
    [fDefaults setInteger: peersTorrent forKey: @"PeersTorrent"];

    //pex
    const BOOL pex = tr_sessionIsPexEnabled(fHandle);
    [fDefaults setBool: pex forKey: @"PEXGlobal"];

    //dht
    const BOOL dht = tr_sessionIsDHTEnabled(fHandle);
    [fDefaults setBool: dht forKey: @"DHTGlobal"];

    //lpd
    const BOOL lpd = tr_sessionIsLPDEnabled(fHandle);
    [fDefaults setBool: lpd forKey: @"LocalPeerDiscoveryGlobal"];

    //auto start
    const BOOL autoStart = !tr_sessionGetPaused(fHandle);
    [fDefaults setBool: autoStart forKey: @"AutoStartDownload"];

    //port
    const tr_port port = tr_sessionGetPeerPort(fHandle);
    [fDefaults setInteger: port forKey: @"BindPort"];

    const BOOL nat = tr_sessionIsPortForwardingEnabled(fHandle);
    [fDefaults setBool: nat forKey: @"NatTraversal"];

    fPeerPort = -1;
    fNatStatus = -1;
    [self updatePortStatus];

    const BOOL randomPort = tr_sessionGetPeerPortRandomOnStart(fHandle);
    [fDefaults setBool: randomPort forKey: @"RandomPort"];

    //speed limit - down
    const BOOL downLimitEnabled = tr_sessionIsSpeedLimited(fHandle, TR_DOWN);
    [fDefaults setBool: downLimitEnabled forKey: @"CheckDownload"];

    const int downLimit = tr_sessionGetSpeedLimit_KBps(fHandle, TR_DOWN);
    [fDefaults setInteger: downLimit forKey: @"DownloadLimit"];

    //speed limit - up
    const BOOL upLimitEnabled = tr_sessionIsSpeedLimited(fHandle, TR_UP);
    [fDefaults setBool: upLimitEnabled forKey: @"CheckUpload"];

    const int upLimit = tr_sessionGetSpeedLimit_KBps(fHandle, TR_UP);
    [fDefaults setInteger: upLimit forKey: @"UploadLimit"];

    //alt speed limit enabled
    const BOOL useAltSpeed = tr_sessionUsesAltSpeed(fHandle);
    [fDefaults setBool: useAltSpeed forKey: @"SpeedLimit"];

    //alt speed limit - down
    const int downLimitAlt = tr_sessionGetAltSpeed_KBps(fHandle, TR_DOWN);
    [fDefaults setInteger: downLimitAlt forKey: @"SpeedLimitDownloadLimit"];

    //alt speed limit - up
    const int upLimitAlt = tr_sessionGetAltSpeed_KBps(fHandle, TR_UP);
    [fDefaults setInteger: upLimitAlt forKey: @"SpeedLimitUploadLimit"];

    //alt speed limit schedule
    const BOOL useAltSpeedSched = tr_sessionUsesAltSpeedTime(fHandle);
    [fDefaults setBool: useAltSpeedSched forKey: @"SpeedLimitAuto"];

    NSDate * limitStartDate = [PrefsController timeSumToDate: tr_sessionGetAltSpeedBegin(fHandle)];
    [fDefaults setObject: limitStartDate forKey: @"SpeedLimitAutoOnDate"];

    NSDate * limitEndDate = [PrefsController timeSumToDate: tr_sessionGetAltSpeedEnd(fHandle)];
    [fDefaults setObject: limitEndDate forKey: @"SpeedLimitAutoOffDate"];

    const int limitDay = tr_sessionGetAltSpeedDay(fHandle);
    [fDefaults setInteger: limitDay forKey: @"SpeedLimitAutoDay"];

    //blocklist
    const BOOL blocklist = tr_blocklistIsEnabled(fHandle);
    [fDefaults setBool: blocklist forKey: @"BlocklistNew"];

    NSString * blocklistURL = @(tr_blocklistGetURL(fHandle));
    [fDefaults setObject: blocklistURL forKey: @"BlocklistURL"];

    //seed ratio
    const BOOL ratioLimited = tr_sessionIsRatioLimited(fHandle);
    [fDefaults setBool: ratioLimited forKey: @"RatioCheck"];

    const float ratioLimit = tr_sessionGetRatioLimit(fHandle);
    [fDefaults setFloat: ratioLimit forKey: @"RatioLimit"];

    //idle seed limit
    const BOOL idleLimited = tr_sessionIsIdleLimited(fHandle);
    [fDefaults setBool: idleLimited forKey: @"IdleLimitCheck"];

    const NSUInteger idleLimitMin = tr_sessionGetIdleLimit(fHandle);
    [fDefaults setInteger: idleLimitMin forKey: @"IdleLimitMinutes"];

    //queue
    const BOOL downloadQueue = tr_sessionGetQueueEnabled(fHandle, TR_DOWN);
    [fDefaults setBool: downloadQueue forKey: @"Queue"];

    const int downloadQueueNum = tr_sessionGetQueueSize(fHandle, TR_DOWN);
    [fDefaults setInteger: downloadQueueNum forKey: @"QueueDownloadNumber"];

    const BOOL seedQueue = tr_sessionGetQueueEnabled(fHandle, TR_UP);
    [fDefaults setBool: seedQueue forKey: @"QueueSeed"];

    const int seedQueueNum = tr_sessionGetQueueSize(fHandle, TR_UP);
    [fDefaults setInteger: seedQueueNum forKey: @"QueueSeedNumber"];

    const BOOL checkStalled = tr_sessionGetQueueStalledEnabled(fHandle);
    [fDefaults setBool: checkStalled forKey: @"CheckStalled"];

    const int stalledMinutes = tr_sessionGetQueueStalledMinutes(fHandle);
    [fDefaults setInteger: stalledMinutes forKey: @"StalledMinutes"];

    //done script
    const BOOL doneScriptEnabled = tr_sessionIsTorrentDoneScriptEnabled(fHandle);
    [fDefaults setBool: doneScriptEnabled forKey: @"DoneScriptEnabled"];

    NSString * doneScriptPath = @(tr_sessionGetTorrentDoneScript(fHandle));
    [fDefaults setObject: doneScriptPath forKey: @"DoneScriptPath"];

    //update gui if loaded
    if (fHasLoaded)
    {
        //encryption handled by bindings

        //download directory handled by bindings

        //utp handled by bindings

        [fPeersGlobalField setIntValue: peersTotal];
        [fPeersTorrentField setIntValue: peersTorrent];

        //pex handled by bindings

        //dht handled by bindings

        //lpd handled by bindings

        [fPortField setIntValue: port];
        //port forwarding (nat) handled by bindings
        //random port handled by bindings

        //limit check handled by bindings
        [fDownloadField setIntValue: downLimit];

        //limit check handled by bindings
        [fUploadField setIntValue: upLimit];

        [fSpeedLimitDownloadField setIntValue: downLimitAlt];

        [fSpeedLimitUploadField setIntValue: upLimitAlt];

        //speed limit schedule handled by bindings

        //speed limit schedule times and day handled by bindings

        [fBlocklistURLField setStringValue: blocklistURL];
        [self updateBlocklistButton];
        [self updateBlocklistFields];

        //ratio limit enabled handled by bindings
        [fRatioStopField setFloatValue: ratioLimit];

        //idle limit enabled handled by bindings
        [fIdleStopField setIntegerValue: idleLimitMin];

        //queues enabled handled by bindings
        [fQueueDownloadField setIntValue: downloadQueueNum];
        [fQueueSeedField setIntValue: seedQueueNum];

        //check stalled handled by bindings
        [fStalledField setIntValue: stalledMinutes];
    }

    [[NSNotificationCenter defaultCenter] postNotificationName: @"SpeedLimitUpdate" object: nil];

    //reload global settings in inspector
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGlobalOptions" object: nil];
}

@end

@implementation PrefsController (Private)

- (void) setPrefView: (id) sender
{
    NSString * identifier;
    if (sender)
    {
        identifier = [sender itemIdentifier];
        [[NSUserDefaults standardUserDefaults] setObject: identifier forKey: @"SelectedPrefView"];
    }
    else
        identifier = [[NSUserDefaults standardUserDefaults] stringForKey: @"SelectedPrefView"];

    NSView * view;
    if ([identifier isEqualToString: TOOLBAR_TRANSFERS])
        view = fTransfersView;
    else if ([identifier isEqualToString: TOOLBAR_GROUPS])
        view = fGroupsView;
    else if ([identifier isEqualToString: TOOLBAR_BANDWIDTH])
        view = fBandwidthView;
    else if ([identifier isEqualToString: TOOLBAR_PEERS])
        view = fPeersView;
    else if ([identifier isEqualToString: TOOLBAR_NETWORK])
        view = fNetworkView;
    else if ([identifier isEqualToString: TOOLBAR_REMOTE])
        view = fRemoteView;
    else
    {
        identifier = TOOLBAR_GENERAL; //general view is the default selected
        view = fGeneralView;
    }

    [[[self window] toolbar] setSelectedItemIdentifier: identifier];

    NSWindow * window = [self window];
    if ([window contentView] == view)
        return;

    NSRect windowRect = [window frame];
    const CGFloat difference = NSHeight([view frame]) - NSHeight([[window contentView] frame]);
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
        for (NSToolbarItem * item in [toolbar items])
            if ([[item itemIdentifier] isEqualToString: itemIdentifier])
            {
                [window setTitle: [item label]];
                break;
            }
    }
}

static NSString * getOSStatusDescription(OSStatus errorCode)
{
    return [[NSError errorWithDomain: NSOSStatusErrorDomain code: errorCode userInfo: NULL] description];
}

- (void) setKeychainPassword: (const char *) password forService: (const char *) service username: (const char *) username
{
    SecKeychainItemRef item = NULL;
    NSUInteger passwordLength = strlen(password);

    OSStatus result = SecKeychainFindGenericPassword(NULL, strlen(service), service, strlen(username), username, NULL, NULL, &item);
    if (result == noErr && item)
    {
        if (passwordLength > 0) //found, so update
        {
            result = SecKeychainItemModifyAttributesAndData(item, NULL, passwordLength, (const void *)password);
            if (result != noErr)
                NSLog(@"Problem updating Keychain item: %@", getOSStatusDescription(result));
        }
        else //remove the item
        {
            result = SecKeychainItemDelete(item);
            if (result != noErr)
            {
                NSLog(@"Problem removing Keychain item: %@", getOSStatusDescription(result));
            }
        }
    }
    else if (result == errSecItemNotFound) //not found, so add
    {
        if (passwordLength > 0)
        {
            result = SecKeychainAddGenericPassword(NULL, strlen(service), service, strlen(username), username,
                        passwordLength, (const void *)password, NULL);
            if (result != noErr)
                NSLog(@"Problem adding Keychain item: %@", getOSStatusDescription(result));
        }
    }
    else
        NSLog(@"Problem accessing Keychain: %@", getOSStatusDescription(result));
}

@end
