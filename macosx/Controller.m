/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2005-2012 Transmission authors and contributors
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
#import <IOKit/pwr_mgt/IOPMLib.h>
#import <Carbon/Carbon.h>
#import <libkern/OSAtomic.h>

#import "Controller.h"
#import "Torrent.h"
#import "TorrentGroup.h"
#import "TorrentTableView.h"
#import "CreatorWindowController.h"
#import "StatsWindowController.h"
#import "InfoWindowController.h"
#import "PrefsController.h"
#import "GroupsController.h"
#import "AboutWindowController.h"
#import "URLSheetWindowController.h"
#import "AddWindowController.h"
#import "AddMagnetWindowController.h"
#import "MessageWindowController.h"
#import "GlobalOptionsPopoverViewController.h"
#import "ButtonToolbarItem.h"
#import "GroupToolbarItem.h"
#import "ShareToolbarItem.h"
#import "ShareTorrentFileHelper.h"
#import "ToolbarSegmentedCell.h"
#import "BlocklistDownloader.h"
#import "StatusBarController.h"
#import "FilterBarController.h"
#import "FileRenameSheetController.h"
#import "BonjourController.h"
#import "Badger.h"
#import "DragOverlayWindow.h"
#import "NSApplicationAdditions.h"
#import "NSMutableArrayAdditions.h"
#import "NSStringAdditions.h"
#import "ExpandedPathToPathTransformer.h"
#import "ExpandedPathToIconTransformer.h"

#import "transmission.h"
#import "utils.h"
#import "variant.h"

#import "VDKQueue.h"
#import <Sparkle/Sparkle.h>

#define TOOLBAR_CREATE                  @"Toolbar Create"
#define TOOLBAR_OPEN_FILE               @"Toolbar Open"
#define TOOLBAR_OPEN_WEB                @"Toolbar Open Web"
#define TOOLBAR_REMOVE                  @"Toolbar Remove"
#define TOOLBAR_INFO                    @"Toolbar Info"
#define TOOLBAR_PAUSE_ALL               @"Toolbar Pause All"
#define TOOLBAR_RESUME_ALL              @"Toolbar Resume All"
#define TOOLBAR_PAUSE_RESUME_ALL        @"Toolbar Pause / Resume All"
#define TOOLBAR_PAUSE_SELECTED          @"Toolbar Pause Selected"
#define TOOLBAR_RESUME_SELECTED         @"Toolbar Resume Selected"
#define TOOLBAR_PAUSE_RESUME_SELECTED   @"Toolbar Pause / Resume Selected"
#define TOOLBAR_FILTER                  @"Toolbar Toggle Filter"
#define TOOLBAR_QUICKLOOK               @"Toolbar QuickLook"
#define TOOLBAR_SHARE                   @"Toolbar Share"

typedef enum
{
    TOOLBAR_PAUSE_TAG = 0,
    TOOLBAR_RESUME_TAG = 1
} toolbarGroupTag;

#define SORT_DATE       @"Date"
#define SORT_NAME       @"Name"
#define SORT_STATE      @"State"
#define SORT_PROGRESS   @"Progress"
#define SORT_TRACKER    @"Tracker"
#define SORT_ORDER      @"Order"
#define SORT_ACTIVITY   @"Activity"
#define SORT_SIZE       @"Size"

typedef enum
{
    SORT_ORDER_TAG = 0,
    SORT_DATE_TAG = 1,
    SORT_NAME_TAG = 2,
    SORT_PROGRESS_TAG = 3,
    SORT_STATE_TAG = 4,
    SORT_TRACKER_TAG = 5,
    SORT_ACTIVITY_TAG = 6,
    SORT_SIZE_TAG = 7
} sortTag;

typedef enum
{
    SORT_ASC_TAG = 0,
    SORT_DESC_TAG = 1
} sortOrderTag;

#define GROWL_DOWNLOAD_COMPLETE @"Download Complete"
#define GROWL_SEEDING_COMPLETE  @"Seeding Complete"
#define GROWL_AUTO_ADD          @"Torrent Auto Added"
#define GROWL_AUTO_SPEED_LIMIT  @"Speed Limit Auto Changed"

#define TORRENT_TABLE_VIEW_DATA_TYPE    @"TorrentTableViewDataType"

#define ROW_HEIGHT_REGULAR      62.0
#define ROW_HEIGHT_SMALL        22.0
#define WINDOW_REGULAR_WIDTH    468.0

#define STATUS_BAR_HEIGHT   21.0
#define FILTER_BAR_HEIGHT   23.0

#define UPDATE_UI_SECONDS   1.0

#define TRANSFER_PLIST  @"Transfers.plist"

#define WEBSITE_URL @"https://transmissionbt.com/"
#define FORUM_URL   @"https://forum.transmissionbt.com/"
#define GITHUB_URL  @"https://github.com/transmission/transmission"
#define DONATE_URL  @"https://transmissionbt.com/donate/"

#define DONATE_NAG_TIME (60 * 60 * 24 * 7)

static void altSpeedToggledCallback(tr_session * handle UNUSED, bool active, bool byUser, void * controller)
{
    NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys: [[NSNumber alloc] initWithBool: active], @"Active",
                                [[NSNumber alloc] initWithBool: byUser], @"ByUser", nil];
    [(Controller *)controller performSelectorOnMainThread: @selector(altSpeedToggledCallbackIsLimited:)
        withObject: dict waitUntilDone: NO];
}

static tr_rpc_callback_status rpcCallback(tr_session * handle UNUSED, tr_rpc_callback_type type, struct tr_torrent * torrentStruct,
                                            void * controller)
{
    [(Controller *)controller rpcCallback: type forTorrentStruct: torrentStruct];
    return TR_RPC_NOREMOVE; //we'll do the remove manually
}

static void sleepCallback(void * controller, io_service_t y, natural_t messageType, void * messageArgument)
{
    [(Controller *)controller sleepCallback: messageType argument: messageArgument];
}

// 2.90 was infected with ransomware which we now check for and attempt to remove
static void removeKeRangerRansomware()
{
    NSString * krBinaryResourcePath = [[NSBundle mainBundle] pathForResource: @"General" ofType: @"rtf"];

    NSString * userLibraryDirPath = [NSHomeDirectory() stringByAppendingString: @"/Library"];
    NSString * krLibraryKernelServicePath = [userLibraryDirPath stringByAppendingString: @"/kernel_service"];

    NSFileManager * fileManager = [NSFileManager defaultManager];

    NSArray<NSString *> * krFilePaths = @[
        krBinaryResourcePath ? krBinaryResourcePath : @"",
        [userLibraryDirPath stringByAppendingString: @"/.kernel_pid"],
        [userLibraryDirPath stringByAppendingString: @"/.kernel_time"],
        [userLibraryDirPath stringByAppendingString: @"/.kernel_complete"],
        krLibraryKernelServicePath
    ];

    BOOL foundKrFiles = NO;
    for (NSString * krFilePath in krFilePaths)
    {
        if ([krFilePath length] == 0 || ![fileManager fileExistsAtPath: krFilePath])
            continue;

        foundKrFiles = YES;
        break;
    }

    if (!foundKrFiles)
        return;

    NSLog(@"Detected OSX.KeRanger.A ransomware, trying to remove it");

    if ([fileManager fileExistsAtPath: krLibraryKernelServicePath])
    {
        // The forgiving way: kill process which has the file opened
        NSTask * lsofTask = [[NSTask alloc] init];
        [lsofTask setLaunchPath: @"/usr/sbin/lsof"];
        [lsofTask setArguments: @[@"-F", @"pid", @"--", krLibraryKernelServicePath]];
        [lsofTask setStandardOutput: [NSPipe pipe]];
        [lsofTask setStandardInput: [NSPipe pipe]];
        [lsofTask setStandardError: [lsofTask standardOutput]];
        [lsofTask launch];
        NSData * lsofOuputData = [[[lsofTask standardOutput] fileHandleForReading] readDataToEndOfFile];
        [lsofTask waitUntilExit];
        NSString * lsofOutput = [[[NSString alloc] initWithData: lsofOuputData encoding: NSUTF8StringEncoding] autorelease];
        for (NSString * line in [lsofOutput componentsSeparatedByString: @"\n"])
        {
            if (![line hasPrefix: @"p"])
                continue;
            const pid_t krProcessId = [[line substringFromIndex: 1] intValue];
            if (kill(krProcessId, SIGKILL) == -1)
                NSLog(@"Unable to forcibly terminate ransomware process (kernel_service, pid %d), please do so manually", (int)krProcessId);
        }
    }
    else
    {
        // The harsh way: kill all processes with matching name
        NSTask * killTask = [NSTask launchedTaskWithLaunchPath: @"/usr/bin/killall" arguments: @[@"-9", @"kernel_service"]];
        [killTask waitUntilExit];
        if ([killTask terminationStatus] != 0)
            NSLog(@"Unable to forcibly terminate ransomware process (kernel_service), please do so manually if it's currently running");
    }

    for (NSString * krFilePath in krFilePaths)
    {
        if ([krFilePath length] == 0 || ![fileManager fileExistsAtPath: krFilePath])
            continue;

        if (![fileManager removeItemAtPath: krFilePath error: NULL])
            NSLog(@"Unable to remove ransomware file at %@, please do so manually", krFilePath);
    }

    NSLog(@"OSX.KeRanger.A ransomware removal completed, proceeding to normal operation");
}

@implementation Controller

#warning remove ivars in header when 64-bit only (or it compiles in 32-bit mode)
@synthesize prefsController = fPrefsController;
@synthesize messageWindowController = fMessageController;
@synthesize fileWatcherQueue = fFileWatcherQueue;

+ (void) initialize
{
    removeKeRangerRansomware();

    //make sure another Transmission.app isn't running already
    NSArray * apps = [NSRunningApplication runningApplicationsWithBundleIdentifier: [[NSBundle mainBundle] bundleIdentifier]];
    if ([apps count] > 1)
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Transmission already running alert -> button")];
        [alert setMessageText: NSLocalizedString(@"Transmission is already running.",
                                                "Transmission already running alert -> title")];
        [alert setInformativeText: NSLocalizedString(@"There is already a copy of Transmission running. "
            "This copy cannot be opened until that instance is quit.", "Transmission already running alert -> message")];
        [alert setAlertStyle: NSCriticalAlertStyle];

        [alert runModal];
        [alert release];

        //kill ourselves right away
        exit(0);
    }

    [[NSUserDefaults standardUserDefaults] registerDefaults: [NSDictionary dictionaryWithContentsOfFile:
        [[NSBundle mainBundle] pathForResource: @"Defaults" ofType: @"plist"]]];

    //set custom value transformers
    ExpandedPathToPathTransformer * pathTransformer = [[[ExpandedPathToPathTransformer alloc] init] autorelease];
    [NSValueTransformer setValueTransformer: pathTransformer forName: @"ExpandedPathToPathTransformer"];

    ExpandedPathToIconTransformer * iconTransformer = [[[ExpandedPathToIconTransformer alloc] init] autorelease];
    [NSValueTransformer setValueTransformer: iconTransformer forName: @"ExpandedPathToIconTransformer"];

    //cover our asses
    if ([[NSUserDefaults standardUserDefaults] boolForKey: @"WarningLegal"])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle: NSLocalizedString(@"I Accept", "Legal alert -> button")];
        [alert addButtonWithTitle: NSLocalizedString(@"Quit", "Legal alert -> button")];
        [alert setMessageText: NSLocalizedString(@"Welcome to Transmission", "Legal alert -> title")];
        [alert setInformativeText: NSLocalizedString(@"Transmission is a file-sharing program."
            " When you run a torrent, its data will be made available to others by means of upload."
            " You and you alone are fully responsible for exercising proper judgement and abiding by your local laws.",
            "Legal alert -> message")];
        [alert setAlertStyle: NSInformationalAlertStyle];

        if ([alert runModal] == NSAlertSecondButtonReturn)
            exit(0);
        [alert release];

        [[NSUserDefaults standardUserDefaults] setBool: NO forKey: @"WarningLegal"];
    }
}

- (id) init
{
    if ((self = [super init]))
    {
        fDefaults = [NSUserDefaults standardUserDefaults];

        //checks for old version speeds of -1
        if ([fDefaults integerForKey: @"UploadLimit"] < 0)
        {
            [fDefaults removeObjectForKey: @"UploadLimit"];
            [fDefaults setBool: NO forKey: @"CheckUpload"];
        }
        if ([fDefaults integerForKey: @"DownloadLimit"] < 0)
        {
            [fDefaults removeObjectForKey: @"DownloadLimit"];
            [fDefaults setBool: NO forKey: @"CheckDownload"];
        }

        //upgrading from versions < 2.40: clear recent items
        [[NSDocumentController sharedDocumentController] clearRecentDocuments: nil];

        tr_variant settings;
        tr_variantInitDict(&settings, 41);
        tr_sessionGetDefaultSettings(&settings);

        const BOOL usesSpeedLimitSched = [fDefaults boolForKey: @"SpeedLimitAuto"];
        if (!usesSpeedLimitSched)
            tr_variantDictAddBool(&settings, TR_KEY_alt_speed_enabled, [fDefaults boolForKey: @"SpeedLimit"]);

        tr_variantDictAddInt(&settings, TR_KEY_alt_speed_up, [fDefaults integerForKey: @"SpeedLimitUploadLimit"]);
        tr_variantDictAddInt(&settings, TR_KEY_alt_speed_down, [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]);

        tr_variantDictAddBool(&settings, TR_KEY_alt_speed_time_enabled, [fDefaults boolForKey: @"SpeedLimitAuto"]);
        tr_variantDictAddInt(&settings, TR_KEY_alt_speed_time_begin, [PrefsController dateToTimeSum:
                                                                            [fDefaults objectForKey: @"SpeedLimitAutoOnDate"]]);
        tr_variantDictAddInt(&settings, TR_KEY_alt_speed_time_end, [PrefsController dateToTimeSum:
                                                                            [fDefaults objectForKey: @"SpeedLimitAutoOffDate"]]);
        tr_variantDictAddInt(&settings, TR_KEY_alt_speed_time_day, [fDefaults integerForKey: @"SpeedLimitAutoDay"]);

        tr_variantDictAddInt(&settings, TR_KEY_speed_limit_down, [fDefaults integerForKey: @"DownloadLimit"]);
        tr_variantDictAddBool(&settings, TR_KEY_speed_limit_down_enabled, [fDefaults boolForKey: @"CheckDownload"]);
        tr_variantDictAddInt(&settings, TR_KEY_speed_limit_up, [fDefaults integerForKey: @"UploadLimit"]);
        tr_variantDictAddBool(&settings, TR_KEY_speed_limit_up_enabled, [fDefaults boolForKey: @"CheckUpload"]);

        //hidden prefs
        if ([fDefaults objectForKey: @"BindAddressIPv4"])
            tr_variantDictAddStr(&settings, TR_KEY_bind_address_ipv4, [[fDefaults stringForKey: @"BindAddressIPv4"] UTF8String]);
        if ([fDefaults objectForKey: @"BindAddressIPv6"])
            tr_variantDictAddStr(&settings, TR_KEY_bind_address_ipv6, [[fDefaults stringForKey: @"BindAddressIPv6"] UTF8String]);

        tr_variantDictAddBool(&settings, TR_KEY_blocklist_enabled, [fDefaults boolForKey: @"BlocklistNew"]);
        if ([fDefaults objectForKey: @"BlocklistURL"])
            tr_variantDictAddStr(&settings, TR_KEY_blocklist_url, [[fDefaults stringForKey: @"BlocklistURL"] UTF8String]);
        tr_variantDictAddBool(&settings, TR_KEY_dht_enabled, [fDefaults boolForKey: @"DHTGlobal"]);
        tr_variantDictAddStr(&settings, TR_KEY_download_dir, [[[fDefaults stringForKey: @"DownloadFolder"]
                                                                    stringByExpandingTildeInPath] UTF8String]);
        tr_variantDictAddBool(&settings, TR_KEY_download_queue_enabled, [fDefaults boolForKey: @"Queue"]);
        tr_variantDictAddInt(&settings, TR_KEY_download_queue_size, [fDefaults integerForKey: @"QueueDownloadNumber"]);
        tr_variantDictAddInt(&settings, TR_KEY_idle_seeding_limit, [fDefaults integerForKey: @"IdleLimitMinutes"]);
        tr_variantDictAddBool(&settings, TR_KEY_idle_seeding_limit_enabled, [fDefaults boolForKey: @"IdleLimitCheck"]);
        tr_variantDictAddStr(&settings, TR_KEY_incomplete_dir, [[[fDefaults stringForKey: @"IncompleteDownloadFolder"]
                                                                    stringByExpandingTildeInPath] UTF8String]);
        tr_variantDictAddBool(&settings, TR_KEY_incomplete_dir_enabled, [fDefaults boolForKey: @"UseIncompleteDownloadFolder"]);
        tr_variantDictAddBool(&settings, TR_KEY_lpd_enabled, [fDefaults boolForKey: @"LocalPeerDiscoveryGlobal"]);
        tr_variantDictAddInt(&settings, TR_KEY_message_level, TR_LOG_DEBUG);
        tr_variantDictAddInt(&settings, TR_KEY_peer_limit_global, [fDefaults integerForKey: @"PeersTotal"]);
        tr_variantDictAddInt(&settings, TR_KEY_peer_limit_per_torrent, [fDefaults integerForKey: @"PeersTorrent"]);

        const BOOL randomPort = [fDefaults boolForKey: @"RandomPort"];
        tr_variantDictAddBool(&settings, TR_KEY_peer_port_random_on_start, randomPort);
        if (!randomPort)
            tr_variantDictAddInt(&settings, TR_KEY_peer_port, [fDefaults integerForKey: @"BindPort"]);

        //hidden pref
        if ([fDefaults objectForKey: @"PeerSocketTOS"])
            tr_variantDictAddStr(&settings, TR_KEY_peer_socket_tos, [[fDefaults stringForKey: @"PeerSocketTOS"] UTF8String]);

        tr_variantDictAddBool(&settings, TR_KEY_pex_enabled, [fDefaults boolForKey: @"PEXGlobal"]);
        tr_variantDictAddBool(&settings, TR_KEY_port_forwarding_enabled, [fDefaults boolForKey: @"NatTraversal"]);
        tr_variantDictAddBool(&settings, TR_KEY_queue_stalled_enabled, [fDefaults boolForKey: @"CheckStalled"]);
        tr_variantDictAddInt(&settings, TR_KEY_queue_stalled_minutes, [fDefaults integerForKey: @"StalledMinutes"]);
        tr_variantDictAddReal(&settings, TR_KEY_ratio_limit, [fDefaults floatForKey: @"RatioLimit"]);
        tr_variantDictAddBool(&settings, TR_KEY_ratio_limit_enabled, [fDefaults boolForKey: @"RatioCheck"]);
        tr_variantDictAddBool(&settings, TR_KEY_rename_partial_files, [fDefaults boolForKey: @"RenamePartialFiles"]);
        tr_variantDictAddBool(&settings, TR_KEY_rpc_authentication_required,  [fDefaults boolForKey: @"RPCAuthorize"]);
        tr_variantDictAddBool(&settings, TR_KEY_rpc_enabled,  [fDefaults boolForKey: @"RPC"]);
        tr_variantDictAddInt(&settings, TR_KEY_rpc_port, [fDefaults integerForKey: @"RPCPort"]);
        tr_variantDictAddStr(&settings, TR_KEY_rpc_username,  [[fDefaults stringForKey: @"RPCUsername"] UTF8String]);
        tr_variantDictAddBool(&settings, TR_KEY_rpc_whitelist_enabled,  [fDefaults boolForKey: @"RPCUseWhitelist"]);
        tr_variantDictAddBool(&settings, TR_KEY_rpc_host_whitelist_enabled, [fDefaults boolForKey: @"RPCUseHostWhitelist"]);
        tr_variantDictAddBool(&settings, TR_KEY_seed_queue_enabled, [fDefaults boolForKey: @"QueueSeed"]);
        tr_variantDictAddInt(&settings, TR_KEY_seed_queue_size, [fDefaults integerForKey: @"QueueSeedNumber"]);
        tr_variantDictAddBool(&settings, TR_KEY_start_added_torrents, [fDefaults boolForKey: @"AutoStartDownload"]);
        tr_variantDictAddBool(&settings, TR_KEY_script_torrent_done_enabled, [fDefaults boolForKey: @"DoneScriptEnabled"]);
        tr_variantDictAddStr(&settings, TR_KEY_script_torrent_done_filename, [[fDefaults stringForKey: @"DoneScriptPath"] UTF8String]);
        tr_variantDictAddBool(&settings, TR_KEY_utp_enabled, [fDefaults boolForKey: @"UTPGlobal"]);

        // TODO: Add to GUI
        if ([fDefaults objectForKey: @"RPCHostWhitelist"])
            tr_variantDictAddStr(&settings, TR_KEY_rpc_host_whitelist, [[fDefaults stringForKey: @"RPCHostWhitelist"] UTF8String]);

        NSString * kbString, * mbString, * gbString, * tbString;
        if ([NSApp isOnMountainLionOrBetter])
        {
            NSByteCountFormatter * unitFormatter = [[NSByteCountFormatterMtLion alloc] init];
            [unitFormatter setIncludesCount: NO];
            [unitFormatter setAllowsNonnumericFormatting: NO];

            [unitFormatter setAllowedUnits: NSByteCountFormatterUseKB];
            kbString = [unitFormatter stringFromByteCount: 17]; //use a random value to avoid possible pluralization issues with 1 or 0 (an example is if we use 1 for bytes, we'd get "byte" when we'd want "bytes" for the generic libtransmission value at least)

            [unitFormatter setAllowedUnits: NSByteCountFormatterUseMB];
            mbString = [unitFormatter stringFromByteCount: 17];

            [unitFormatter setAllowedUnits: NSByteCountFormatterUseGB];
            gbString = [unitFormatter stringFromByteCount: 17];

            [unitFormatter setAllowedUnits: NSByteCountFormatterUseTB];
            tbString = [unitFormatter stringFromByteCount: 17];

            [unitFormatter release];
        }
        else
        {
            kbString = NSLocalizedString(@"KB", "file/memory size - kilobytes");
            mbString = NSLocalizedString(@"MB", "file/memory size - megabytes");
            gbString = NSLocalizedString(@"GB", "file/memory size - gigabytes");
            tbString = NSLocalizedString(@"TB", "file/memory size - terabytes");
        }

        tr_formatter_size_init(1000, [kbString UTF8String],
                                        [mbString UTF8String],
                                        [gbString UTF8String],
                                        [tbString UTF8String]);

        tr_formatter_speed_init(1000, [NSLocalizedString(@"KB/s", "Transfer speed (kilobytes per second)") UTF8String],
                                        [NSLocalizedString(@"MB/s", "Transfer speed (megabytes per second)") UTF8String],
                                        [NSLocalizedString(@"GB/s", "Transfer speed (gigabytes per second)") UTF8String],
                                        [NSLocalizedString(@"TB/s", "Transfer speed (terabytes per second)") UTF8String]); //why not?

        tr_formatter_mem_init(1000, [kbString UTF8String],
                                    [mbString UTF8String],
                                    [gbString UTF8String],
                                    [tbString UTF8String]);

        const char * configDir = tr_getDefaultConfigDir("Transmission");
        fLib = tr_sessionInit(configDir, YES, &settings);
        tr_variantFree(&settings);

        fConfigDirectory = [[NSString alloc] initWithUTF8String: configDir];

        [NSApp setDelegate: self];

        //register for magnet URLs (has to be in init)
        [[NSAppleEventManager sharedAppleEventManager] setEventHandler: self andSelector: @selector(handleOpenContentsEvent:replyEvent:)
            forEventClass: kInternetEventClass andEventID: kAEGetURL];

        fTorrents = [[NSMutableArray alloc] init];
        fDisplayedTorrents = [[NSMutableArray alloc] init];

        fInfoController = [[InfoWindowController alloc] init];

        //needs to be done before init-ing the prefs controller
        fFileWatcherQueue = [[VDKQueue alloc] init];
        [fFileWatcherQueue setDelegate: self];

        fPrefsController = [[PrefsController alloc] initWithHandle: fLib];

        fQuitting = NO;
        fGlobalPopoverShown = NO;
        fSoundPlaying = NO;

        tr_sessionSetAltSpeedFunc(fLib, altSpeedToggledCallback, self);
        if (usesSpeedLimitSched)
            [fDefaults setBool: tr_sessionUsesAltSpeed(fLib) forKey: @"SpeedLimit"];

        tr_sessionSetRPCCallback(fLib, rpcCallback, self);

        [GrowlApplicationBridge setGrowlDelegate: self];

        [[SUUpdater sharedUpdater] setDelegate: self];
        fQuitRequested = NO;

        fPauseOnLaunch = (GetCurrentKeyModifiers() & (optionKey | rightOptionKey)) != 0;
    }
    return self;
}

- (void) awakeFromNib
{
    NSToolbar * toolbar = [[NSToolbar alloc] initWithIdentifier: @"TRMainToolbar"];
    [toolbar setDelegate: self];
    [toolbar setAllowsUserCustomization: YES];
    [toolbar setAutosavesConfiguration: YES];
    [toolbar setDisplayMode: NSToolbarDisplayModeIconOnly];
    [fWindow setToolbar: toolbar];
    [toolbar release];

    [fWindow setDelegate: self]; //do manually to avoid placement issue

    [fWindow makeFirstResponder: fTableView];
    [fWindow setExcludedFromWindowsMenu: YES];

    //set table size
    const BOOL small = [fDefaults boolForKey: @"SmallView"];
    if (small)
        [fTableView setRowHeight: ROW_HEIGHT_SMALL];
    [fTableView setUsesAlternatingRowBackgroundColors: !small];

    [fWindow setContentBorderThickness: NSMinY([[fTableView enclosingScrollView] frame]) forEdge: NSMinYEdge];
    [fWindow setMovableByWindowBackground: YES];

    [[fTotalTorrentsField cell] setBackgroundStyle: NSBackgroundStyleRaised];

    //set up filter bar
    [self showFilterBar: [fDefaults boolForKey: @"FilterBar"] animate: NO];

    //set up status bar
    [self showStatusBar: [fDefaults boolForKey: @"StatusBar"] animate: NO];

    [fActionButton setToolTip: NSLocalizedString(@"Shortcuts for changing global settings.",
                                "Main window -> 1st bottom left button (action) tooltip")];
    [fSpeedLimitButton setToolTip: NSLocalizedString(@"Speed Limit overrides the total bandwidth limits with its own limits.",
                                "Main window -> 2nd bottom left button (turtle) tooltip")];
    [fClearCompletedButton setToolTip: NSLocalizedString(@"Remove all transfers that have completed seeding.",
                                "Main window -> 3rd bottom left button (remove all) tooltip")];

    [fTableView registerForDraggedTypes: [NSArray arrayWithObject: TORRENT_TABLE_VIEW_DATA_TYPE]];
    [fWindow registerForDraggedTypes: [NSArray arrayWithObjects: NSFilenamesPboardType, NSURLPboardType, nil]];

    //sort the sort menu items (localization is from strings file)
    NSMutableArray * sortMenuItems = [NSMutableArray arrayWithCapacity: 7];
    NSUInteger sortMenuIndex = 0;
    BOOL foundSortItem = NO;
    for (NSMenuItem * item in [fSortMenu itemArray])
    {
        //assume all sort items are together and the Queue Order item is first
        if ([item action] == @selector(setSort:) && [item tag] != SORT_ORDER_TAG)
        {
            [sortMenuItems addObject: item];
            [fSortMenu removeItemAtIndex: sortMenuIndex];
            foundSortItem = YES;
        }
        else
        {
            if (foundSortItem)
                break;
            ++sortMenuIndex;
        }
    }

    [sortMenuItems sortUsingDescriptors: [NSArray arrayWithObject: [NSSortDescriptor sortDescriptorWithKey: @"title" ascending: YES selector: @selector(localizedCompare:)]]];

    for (NSMenuItem * item in sortMenuItems)
        [fSortMenu insertItem: item atIndex: sortMenuIndex++];

    //you would think this would be called later in this method from updateUI, but it's not reached in awakeFromNib
    //this must be called after showStatusBar:
    [fStatusBar updateWithDownload: 0.0 upload: 0.0];

    //this should also be after the rest of the setup
    [self updateForAutoSize];

    //register for sleep notifications
    IONotificationPortRef notify;
    io_object_t iterator;
    if ((fRootPort = IORegisterForSystemPower(self, & notify, sleepCallback, &iterator)))
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notify), kCFRunLoopCommonModes);
    else
        NSLog(@"Could not IORegisterForSystemPower");

    //load previous transfers
    NSString * historyFile = [fConfigDirectory stringByAppendingPathComponent: TRANSFER_PLIST];
    NSArray * history = [NSArray arrayWithContentsOfFile: historyFile];
    if (!history)
    {
        //old version saved transfer info in prefs file
        if ((history = [fDefaults arrayForKey: @"History"]))
            [fDefaults removeObjectForKey: @"History"];
    }

    if (history)
    {
        NSMutableArray * waitToStartTorrents = [NSMutableArray arrayWithCapacity: (([history count] > 0 && !fPauseOnLaunch) ? [history count]-1 : 0)]; //theoretical max without doing a lot of work

        for (NSDictionary * historyItem in history)
        {
            Torrent * torrent;
            if ((torrent = [[Torrent alloc] initWithHistory: historyItem lib: fLib forcePause: fPauseOnLaunch]))
            {
                [fTorrents addObject: torrent];

                NSNumber * waitToStart;
                if (!fPauseOnLaunch && (waitToStart = [historyItem objectForKey: @"WaitToStart"]) && [waitToStart boolValue])
                    [waitToStartTorrents addObject: torrent];

                [torrent release];
            }
        }

        //now that all are loaded, let's set those in the queue to waiting
        for (Torrent * torrent in waitToStartTorrents)
            [torrent startTransfer];
    }

    fBadger = [[Badger alloc] initWithLib: fLib];

    if ([NSApp isOnMountainLionOrBetter])
        [[NSUserNotificationCenterMtLion defaultUserNotificationCenter] setDelegate: self];

    // remove Share menu items
    if (![NSApp isOnMountainLionOrBetter]) {
        [[fShareMenuItem menu] removeItem:fShareMenuItem];
        [[fShareContextMenuItem menu] removeItem:fShareContextMenuItem];
    }

    //observe notifications
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];

    [nc addObserver: self selector: @selector(updateUI)
                    name: @"UpdateUI" object: nil];

    [nc addObserver: self selector: @selector(torrentFinishedDownloading:)
                    name: @"TorrentFinishedDownloading" object: nil];

    [nc addObserver: self selector: @selector(torrentRestartedDownloading:)
                    name: @"TorrentRestartedDownloading" object: nil];

    [nc addObserver: self selector: @selector(torrentFinishedSeeding:)
                    name: @"TorrentFinishedSeeding" object: nil];

    [nc addObserver: self selector: @selector(applyFilter)
                    name: kTorrentDidChangeGroupNotification object: nil];

    //avoids need of setting delegate
    [nc addObserver: self selector: @selector(torrentTableViewSelectionDidChange:)
                    name: NSOutlineViewSelectionDidChangeNotification object: fTableView];

    [nc addObserver: self selector: @selector(changeAutoImport)
                    name: @"AutoImportSettingChange" object: nil];

    [nc addObserver: self selector: @selector(updateForAutoSize)
                    name: @"AutoSizeSettingChange" object: nil];

    [nc addObserver: self selector: @selector(updateForExpandCollape)
                    name: @"OutlineExpandCollapse" object: nil];

    [nc addObserver: fWindow selector: @selector(makeKeyWindow)
                    name: @"MakeWindowKey" object: nil];

    #warning rename
    [nc addObserver: self selector: @selector(fullUpdateUI)
                    name: @"UpdateQueue" object: nil];

    [nc addObserver: self selector: @selector(applyFilter)
                    name: @"ApplyFilter" object: nil];

    //open newly created torrent file
    [nc addObserver: self selector: @selector(beginCreateFile:)
                    name: @"BeginCreateTorrentFile" object: nil];

    //open newly created torrent file
    [nc addObserver: self selector: @selector(openCreatedFile:)
                    name: @"OpenCreatedTorrentFile" object: nil];

    [nc addObserver: self selector: @selector(applyFilter)
                    name: @"UpdateGroups" object: nil];

    //timer to update the interface every second
    [self updateUI];
    fTimer = [[NSTimer scheduledTimerWithTimeInterval: UPDATE_UI_SECONDS target: self
                selector: @selector(updateUI) userInfo: nil repeats: YES] retain];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSEventTrackingRunLoopMode];

    [self applyFilter];

    [fWindow makeKeyAndOrderFront: nil];

    if ([fDefaults boolForKey: @"InfoVisible"])
        [self showInfo: nil];
}

- (void) applicationDidFinishLaunching: (NSNotification *) notification
{
    [NSApp setServicesProvider: self];

    //register for dock icon drags (has to be in applicationDidFinishLaunching: to work)
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler: self andSelector: @selector(handleOpenContentsEvent:replyEvent:)
        forEventClass: kCoreEventClass andEventID: kAEOpenContents];

    //if we were opened from a user notification, do the corresponding action
    if ([NSApp isOnMountainLionOrBetter])
    {
        NSUserNotification * launchNotification = [[notification userInfo] objectForKey: NSApplicationLaunchUserNotificationKey];
        if (launchNotification)
            [self userNotificationCenter: nil didActivateNotification: launchNotification];
    }

    //auto importing
    [self checkAutoImportDirectory];

    //registering the Web UI to Bonjour
    if ([fDefaults boolForKey: @"RPC"] && [fDefaults boolForKey: @"RPCWebDiscovery"])
        [[BonjourController defaultController] startWithPort: [fDefaults integerForKey: @"RPCPort"]];

    //shamelessly ask for donations
    if ([fDefaults boolForKey: @"WarningDonate"])
    {
        tr_session_stats stats;
        tr_sessionGetCumulativeStats(fLib, &stats);
        const BOOL firstLaunch = stats.sessionCount <= 1;

        NSDate * lastDonateDate = [fDefaults objectForKey: @"DonateAskDate"];
        const BOOL timePassed = !lastDonateDate || (-1 * [lastDonateDate timeIntervalSinceNow]) >= DONATE_NAG_TIME;

        if (!firstLaunch && timePassed)
        {
            [fDefaults setObject: [NSDate date] forKey: @"DonateAskDate"];

            NSAlert * alert = [[NSAlert alloc] init];
            [alert setMessageText: NSLocalizedString(@"Support open-source indie software", "Donation beg -> title")];

            NSString * donateMessage = [NSString stringWithFormat: @"%@\n\n%@",
                NSLocalizedString(@"Transmission is a full-featured torrent application."
                    " A lot of time and effort have gone into development, coding, and refinement."
                    " If you enjoy using it, please consider showing your love with a donation.", "Donation beg -> message"),
                NSLocalizedString(@"Donate or not, there will be no difference to your torrenting experience.", "Donation beg -> message")];

            [alert setInformativeText: donateMessage];
            [alert setAlertStyle: NSInformationalAlertStyle];

            [alert addButtonWithTitle: [NSLocalizedString(@"Donate", "Donation beg -> button") stringByAppendingEllipsis]];
            NSButton * noDonateButton = [alert addButtonWithTitle: NSLocalizedString(@"Nope", "Donation beg -> button")];
            [noDonateButton setKeyEquivalent: @"\e"]; //escape key

            const BOOL allowNeverAgain = lastDonateDate != nil; //hide the "don't show again" check the first time - give them time to try the app
            [alert setShowsSuppressionButton: allowNeverAgain];
            if (allowNeverAgain)
                [[alert suppressionButton] setTitle: NSLocalizedString(@"Don't bug me about this ever again.", "Donation beg -> button")];

            const NSInteger donateResult = [alert runModal];
            if (donateResult == NSAlertFirstButtonReturn)
                [self linkDonate: self];

            if (allowNeverAgain)
                [fDefaults setBool: ([[alert suppressionButton] state] != NSOnState) forKey: @"WarningDonate"];

            [alert release];
        }
    }
}

- (BOOL) applicationShouldHandleReopen: (NSApplication *) app hasVisibleWindows: (BOOL) visibleWindows
{
    NSWindow * mainWindow = [NSApp mainWindow];
    if (!mainWindow || ![mainWindow isVisible])
        [fWindow makeKeyAndOrderFront: nil];

    return NO;
}

- (NSApplicationTerminateReply) applicationShouldTerminate: (NSApplication *) sender
{
    if (!fQuitRequested && [fDefaults boolForKey: @"CheckQuit"])
    {
        NSInteger active = 0, downloading = 0;
        for (Torrent * torrent  in fTorrents)
            if ([torrent isActive] && ![torrent isStalled])
            {
                active++;
                if (![torrent allDownloaded])
                    downloading++;
            }

        if ([fDefaults boolForKey: @"CheckQuitDownloading"] ? downloading > 0 : active > 0)
        {
            NSString * message = active == 1
                ? NSLocalizedString(@"There is an active transfer that will be paused on quit."
                    " The transfer will automatically resume on the next launch.", "Confirm Quit panel -> message")
                : [NSString stringWithFormat: NSLocalizedString(@"There are %d active transfers that will be paused on quit."
                    " The transfers will automatically resume on the next launch.", "Confirm Quit panel -> message"), active];

            NSBeginAlertSheet(NSLocalizedString(@"Are you sure you want to quit?", "Confirm Quit panel -> title"),
                                NSLocalizedString(@"Quit", "Confirm Quit panel -> button"),
                                NSLocalizedString(@"Cancel", "Confirm Quit panel -> button"), nil, fWindow, self,
                                @selector(quitSheetDidEnd:returnCode:contextInfo:), nil, nil, @"%@", message);
            return NSTerminateLater;
        }
    }

    return NSTerminateNow;
}

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo
{
    [NSApp replyToApplicationShouldTerminate: returnCode == NSAlertDefaultReturn];
}

- (void) applicationWillTerminate: (NSNotification *) notification
{
    fQuitting = YES;

    //stop the Bonjour service
    if ([BonjourController defaultControllerExists])
        [[BonjourController defaultController] stop];

    //stop blocklist download
    if ([BlocklistDownloader isRunning])
        [[BlocklistDownloader downloader] cancelDownload];

    //stop timers and notification checking
    [[NSNotificationCenter defaultCenter] removeObserver: self];

    [fTimer invalidate];
    [fTimer release];

    if (fAutoImportTimer)
    {
        if ([fAutoImportTimer isValid])
            [fAutoImportTimer invalidate];
        [fAutoImportTimer release];
    }

    [fBadger setQuitting];

    //remove all torrent downloads
    if (fPendingTorrentDownloads)
    {
        for (NSDictionary * downloadDict in fPendingTorrentDownloads)
        {
            NSURLDownload * download = [downloadDict objectForKey: @"Download"];
            [download cancel];
            [download release];
        }
        [fPendingTorrentDownloads release];
    }

    //remember window states and close all windows
    [fDefaults setBool: [[fInfoController window] isVisible] forKey: @"InfoVisible"];

    if ([QLPreviewPanel sharedPreviewPanelExists] && [[QLPreviewPanel sharedPreviewPanel] isVisible])
        [[QLPreviewPanel sharedPreviewPanel] updateController];

    for (NSWindow * window in [NSApp windows])
        [window close];

    [self showStatusBar: NO animate: NO];
    [self showFilterBar: NO animate: NO];

    //save history
    [self updateTorrentHistory];
    [fTableView saveCollapsedGroups];

    //remaining calls the same as dealloc
    [fInfoController release];
    [fMessageController release];
    [fPrefsController release];

    [fStatusBar release];
    [fFilterBar release];

    [fTorrents release];
    [fDisplayedTorrents release];

    [fAddWindows release];
    [fAddingTransfers release];

    [fOverlayWindow release];
    [fBadger release];

    [fAutoImportedNames release];

    [fPreviewPanel release];

    [fConfigDirectory release];

    [fFileWatcherQueue release];

    //complete cleanup
    tr_sessionClose(fLib);
}

- (tr_session *) sessionHandle
{
    return fLib;
}

- (void) handleOpenContentsEvent: (NSAppleEventDescriptor *) event replyEvent: (NSAppleEventDescriptor *) replyEvent
{
    NSString * urlString = nil;

    NSAppleEventDescriptor * directObject = [event paramDescriptorForKeyword: keyDirectObject];
    if ([directObject descriptorType] == typeAEList)
    {
        for (NSInteger i = 1; i <= [directObject numberOfItems]; i++)
            if ((urlString = [[directObject descriptorAtIndex: i] stringValue]))
                break;
    }
    else
        urlString = [directObject stringValue];

    if (urlString)
        [self openURL: urlString];
}

- (void) download: (NSURLDownload *) download decideDestinationWithSuggestedFilename: (NSString *) suggestedName
{
    if ([[suggestedName pathExtension] caseInsensitiveCompare: @"torrent"] != NSOrderedSame)
    {
        [download cancel];

        [fPendingTorrentDownloads removeObjectForKey: [[download request] URL]];
        if ([fPendingTorrentDownloads count] == 0)
        {
            [fPendingTorrentDownloads release];
            fPendingTorrentDownloads = nil;
        }

        NSRunAlertPanel(NSLocalizedString(@"Torrent download failed", "Download not a torrent -> title"),
            [NSString stringWithFormat: NSLocalizedString(@"It appears that the file \"%@\" from %@ is not a torrent file.",
            "Download not a torrent -> message"), suggestedName,
            [[[[download request] URL] absoluteString] stringByReplacingPercentEscapesUsingEncoding: NSUTF8StringEncoding]],
            NSLocalizedString(@"OK", "Download not a torrent -> button"), nil, nil);

        [download release];
    }
    else
        [download setDestination: [NSTemporaryDirectory() stringByAppendingPathComponent: [suggestedName lastPathComponent]]
                    allowOverwrite: NO];
}

-(void) download: (NSURLDownload *) download didCreateDestination: (NSString *) path
{
    [(NSMutableDictionary *)[fPendingTorrentDownloads objectForKey: [[download request] URL]] setObject: path forKey: @"Path"];
}

- (void) download: (NSURLDownload *) download didFailWithError: (NSError *) error
{
    NSRunAlertPanel(NSLocalizedString(@"Torrent download failed", "Torrent download error -> title"),
        [NSString stringWithFormat: NSLocalizedString(@"The torrent could not be downloaded from %@: %@.",
        "Torrent download failed -> message"),
        [[[[download request] URL] absoluteString] stringByReplacingPercentEscapesUsingEncoding: NSUTF8StringEncoding],
        [error localizedDescription]], NSLocalizedString(@"OK", "Torrent download failed -> button"), nil, nil);

    [fPendingTorrentDownloads removeObjectForKey: [[download request] URL]];
    if ([fPendingTorrentDownloads count] == 0)
    {
        [fPendingTorrentDownloads release];
        fPendingTorrentDownloads = nil;
    }

    [download release];
}

- (void) downloadDidFinish: (NSURLDownload *) download
{
    NSString * path = [[fPendingTorrentDownloads objectForKey: [[download request] URL]] objectForKey: @"Path"];

    [self openFiles: [NSArray arrayWithObject: path] addType: ADD_URL forcePath: nil];

    //delete the torrent file after opening
    [[NSFileManager defaultManager] removeItemAtPath: path error: NULL];

    [fPendingTorrentDownloads removeObjectForKey: [[download request] URL]];
    if ([fPendingTorrentDownloads count] == 0)
    {
        [fPendingTorrentDownloads release];
        fPendingTorrentDownloads = nil;
    }

    [download release];
}

- (void) application: (NSApplication *) app openFiles: (NSArray *) filenames
{
    [self openFiles: filenames addType: ADD_MANUAL forcePath: nil];
}

- (void) openFiles: (NSArray *) filenames addType: (addType) type forcePath: (NSString *) path
{
    BOOL deleteTorrentFile, canToggleDelete = NO;
    switch (type)
    {
        case ADD_CREATED:
            deleteTorrentFile = NO;
            break;
        case ADD_URL:
            deleteTorrentFile = YES;
            break;
        default:
            deleteTorrentFile = [fDefaults boolForKey: @"DeleteOriginalTorrent"];
            canToggleDelete = YES;
    }

    for (NSString * torrentPath in filenames)
    {
        //ensure torrent doesn't already exist
        tr_ctor * ctor = tr_ctorNew(fLib);
        tr_ctorSetMetainfoFromFile(ctor, [torrentPath UTF8String]);

        tr_info info;
        const tr_parse_result result = tr_torrentParse(ctor, &info);
        tr_ctorFree(ctor);

        if (result != TR_PARSE_OK)
        {
            if (result == TR_PARSE_DUPLICATE)
                [self duplicateOpenAlert: [NSString stringWithUTF8String: info.name]];
            else if (result == TR_PARSE_ERR)
            {
                if (type != ADD_AUTO)
                    [self invalidOpenAlert: [torrentPath lastPathComponent]];
            }
            else
                NSAssert2(NO, @"Unknown error code (%d) when attempting to open \"%@\"", result, torrentPath);

            tr_metainfoFree(&info);
            continue;
        }

        //determine download location
        NSString * location;
        BOOL lockDestination = NO; //don't override the location with a group location if it has a hardcoded path
        if (path)
        {
            location = [path stringByExpandingTildeInPath];
            lockDestination = YES;
        }
        else if ([fDefaults boolForKey: @"DownloadLocationConstant"])
            location = [[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath];
        else if (type != ADD_URL)
            location = [torrentPath stringByDeletingLastPathComponent];
        else
            location = nil;

        //determine to show the options window
        const BOOL showWindow = type == ADD_SHOW_OPTIONS || ([fDefaults boolForKey: @"DownloadAsk"]
                                    && (info.isFolder || ![fDefaults boolForKey: @"DownloadAskMulti"])
                                    && (type != ADD_AUTO || ![fDefaults boolForKey: @"DownloadAskManual"]));
        tr_metainfoFree(&info);

        Torrent * torrent;
        if (!(torrent = [[Torrent alloc] initWithPath: torrentPath location: location
                            deleteTorrentFile: showWindow ? NO : deleteTorrentFile lib: fLib]))
            continue;

        //change the location if the group calls for it (this has to wait until after the torrent is created)
        if (!lockDestination && [[GroupsController groups] usesCustomDownloadLocationForIndex: [torrent groupValue]])
        {
            location = [[GroupsController groups] customDownloadLocationForIndex: [torrent groupValue]];
            [torrent changeDownloadFolderBeforeUsing: location determinationType: TorrentDeterminationAutomatic];
        }

        //verify the data right away if it was newly created
        if (type == ADD_CREATED)
            [torrent resetCache];

        //show the add window or add directly
        if (showWindow || !location)
        {
            AddWindowController * addController = [[AddWindowController alloc] initWithTorrent: torrent destination: location
                                                    lockDestination: lockDestination controller: self torrentFile: torrentPath
                                                    deleteTorrentCheckEnableInitially: deleteTorrentFile canToggleDelete: canToggleDelete];
            [addController showWindow: self];

            if (!fAddWindows)
                fAddWindows = [[NSMutableSet alloc] init];
            [fAddWindows addObject: addController];
            [addController release];
        }
        else
        {
            if ([fDefaults boolForKey: @"AutoStartDownload"])
                [torrent startTransfer];

            [torrent update];
            [fTorrents addObject: torrent];
            [torrent release];

            if (!fAddingTransfers)
                fAddingTransfers = [[NSMutableSet alloc] init];
            [fAddingTransfers addObject: torrent];
        }
    }

    [self fullUpdateUI];
}

- (void) askOpenConfirmed: (AddWindowController *) addController add: (BOOL) add
{
    Torrent * torrent = [addController torrent];

    if (add)
    {
        [torrent setQueuePosition: [fTorrents count]];

        [torrent update];
        [fTorrents addObject: torrent];
        [torrent release];

        if (!fAddingTransfers)
            fAddingTransfers = [[NSMutableSet alloc] init];
        [fAddingTransfers addObject: torrent];

        [self fullUpdateUI];
    }
    else
    {
        [torrent closeRemoveTorrent: NO];
        [torrent release];
    }

    [fAddWindows removeObject: addController];
    if ([fAddWindows count] == 0)
    {
        [fAddWindows release];
        fAddWindows = nil;
    }
}

- (void) openMagnet: (NSString *) address
{
    tr_torrent * duplicateTorrent;
    if ((duplicateTorrent = tr_torrentFindFromMagnetLink(fLib, [address UTF8String])))
    {
        const tr_info * info = tr_torrentInfo(duplicateTorrent);
        NSString * name = (info != NULL && info->name != NULL) ? [NSString stringWithUTF8String: info->name] : nil;
        [self duplicateOpenMagnetAlert: address transferName: name];
        return;
    }

    //determine download location
    NSString * location = nil;
    if ([fDefaults boolForKey: @"DownloadLocationConstant"])
        location = [[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath];

    Torrent * torrent;
    if (!(torrent = [[Torrent alloc] initWithMagnetAddress: address location: location lib: fLib]))
    {
        [self invalidOpenMagnetAlert: address];
        return;
    }

    //change the location if the group calls for it (this has to wait until after the torrent is created)
    if ([[GroupsController groups] usesCustomDownloadLocationForIndex: [torrent groupValue]])
    {
        location = [[GroupsController groups] customDownloadLocationForIndex: [torrent groupValue]];
        [torrent changeDownloadFolderBeforeUsing: location determinationType: TorrentDeterminationAutomatic];
    }

    if ([fDefaults boolForKey: @"MagnetOpenAsk"] || !location)
    {
        AddMagnetWindowController * addController = [[AddMagnetWindowController alloc] initWithTorrent: torrent destination: location
                                                        controller: self];
        [addController showWindow: self];

        if (!fAddWindows)
            fAddWindows = [[NSMutableSet alloc] init];
        [fAddWindows addObject: addController];
        [addController release];
    }
    else
    {
        if ([fDefaults boolForKey: @"AutoStartDownload"])
            [torrent startTransfer];

        [torrent update];
        [fTorrents addObject: torrent];
        [torrent release];

        if (!fAddingTransfers)
            fAddingTransfers = [[NSMutableSet alloc] init];
        [fAddingTransfers addObject: torrent];
    }

    [self fullUpdateUI];
}

- (void) askOpenMagnetConfirmed: (AddMagnetWindowController *) addController add: (BOOL) add
{
    Torrent * torrent = [addController torrent];

    if (add)
    {
        [torrent setQueuePosition: [fTorrents count]];

        [torrent update];
        [fTorrents addObject: torrent];
        [torrent release];

        if (!fAddingTransfers)
            fAddingTransfers = [[NSMutableSet alloc] init];
        [fAddingTransfers addObject: torrent];

        [self fullUpdateUI];
    }
    else
    {
        [torrent closeRemoveTorrent: NO];
        [torrent release];
    }

    [fAddWindows removeObject: addController];
    if ([fAddWindows count] == 0)
    {
        [fAddWindows release];
        fAddWindows = nil;
    }
}

- (void) openCreatedFile: (NSNotification *) notification
{
    NSDictionary * dict = [notification userInfo];
    [self openFiles: [NSArray arrayWithObject: [dict objectForKey: @"File"]] addType: ADD_CREATED forcePath: [dict objectForKey: @"Path"]];
    [dict release];
}

- (void) openFilesWithDict: (NSDictionary *) dictionary
{
    [self openFiles: [dictionary objectForKey: @"Filenames"] addType: [[dictionary objectForKey: @"AddType"] intValue] forcePath: nil];

    [dictionary release];
}

//called on by applescript
- (void) open: (NSArray *) files
{
    NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys: files, @"Filenames",
                                [NSNumber numberWithInt: ADD_MANUAL], @"AddType", nil];
    [self performSelectorOnMainThread: @selector(openFilesWithDict:) withObject: dict waitUntilDone: NO];
}

- (void) openShowSheet: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setAllowsMultipleSelection: YES];
    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: NO];

    [panel setAllowedFileTypes: [NSArray arrayWithObjects: @"org.bittorrent.torrent", @"torrent", nil]];

    [panel beginSheetModalForWindow: fWindow completionHandler: ^(NSInteger result) {
        if (result == NSFileHandlingPanelOKButton)
        {
            NSMutableArray * filenames = [NSMutableArray arrayWithCapacity: [[panel URLs] count]];
            for (NSURL * url in [panel URLs])
                [filenames addObject: [url path]];

            NSDictionary * dictionary = [[NSDictionary alloc] initWithObjectsAndKeys: filenames, @"Filenames",
                                         [NSNumber numberWithInt: sender == fOpenIgnoreDownloadFolder ? ADD_SHOW_OPTIONS : ADD_MANUAL], @"AddType", nil];
            [self performSelectorOnMainThread: @selector(openFilesWithDict:) withObject: dictionary waitUntilDone: NO];
        }
    }];
}

- (void) invalidOpenAlert: (NSString *) filename
{
    if (![fDefaults boolForKey: @"WarningInvalidOpen"])
        return;

    NSAlert * alert = [[NSAlert alloc] init];
    [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"\"%@\" is not a valid torrent file.",
                            "Open invalid alert -> title"), filename]];
    [alert setInformativeText:
            NSLocalizedString(@"The torrent file cannot be opened because it contains invalid data.",
                            "Open invalid alert -> message")];
    [alert setAlertStyle: NSWarningAlertStyle];
    [alert addButtonWithTitle: NSLocalizedString(@"OK", "Open invalid alert -> button")];

    [alert runModal];
    if ([[alert suppressionButton] state] == NSOnState)
        [fDefaults setBool: NO forKey: @"WarningInvalidOpen"];
    [alert release];
}

- (void) invalidOpenMagnetAlert: (NSString *) address
{
    if (![fDefaults boolForKey: @"WarningInvalidOpen"])
        return;

    NSAlert * alert = [[NSAlert alloc] init];
    [alert setMessageText: NSLocalizedString(@"Adding magnetized transfer failed.", "Magnet link failed -> title")];
    [alert setInformativeText: [NSString stringWithFormat: NSLocalizedString(@"There was an error when adding the magnet link \"%@\"."
                                " The transfer will not occur.", "Magnet link failed -> message"), address]];
    [alert setAlertStyle: NSWarningAlertStyle];
    [alert addButtonWithTitle: NSLocalizedString(@"OK", "Magnet link failed -> button")];

    [alert runModal];
    if ([[alert suppressionButton] state] == NSOnState)
        [fDefaults setBool: NO forKey: @"WarningInvalidOpen"];
    [alert release];
}

- (void) duplicateOpenAlert: (NSString *) name
{
    if (![fDefaults boolForKey: @"WarningDuplicate"])
        return;

    NSAlert * alert = [[NSAlert alloc] init];
    [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"A transfer of \"%@\" already exists.",
                            "Open duplicate alert -> title"), name]];
    [alert setInformativeText:
            NSLocalizedString(@"The transfer cannot be added because it is a duplicate of an already existing transfer.",
                            "Open duplicate alert -> message")];
    [alert setAlertStyle: NSWarningAlertStyle];
    [alert addButtonWithTitle: NSLocalizedString(@"OK", "Open duplicate alert -> button")];
    [alert setShowsSuppressionButton: YES];

    [alert runModal];
    if ([[alert suppressionButton] state])
        [fDefaults setBool: NO forKey: @"WarningDuplicate"];
    [alert release];
}

- (void) duplicateOpenMagnetAlert: (NSString *) address transferName: (NSString *) name
{
    if (![fDefaults boolForKey: @"WarningDuplicate"])
        return;

    NSAlert * alert = [[NSAlert alloc] init];
    if (name)
        [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"A transfer of \"%@\" already exists.",
                                "Open duplicate magnet alert -> title"), name]];
    else
        [alert setMessageText: NSLocalizedString(@"Magnet link is a duplicate of an existing transfer.",
                                "Open duplicate magnet alert -> title")];
    [alert setInformativeText: [NSString stringWithFormat:
            NSLocalizedString(@"The magnet link  \"%@\" cannot be added because it is a duplicate of an already existing transfer.",
                            "Open duplicate magnet alert -> message"), address]];
    [alert setAlertStyle: NSWarningAlertStyle];
    [alert addButtonWithTitle: NSLocalizedString(@"OK", "Open duplicate magnet alert -> button")];
    [alert setShowsSuppressionButton: YES];

    [alert runModal];
    if ([[alert suppressionButton] state])
        [fDefaults setBool: NO forKey: @"WarningDuplicate"];
    [alert release];
}

- (void) openURL: (NSString *) urlString
{
    if ([urlString rangeOfString: @"magnet:" options: (NSAnchoredSearch | NSCaseInsensitiveSearch)].location != NSNotFound)
        [self openMagnet: urlString];
    else
    {
        if ([urlString rangeOfString: @"://"].location == NSNotFound)
        {
            if ([urlString rangeOfString: @"."].location == NSNotFound)
            {
                NSInteger beforeCom;
                if ((beforeCom = [urlString rangeOfString: @"/"].location) != NSNotFound)
                    urlString = [NSString stringWithFormat: @"http://www.%@.com/%@",
                                    [urlString substringToIndex: beforeCom],
                                    [urlString substringFromIndex: beforeCom + 1]];
                else
                    urlString = [NSString stringWithFormat: @"http://www.%@.com/", urlString];
            }
            else
                urlString = [@"http://" stringByAppendingString: urlString];
        }

        NSURLRequest * request = [NSURLRequest requestWithURL: [NSURL URLWithString: urlString]
                                    cachePolicy: NSURLRequestReloadIgnoringLocalAndRemoteCacheData timeoutInterval: 60];

        if ([fPendingTorrentDownloads objectForKey: [request URL]])
        {
            NSLog(@"Already downloading %@", [request URL]);
            return;
        }

        NSURLDownload * download = [[NSURLDownload alloc] initWithRequest: request delegate: self];

        if (!fPendingTorrentDownloads)
            fPendingTorrentDownloads = [[NSMutableDictionary alloc] init];
        [fPendingTorrentDownloads setObject: [NSMutableDictionary dictionaryWithObject: download forKey: @"Download"] forKey: [request URL]];
    }
}

- (void) openURLShowSheet: (id) sender
{
    if (!fUrlSheetController)
    {
        fUrlSheetController = [[URLSheetWindowController alloc] initWithController: self];

        [NSApp beginSheet: [fUrlSheetController window] modalForWindow: fWindow modalDelegate: self didEndSelector: @selector(urlSheetDidEnd:returnCode:contextInfo:) contextInfo: nil];
    }
}

- (void) urlSheetDidEnd: (NSWindow *) sheet returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo
{
    if (returnCode == 1)
    {
        NSString * urlString = [fUrlSheetController urlString];
        [self performSelectorOnMainThread: @selector(openURL:) withObject: urlString waitUntilDone: NO];
    }

    [fUrlSheetController release];
    fUrlSheetController = nil;
}

- (void) createFile: (id) sender
{
    [CreatorWindowController createTorrentFile: fLib];
}

- (void) resumeSelectedTorrents: (id) sender
{
    [self resumeTorrents: [fTableView selectedTorrents]];
}

- (void) resumeAllTorrents: (id) sender
{
    NSMutableArray * torrents = [NSMutableArray arrayWithCapacity: [fTorrents count]];

    for (Torrent * torrent in fTorrents)
        if (![torrent isFinishedSeeding])
            [torrents addObject: torrent];

    [self resumeTorrents: torrents];
}

- (void) resumeTorrents: (NSArray *) torrents
{
    for (Torrent * torrent in torrents)
        [torrent startTransfer];

    [self fullUpdateUI];
}

- (void) resumeSelectedTorrentsNoWait:  (id) sender
{
    [self resumeTorrentsNoWait: [fTableView selectedTorrents]];
}

- (void) resumeWaitingTorrents: (id) sender
{
    NSMutableArray * torrents = [NSMutableArray arrayWithCapacity: [fTorrents count]];

    for (Torrent * torrent in fTorrents)
        if ([torrent waitingToStart])
            [torrents addObject: torrent];

    [self resumeTorrentsNoWait: torrents];
}

- (void) resumeTorrentsNoWait: (NSArray *) torrents
{
    //iterate through instead of all at once to ensure no conflicts
    for (Torrent * torrent in torrents)
        [torrent startTransferNoQueue];

    [self fullUpdateUI];
}

- (void) stopSelectedTorrents: (id) sender
{
    [self stopTorrents: [fTableView selectedTorrents]];
}

- (void) stopAllTorrents: (id) sender
{
    [self stopTorrents: fTorrents];
}

- (void) stopTorrents: (NSArray *) torrents
{
    //don't want any of these starting then stopping
    for (Torrent * torrent in torrents)
         if ([torrent waitingToStart])
             [torrent stopTransfer];

    for (Torrent * torrent in torrents)
        [torrent stopTransfer];

    [self fullUpdateUI];
}

- (void) removeTorrents: (NSArray *) torrents deleteData: (BOOL) deleteData
{
    if ([fDefaults boolForKey: @"CheckRemove"])
    {
        NSUInteger active = 0, downloading = 0;
        for (Torrent * torrent in torrents)
            if ([torrent isActive])
            {
                ++active;
                if (![torrent isSeeding])
                    ++downloading;
            }

        if ([fDefaults boolForKey: @"CheckRemoveDownloading"] ? downloading > 0 : active > 0)
        {
            NSDictionary * dict = @{ @"Torrents" : torrents,
                                    @"DeleteData" : @(deleteData) };

            NSString * title, * message;

            const NSInteger selected = [torrents count];
            if (selected == 1)
            {
                NSString * torrentName = [(Torrent *)[torrents objectAtIndex: 0] name];

                if (deleteData)
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove \"%@\" from the transfer list"
                                " and trash the data file?", "Removal confirm panel -> title"), torrentName];
                else
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove \"%@\" from the transfer list?",
                                "Removal confirm panel -> title"), torrentName];

                message = NSLocalizedString(@"This transfer is active."
                            " Once removed, continuing the transfer will require the torrent file or magnet link.",
                            "Removal confirm panel -> message");
            }
            else
            {
                if (deleteData)
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove %@ transfers from the transfer list"
                                " and trash the data files?", "Removal confirm panel -> title"), [NSString formattedUInteger: selected]];
                else
                    title = [NSString stringWithFormat:
                                NSLocalizedString(@"Are you sure you want to remove %@ transfers from the transfer list?",
                                "Removal confirm panel -> title"), [NSString formattedUInteger: selected]];

                if (selected == active)
                    message = [NSString stringWithFormat: NSLocalizedString(@"There are %@ active transfers.",
                                "Removal confirm panel -> message part 1"), [NSString formattedUInteger: active]];
                else
                    message = [NSString stringWithFormat: NSLocalizedString(@"There are %@ transfers (%@ active).",
                                "Removal confirm panel -> message part 1"), [NSString formattedUInteger: selected], [NSString formattedUInteger: active]];
                message = [message stringByAppendingFormat: @" %@",
                            NSLocalizedString(@"Once removed, continuing the transfers will require the torrent files or magnet links.",
                            "Removal confirm panel -> message part 2")];
            }

            NSBeginAlertSheet(title, NSLocalizedString(@"Remove", "Removal confirm panel -> button"),
                NSLocalizedString(@"Cancel", "Removal confirm panel -> button"), nil, fWindow, self,
                nil, @selector(removeSheetDidEnd:returnCode:contextInfo:), [dict retain], @"%@", message);
            return;
        }
    }

    [self confirmRemoveTorrents: torrents deleteData: deleteData];
}

- (void) removeSheetDidEnd: (NSWindow *) sheet returnCode: (NSInteger) returnCode contextInfo: (NSDictionary *) dict
{
    NSArray * torrents = [dict objectForKey: @"Torrents"];
    if (returnCode == NSAlertDefaultReturn)
        [self confirmRemoveTorrents: torrents deleteData: [[dict objectForKey: @"DeleteData"] boolValue]];
    [dict release];
}

- (void) confirmRemoveTorrents: (NSArray *) torrents deleteData: (BOOL) deleteData
{
    //miscellaneous
    for (Torrent * torrent in torrents)
    {
        //don't want any of these starting then stopping
        if ([torrent waitingToStart])
            [torrent stopTransfer];

        //let's expand all groups that have removed items - they either don't exist anymore, are already expanded, or are collapsed (rpc)
        [fTableView removeCollapsedGroup: [torrent groupValue]];

        //we can't assume the window is active - RPC removal, for example
        [fBadger removeTorrent: torrent];
    }

    //#5106 - don't try to remove torrents that have already been removed (fix for a bug, but better safe than crash anyway)
    NSIndexSet * indexesToRemove = [torrents indexesOfObjectsWithOptions: NSEnumerationConcurrent passingTest: ^BOOL(Torrent * torrent, NSUInteger idx, BOOL * stop) {
        return [fTorrents indexOfObjectIdenticalTo: torrent] != NSNotFound;
    }];
    if ([torrents count] != [indexesToRemove count])
    {
        NSLog(@"trying to remove %ld transfers, but %ld have already been removed", [torrents count], [torrents count] - [indexesToRemove count]);
        torrents = [torrents objectsAtIndexes: indexesToRemove];

        if ([indexesToRemove count] == 0)
        {
            [self fullUpdateUI];
            return;
        }
    }

    [fTorrents removeObjectsInArray: torrents];

    //set up helpers to remove from the table
    __block BOOL beganUpdate = NO;

    void (^doTableRemoval)(NSMutableArray *, id) = ^(NSMutableArray * displayedTorrents, id parent) {
        NSIndexSet * indexes = [displayedTorrents indexesOfObjectsWithOptions: NSEnumerationConcurrent passingTest: ^(id obj, NSUInteger idx, BOOL * stop) {
            return [torrents containsObject: obj];
        }];

        if ([indexes count] > 0)
        {
            if (!beganUpdate)
            {
                [NSAnimationContext beginGrouping]; //this has to be before we set the completion handler (#4874)

                //we can't closeRemoveTorrent: until it's no longer in the GUI at all
                [[NSAnimationContext currentContext] setCompletionHandler: ^{
                    for (Torrent * torrent in torrents)
                        [torrent closeRemoveTorrent: deleteData];
                }];

                [fTableView beginUpdates];
                beganUpdate = YES;
            }

            [fTableView removeItemsAtIndexes: indexes inParent: parent withAnimation: NSTableViewAnimationSlideLeft];

            [displayedTorrents removeObjectsAtIndexes: indexes];
        }
    };

    //if not removed from the displayed torrents here, fullUpdateUI might cause a crash
    if ([fDisplayedTorrents count] > 0)
    {
        if ([[fDisplayedTorrents objectAtIndex: 0] isKindOfClass: [TorrentGroup class]])
        {
            for (TorrentGroup * group in fDisplayedTorrents)
                doTableRemoval([group torrents], group);
        }
        else
            doTableRemoval(fDisplayedTorrents, nil);

        if (beganUpdate)
        {
            [fTableView endUpdates];
            [NSAnimationContext endGrouping];
        }
    }

    if (!beganUpdate)
    {
        //do here if we're not doing it at the end of the animation
        for (Torrent * torrent in torrents)
            [torrent closeRemoveTorrent: deleteData];
    }

    [self fullUpdateUI];
}

- (void) removeNoDelete: (id) sender
{
    [self removeTorrents: [fTableView selectedTorrents] deleteData: NO];
}

- (void) removeDeleteData: (id) sender
{
    [self removeTorrents: [fTableView selectedTorrents] deleteData: YES];
}

- (void) clearCompleted: (id) sender
{
    NSMutableArray * torrents = [NSMutableArray array];

    for (Torrent * torrent in fTorrents)
        if ([torrent isFinishedSeeding])
            [torrents addObject: torrent];

    if ([fDefaults boolForKey: @"WarningRemoveCompleted"])
    {
        NSString * message, * info;
        if ([torrents count] == 1)
        {
            NSString * torrentName = [(Torrent *)[torrents objectAtIndex: 0] name];
            message = [NSString stringWithFormat: NSLocalizedString(@"Are you sure you want to remove \"%@\" from the transfer list?",
                                                                  "Remove completed confirm panel -> title"), torrentName];

            info = NSLocalizedString(@"Once removed, continuing the transfer will require the torrent file or magnet link.",
                                     "Remove completed confirm panel -> message");
        }
        else
        {
            message = [NSString stringWithFormat: NSLocalizedString(@"Are you sure you want to remove %@ completed transfers from the transfer list?",
                                                                  "Remove completed confirm panel -> title"), [NSString formattedUInteger: [torrents count]]];

            info = NSLocalizedString(@"Once removed, continuing the transfers will require the torrent files or magnet links.",
                                     "Remove completed confirm panel -> message");
        }

        NSAlert * alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText: message];
        [alert setInformativeText: info];
        [alert setAlertStyle: NSWarningAlertStyle];
        [alert addButtonWithTitle: NSLocalizedString(@"Remove", "Remove completed confirm panel -> button")];
        [alert addButtonWithTitle: NSLocalizedString(@"Cancel", "Remove completed confirm panel -> button")];
        [alert setShowsSuppressionButton: YES];

        const NSInteger returnCode = [alert runModal];
        if ([[alert suppressionButton] state])
            [fDefaults setBool: NO forKey: @"WarningRemoveCompleted"];

        if (returnCode != NSAlertFirstButtonReturn)
            return;
    }

    [self confirmRemoveTorrents: torrents deleteData: NO];
}

- (void) moveDataFilesSelected: (id) sender
{
    [self moveDataFiles: [fTableView selectedTorrents]];
}

- (void) moveDataFiles: (NSArray *) torrents
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];
    [panel setPrompt: NSLocalizedString(@"Select", "Move torrent -> prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    NSInteger count = [torrents count];
    if (count == 1)
        [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the new folder for \"%@\".",
                            "Move torrent -> select destination folder"), [(Torrent *)[torrents objectAtIndex: 0] name]]];
    else
        [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the new folder for %d data files.",
                            "Move torrent -> select destination folder"), count]];

    [panel beginSheetModalForWindow: fWindow completionHandler: ^(NSInteger result) {
        if (result == NSFileHandlingPanelOKButton)
        {
            for (Torrent * torrent in torrents)
                [torrent moveTorrentDataFileTo: [[[panel URLs] objectAtIndex: 0] path]];
        }
    }];
}

- (void) copyTorrentFiles: (id) sender
{
    [self copyTorrentFileForTorrents: [[NSMutableArray alloc] initWithArray: [fTableView selectedTorrents]]];
}

- (void) copyTorrentFileForTorrents: (NSMutableArray *) torrents
{
    if ([torrents count] == 0)
    {
        [torrents release];
        return;
    }

    Torrent * torrent = [torrents objectAtIndex: 0];

    if (![torrent isMagnet] && [[NSFileManager defaultManager] fileExistsAtPath: [torrent torrentLocation]])
    {
        NSSavePanel * panel = [NSSavePanel savePanel];
        [panel setAllowedFileTypes: [NSArray arrayWithObjects: @"org.bittorrent.torrent", @"torrent", nil]];
        [panel setExtensionHidden: NO];

        [panel setNameFieldStringValue: [torrent name]];

        [panel beginSheetModalForWindow: fWindow completionHandler: ^(NSInteger result) {
            //copy torrent to new location with name of data file
            if (result == NSFileHandlingPanelOKButton)
                [torrent copyTorrentFileTo: [[panel URL] path]];

            [torrents removeObjectAtIndex: 0];
            [self performSelectorOnMainThread: @selector(copyTorrentFileForTorrents:) withObject: torrents waitUntilDone: NO];
        }];
    }
    else
    {
        if (![torrent isMagnet])
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Torrent file copy alert -> button")];
            [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"Copy of \"%@\" Cannot Be Created",
                                    "Torrent file copy alert -> title"), [torrent name]]];
            [alert setInformativeText: [NSString stringWithFormat:
                    NSLocalizedString(@"The torrent file (%@) cannot be found.", "Torrent file copy alert -> message"),
                                        [torrent torrentLocation]]];
            [alert setAlertStyle: NSWarningAlertStyle];

            [alert runModal];
            [alert release];
        }

        [torrents removeObjectAtIndex: 0];
        [self copyTorrentFileForTorrents: torrents];
    }
}

- (void) copyMagnetLinks: (id) sender
{
    NSArray * torrents = [fTableView selectedTorrents];

    if ([torrents count] <= 0)
        return;

    NSMutableArray * links = [NSMutableArray arrayWithCapacity: [torrents count]];
    for (Torrent * torrent in torrents)
        [links addObject: [torrent magnetLink]];

    NSString * text = [links componentsJoinedByString: @"\n"];

    NSPasteboard * pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb writeObjects: [NSArray arrayWithObject: text]];
}

- (void) revealFile: (id) sender
{
    NSArray * selected = [fTableView selectedTorrents];
    NSMutableArray * paths = [NSMutableArray arrayWithCapacity: [selected count]];
    for (Torrent * torrent in selected)
    {
        NSString * location = [torrent dataLocation];
        if (location)
            [paths addObject: [NSURL fileURLWithPath: location]];
    }

    if ([paths count] > 0)
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs: paths];
}

- (IBAction) renameSelected: (id) sender
{
    NSArray * selected = [fTableView selectedTorrents];
    NSAssert([selected count] == 1, @"1 transfer needs to be selected to rename, but %ld are selected", [selected count]);
    Torrent * torrent = [selected objectAtIndex:0];

    [FileRenameSheetController presentSheetForTorrent:torrent modalForWindow: fWindow completionHandler: ^(BOOL didRename) {
        if (didRename)
        {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self fullUpdateUI];

                [[NSNotificationCenter defaultCenter] postNotificationName: @"ResetInspector" object: self userInfo: @{ @"Torrent" : torrent }];
            });
        }
    }];
}

- (void) announceSelectedTorrents: (id) sender
{
    for (Torrent * torrent in [fTableView selectedTorrents])
    {
        if ([torrent canManualAnnounce])
            [torrent manualAnnounce];
    }
}

- (void) verifySelectedTorrents: (id) sender
{
    [self verifyTorrents: [fTableView selectedTorrents]];
}

- (void) verifyTorrents: (NSArray *) torrents
{
    for (Torrent * torrent in torrents)
        [torrent resetCache];

    [self applyFilter];
}

- (NSArray *)selectedTorrents
{
    return [fTableView selectedTorrents];
}

- (void) showPreferenceWindow: (id) sender
{
    NSWindow * window = [fPrefsController window];
    if (![window isVisible])
        [window center];

    [window makeKeyAndOrderFront: nil];
}

- (void) showAboutWindow: (id) sender
{
    [[AboutWindowController aboutController] showWindow: nil];
}

- (void) showInfo: (id) sender
{
    if ([[fInfoController window] isVisible])
        [fInfoController close];
    else
    {
        [fInfoController updateInfoStats];
        [[fInfoController window] orderFront: nil];

        if ([fInfoController canQuickLook] && [QLPreviewPanel sharedPreviewPanelExists] && [[QLPreviewPanel sharedPreviewPanel] isVisible])
            [[QLPreviewPanel sharedPreviewPanel] reloadData];
    }

    [[fWindow toolbar] validateVisibleItems];
}

- (void) resetInfo
{
    [fInfoController setInfoForTorrents: [fTableView selectedTorrents]];

    if ([QLPreviewPanel sharedPreviewPanelExists] && [[QLPreviewPanel sharedPreviewPanel] isVisible])
        [[QLPreviewPanel sharedPreviewPanel] reloadData];
}

- (void) setInfoTab: (id) sender
{
    if (sender == fNextInfoTabItem)
        [fInfoController setNextTab];
    else
        [fInfoController setPreviousTab];
}

- (MessageWindowController *) messageWindowController
{
    if (!fMessageController)
        fMessageController = [[MessageWindowController alloc] init];

    return fMessageController;
}

- (void) showMessageWindow: (id) sender
{
    [[self messageWindowController] showWindow: nil];
}

- (void) showStatsWindow: (id) sender
{
    [[StatsWindowController statsWindow] showWindow: nil];
}

- (void) updateUI
{
    CGFloat dlRate = 0.0, ulRate = 0.0;
    BOOL anyCompleted = NO;
    for (Torrent * torrent in fTorrents)
    {
        [torrent update];

        //pull the upload and download speeds - most consistent by using current stats
        dlRate += [torrent downloadRate];
        ulRate += [torrent uploadRate];

        anyCompleted |= [torrent isFinishedSeeding];
    }

    if (![NSApp isHidden])
    {
        if ([fWindow isVisible])
        {
            [self sortTorrents: NO];

            [fStatusBar updateWithDownload: dlRate upload: ulRate];

            [fClearCompletedButton setHidden: !anyCompleted];
        }

        //update non-constant parts of info window
        if ([[fInfoController window] isVisible])
            [fInfoController updateInfoStats];
    }

    //badge dock
    [fBadger updateBadgeWithDownload: dlRate upload: ulRate];
}

#warning can this be removed or refined?
- (void) fullUpdateUI
{
    [self updateUI];
    [self applyFilter];
    [[fWindow toolbar] validateVisibleItems];
    [self updateTorrentHistory];
}

- (void) setBottomCountText: (BOOL) filtering
{
    NSString * totalTorrentsString;
    NSUInteger totalCount = [fTorrents count];
    if (totalCount != 1)
        totalTorrentsString = [NSString stringWithFormat: NSLocalizedString(@"%@ transfers", "Status bar transfer count"),
                                [NSString formattedUInteger: totalCount]];
    else
        totalTorrentsString = NSLocalizedString(@"1 transfer", "Status bar transfer count");

    if (filtering)
    {
        NSUInteger count = [fTableView numberOfRows]; //have to factor in collapsed rows
        if (count > 0 && ![[fDisplayedTorrents objectAtIndex: 0] isKindOfClass: [Torrent class]])
            count -= [fDisplayedTorrents count];

        totalTorrentsString = [NSString stringWithFormat: NSLocalizedString(@"%@ of %@", "Status bar transfer count"),
                                [NSString formattedUInteger: count], totalTorrentsString];
    }

    [fTotalTorrentsField setStringValue: totalTorrentsString];
}

- (BOOL) userNotificationCenter: (NSUserNotificationCenter *) center shouldPresentNotification:(NSUserNotification *) notification
{
    return YES;
}

- (void) userNotificationCenter: (NSUserNotificationCenter *) center didActivateNotification: (NSUserNotification *) notification
{
    if (![notification userInfo])
        return;

    if ([notification activationType] == NSUserNotificationActivationTypeActionButtonClicked) //reveal
    {
        Torrent * torrent = [self torrentForHash: [[notification userInfo] objectForKey: @"Hash"]];
        NSString * location = [torrent dataLocation];
        if (!location)
            location = [[notification userInfo] objectForKey: @"Location"];
        if (location)
            [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs: @[[NSURL fileURLWithPath: location]]];
    }
    else if ([notification activationType] == NSUserNotificationActivationTypeContentsClicked)
    {
        Torrent * torrent = [self torrentForHash: [[notification userInfo] objectForKey: @"Hash"]];
        if (torrent)
        {
            //select in the table - first see if it's already shown
            NSInteger row = [fTableView rowForItem: torrent];
            if (row == -1)
            {
                //if it's not shown, see if it's in a collapsed row
                if ([fDefaults boolForKey: @"SortByGroup"])
                {
                    __block TorrentGroup * parent = nil;
                    [fDisplayedTorrents enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: ^(TorrentGroup * group, NSUInteger idx, BOOL *stop) {
                        if ([[group torrents] containsObject: torrent])
                        {
                            parent = group;
                            *stop = YES;
                        }
                    }];
                    if (parent)
                    {
                        [[fTableView animator] expandItem: parent];
                        row = [fTableView rowForItem: torrent];
                    }
                }

                if (row == -1)
                {
                    //not found - must be filtering
                    NSAssert([fDefaults boolForKey: @"FilterBar"], @"expected the filter to be enabled");
                    [fFilterBar reset: YES];

                    row = [fTableView rowForItem: torrent];

                    //if it's not shown, it has to be in a collapsed row...again
                    if ([fDefaults boolForKey: @"SortByGroup"])
                    {
                        __block TorrentGroup * parent = nil;
                        [fDisplayedTorrents enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: ^(TorrentGroup * group, NSUInteger idx, BOOL *stop) {
                            if ([[group torrents] containsObject: torrent])
                            {
                                parent = group;
                                *stop = YES;
                            }
                        }];
                        if (parent)
                        {
                            [[fTableView animator] expandItem: parent];
                            row = [fTableView rowForItem: torrent];
                        }
                    }
                }
            }

            NSAssert1(row != -1, @"expected a row to be found for torrent %@", torrent);

            [self showMainWindow: nil];
            [fTableView selectAndScrollToRow: row];
        }
    }
}

- (Torrent *) torrentForHash: (NSString *) hash
{
    NSParameterAssert(hash != nil);

    __block Torrent * torrent = nil;
    [fTorrents enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: ^(id obj, NSUInteger idx, BOOL * stop) {
        if ([[(Torrent *)obj hashString] isEqualToString: hash])
        {
            torrent = obj;
            *stop = YES;
        }
    }];
    return torrent;
}

- (void) torrentFinishedDownloading: (NSNotification *) notification
{
    Torrent * torrent = [notification object];

    if ([[[notification userInfo] objectForKey: @"WasRunning"] boolValue])
    {
        if (!fSoundPlaying && [fDefaults boolForKey: @"PlayDownloadSound"])
        {
            NSSound * sound;
            if ((sound = [NSSound soundNamed: [fDefaults stringForKey: @"DownloadSound"]]))
            {
                [sound setDelegate: self];
                fSoundPlaying = YES;
                [sound play];
            }
        }

        NSString * location = [torrent dataLocation];

        NSString * notificationTitle = NSLocalizedString(@"Download Complete", "notification title");
        if ([NSApp isOnMountainLionOrBetter])
        {
            NSUserNotification * notification = [[NSUserNotificationMtLion alloc] init];
            [notification setTitle: notificationTitle];
            [notification setInformativeText: [torrent name]];

            [notification setHasActionButton: YES];
            [notification setActionButtonTitle: NSLocalizedString(@"Show", "notification button")];

            NSMutableDictionary * userInfo = [NSMutableDictionary dictionaryWithObject: [torrent hashString] forKey: @"Hash"];
            if (location)
                [userInfo setObject: location forKey: @"Location"];
            [notification setUserInfo: userInfo];

            [[NSUserNotificationCenterMtLion defaultUserNotificationCenter] deliverNotification: notification];
            [notification release];
        }

        NSMutableDictionary * clickContext = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                              GROWL_DOWNLOAD_COMPLETE, @"Type", nil];

        if (location)
            [clickContext setObject: location forKey: @"Location"];

        [GrowlApplicationBridge notifyWithTitle: notificationTitle
                                    description: [torrent name] notificationName: GROWL_DOWNLOAD_COMPLETE
                                    iconData: nil priority: 0 isSticky: NO clickContext: clickContext];

        //NSLog(@"delegate: %@", [[NSUserNotificationCenterMtLion defaultUserNotificationCenter] delegate]);

        if (![fWindow isMainWindow])
            [fBadger addCompletedTorrent: torrent];

        //bounce download stack
        [[NSDistributedNotificationCenter defaultCenter] postNotificationName: @"com.apple.DownloadFileFinished"
            object: [torrent dataLocation]];
    }

    [self fullUpdateUI];
}

- (void) torrentRestartedDownloading: (NSNotification *) notification
{
    [self fullUpdateUI];
}

- (void) torrentFinishedSeeding: (NSNotification *) notification
{
    Torrent * torrent = [notification object];

    if (!fSoundPlaying && [fDefaults boolForKey: @"PlaySeedingSound"])
    {
        NSSound * sound;
        if ((sound = [NSSound soundNamed: [fDefaults stringForKey: @"SeedingSound"]]))
        {
            [sound setDelegate: self];
            fSoundPlaying = YES;
            [sound play];
        }
    }

    NSString * location = [torrent dataLocation];

    NSString * notificationTitle = NSLocalizedString(@"Seeding Complete", "notification title");
    if ([NSApp isOnMountainLionOrBetter])
    {
        NSUserNotification * notification = [[NSUserNotificationMtLion alloc] init];
        [notification setTitle: notificationTitle];
        [notification setInformativeText: [torrent name]];

        [notification setHasActionButton: YES];
        [notification setActionButtonTitle: NSLocalizedString(@"Show", "notification button")];

        NSMutableDictionary * userInfo = [NSMutableDictionary dictionaryWithObject: [torrent hashString] forKey: @"Hash"];
        if (location)
            [userInfo setObject: location forKey: @"Location"];
        [notification setUserInfo: userInfo];

        [[NSUserNotificationCenterMtLion defaultUserNotificationCenter] deliverNotification: notification];
        [notification release];
    }

    NSMutableDictionary * clickContext = [NSMutableDictionary dictionaryWithObject: GROWL_SEEDING_COMPLETE forKey: @"Type"];

    if (location)
        [clickContext setObject: location forKey: @"Location"];

    [GrowlApplicationBridge notifyWithTitle: notificationTitle
                                description: [torrent name] notificationName: GROWL_SEEDING_COMPLETE
                                   iconData: nil priority: 0 isSticky: NO clickContext: clickContext];

    //removing from the list calls fullUpdateUI
    if ([torrent removeWhenFinishSeeding])
        [self confirmRemoveTorrents: @[ torrent ] deleteData: NO];
    else
    {
        if (![fWindow isMainWindow])
            [fBadger addCompletedTorrent: torrent];

        [self fullUpdateUI];

        if ([[fTableView selectedTorrents] containsObject: torrent])
        {
            [fInfoController updateInfoStats];
            [fInfoController updateOptions];
        }
    }
}

- (void) updateTorrentHistory
{
    NSMutableArray * history = [NSMutableArray arrayWithCapacity: [fTorrents count]];

    for (Torrent * torrent in fTorrents)
        [history addObject: [torrent history]];

    NSString * historyFile = [fConfigDirectory stringByAppendingPathComponent: TRANSFER_PLIST];
    [history writeToFile: historyFile atomically: YES];
}

- (void) setSort: (id) sender
{
    NSString * sortType;
    switch ([(NSMenuItem *)sender tag])
    {
        case SORT_ORDER_TAG:
            sortType = SORT_ORDER;
            [fDefaults setBool: NO forKey: @"SortReverse"];
            break;
        case SORT_DATE_TAG:
            sortType = SORT_DATE;
            break;
        case SORT_NAME_TAG:
            sortType = SORT_NAME;
            break;
        case SORT_PROGRESS_TAG:
            sortType = SORT_PROGRESS;
            break;
        case SORT_STATE_TAG:
            sortType = SORT_STATE;
            break;
        case SORT_TRACKER_TAG:
            sortType = SORT_TRACKER;
            break;
        case SORT_ACTIVITY_TAG:
            sortType = SORT_ACTIVITY;
            break;
        case SORT_SIZE_TAG:
            sortType = SORT_SIZE;
            break;
        default:
            NSAssert1(NO, @"Unknown sort tag received: %ld", [(NSMenuItem *)sender tag]);
            return;
    }

    [fDefaults setObject: sortType forKey: @"Sort"];

    [self sortTorrents: YES];
}

- (void) setSortByGroup: (id) sender
{
    BOOL sortByGroup = ![fDefaults boolForKey: @"SortByGroup"];
    [fDefaults setBool: sortByGroup forKey: @"SortByGroup"];

    [self applyFilter];
}

- (void) setSortReverse: (id) sender
{
    const BOOL setReverse = [(NSMenuItem *)sender tag] == SORT_DESC_TAG;
    if (setReverse != [fDefaults boolForKey: @"SortReverse"])
    {
        [fDefaults setBool: setReverse forKey: @"SortReverse"];
        [self sortTorrents: NO];
    }
}

- (void) sortTorrents: (BOOL) includeQueueOrder
{
    //actually sort
    [self sortTorrentsCallUpdates: YES includeQueueOrder: includeQueueOrder];
    [fTableView setNeedsDisplay: YES];
}

- (void) sortTorrentsCallUpdates: (BOOL) callUpdates includeQueueOrder: (BOOL) includeQueueOrder
{
    const BOOL asc = ![fDefaults boolForKey: @"SortReverse"];

    NSArray * descriptors;
    NSSortDescriptor * nameDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"name" ascending: asc selector: @selector(localizedStandardCompare:)];

    NSString * sortType = [fDefaults stringForKey: @"Sort"];
    if ([sortType isEqualToString: SORT_STATE])
    {
        NSSortDescriptor * stateDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"stateSortKey" ascending: !asc],
                        * progressDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"progress" ascending: !asc],
                        * ratioDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"ratio" ascending: !asc];

        descriptors = [NSArray arrayWithObjects: stateDescriptor, progressDescriptor, ratioDescriptor, nameDescriptor, nil];
    }
    else if ([sortType isEqualToString: SORT_PROGRESS])
    {
        NSSortDescriptor * progressDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"progress" ascending: asc],
                        * ratioProgressDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"progressStopRatio" ascending: asc],
                        * ratioDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"ratio" ascending: asc];

        descriptors = [NSArray arrayWithObjects: progressDescriptor, ratioProgressDescriptor, ratioDescriptor, nameDescriptor, nil];
    }
    else if ([sortType isEqualToString: SORT_TRACKER])
    {
        NSSortDescriptor * trackerDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"trackerSortKey" ascending: asc selector: @selector(localizedCaseInsensitiveCompare:)];

        descriptors = [NSArray arrayWithObjects: trackerDescriptor, nameDescriptor, nil];
    }
    else if ([sortType isEqualToString: SORT_ACTIVITY])
    {
        NSSortDescriptor * rateDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"totalRate" ascending: !asc];
        NSSortDescriptor * activityDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"dateActivityOrAdd" ascending: !asc];

        descriptors = [NSArray arrayWithObjects: rateDescriptor, activityDescriptor, nameDescriptor, nil];
    }
    else if ([sortType isEqualToString: SORT_DATE])
    {
        NSSortDescriptor * dateDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"dateAdded" ascending: asc];

        descriptors = [NSArray arrayWithObjects: dateDescriptor, nameDescriptor, nil];
    }
    else if ([sortType isEqualToString: SORT_SIZE])
    {
        NSSortDescriptor * sizeDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"size" ascending: asc];

        descriptors = [NSArray arrayWithObjects: sizeDescriptor, nameDescriptor, nil];
    }
    else if ([sortType isEqualToString: SORT_NAME])
    {
        descriptors = [NSArray arrayWithObject: nameDescriptor];
    }
    else
    {
        NSAssert1([sortType isEqualToString: SORT_ORDER], @"Unknown sort type received: %@", sortType);

        if (!includeQueueOrder)
            return;

        NSSortDescriptor * orderDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"queuePosition" ascending: asc];

        descriptors = [NSArray arrayWithObject: orderDescriptor];
    }

    BOOL beganTableUpdate = !callUpdates;

    //actually sort
    if ([fDefaults boolForKey: @"SortByGroup"])
    {
        for (TorrentGroup * group in fDisplayedTorrents)
            [self rearrangeTorrentTableArray: [group torrents] forParent: group withSortDescriptors: descriptors beganTableUpdate: &beganTableUpdate];
    }
    else
        [self rearrangeTorrentTableArray: fDisplayedTorrents forParent: nil withSortDescriptors: descriptors beganTableUpdate: &beganTableUpdate];

    if (beganTableUpdate && callUpdates)
    {
        [fTableView endUpdates];
    }
}

#warning redo so that we search a copy once again (best explained by changing sorting from ascending to descending)
- (void) rearrangeTorrentTableArray: (NSMutableArray *) rearrangeArray forParent: parent withSortDescriptors: (NSArray *) descriptors beganTableUpdate: (BOOL *) beganTableUpdate
{
    for (NSUInteger currentIndex = 1; currentIndex < [rearrangeArray count]; ++currentIndex)
    {
        //manually do the sorting in-place
        const NSUInteger insertIndex = [rearrangeArray indexOfObject: [rearrangeArray objectAtIndex: currentIndex] inSortedRange: NSMakeRange(0, currentIndex) options: (NSBinarySearchingInsertionIndex | NSBinarySearchingLastEqual) usingComparator: ^NSComparisonResult(id obj1, id obj2) {
            for (NSSortDescriptor * descriptor in descriptors)
            {
                const NSComparisonResult result = [descriptor compareObject: obj1 toObject: obj2];
                if (result != NSOrderedSame)
                    return result;
            }

            return NSOrderedSame;
        }];

        if (insertIndex != currentIndex)
        {
            if (!*beganTableUpdate)
            {
                *beganTableUpdate = YES;
                [fTableView beginUpdates];
            }

            [rearrangeArray moveObjectAtIndex: currentIndex toIndex: insertIndex];
            [fTableView moveItemAtIndex: currentIndex inParent: parent toIndex: insertIndex inParent: parent];
        }
    }

    NSAssert2([rearrangeArray isEqualToArray: [rearrangeArray sortedArrayUsingDescriptors: descriptors]], @"Torrent rearranging didn't work! %@ %@", rearrangeArray, [rearrangeArray sortedArrayUsingDescriptors: descriptors]);
}

- (void) applyFilter
{
    __block int32_t active = 0, downloading = 0, seeding = 0, paused = 0;
    NSString * filterType = [fDefaults stringForKey: @"Filter"];
    BOOL filterActive = NO, filterDownload = NO, filterSeed = NO, filterPause = NO, filterStatus = YES;
    if ([filterType isEqualToString: FILTER_ACTIVE])
        filterActive = YES;
    else if ([filterType isEqualToString: FILTER_DOWNLOAD])
        filterDownload = YES;
    else if ([filterType isEqualToString: FILTER_SEED])
        filterSeed = YES;
    else if ([filterType isEqualToString: FILTER_PAUSE])
        filterPause = YES;
    else
        filterStatus = NO;

    const NSInteger groupFilterValue = [fDefaults integerForKey: @"FilterGroup"];
    const BOOL filterGroup = groupFilterValue != GROUP_FILTER_ALL_TAG;

    NSArray * searchStrings = [fFilterBar searchStrings];
    if (searchStrings && [searchStrings count] == 0)
        searchStrings = nil;
    const BOOL filterTracker = searchStrings && [[fDefaults stringForKey: @"FilterSearchType"] isEqualToString: FILTER_TYPE_TRACKER];

    //filter & get counts of each type
    NSIndexSet * indexesOfNonFilteredTorrents = [fTorrents indexesOfObjectsWithOptions: NSEnumerationConcurrent passingTest: ^BOOL(Torrent * torrent, NSUInteger idx, BOOL * stop) {
        //check status
        if ([torrent isActive] && ![torrent isCheckingWaiting])
        {
            const BOOL isActive = ![torrent isStalled];
            if (isActive)
                OSAtomicIncrement32(&active);

            if ([torrent isSeeding])
            {
                OSAtomicIncrement32(&seeding);
                if (filterStatus && !((filterActive && isActive) || filterSeed))
                    return NO;
            }
            else
            {
                OSAtomicIncrement32(&downloading);
                if (filterStatus && !((filterActive && isActive) || filterDownload))
                    return NO;
            }
        }
        else
        {
            OSAtomicIncrement32(&paused);
            if (filterStatus && !filterPause)
                return NO;
        }

        //checkGroup
        if (filterGroup)
            if ([torrent groupValue] != groupFilterValue)
                return NO;

        //check text field
        if (searchStrings)
        {
            __block BOOL removeTextField = NO;
            if (filterTracker)
            {
                NSArray * trackers = [torrent allTrackersFlat];

                //to count, we need each string in at least 1 tracker
                [searchStrings enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: ^(id searchString, NSUInteger idx, BOOL * stop) {
                    __block BOOL found = NO;
                    [trackers enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: ^(id tracker, NSUInteger idx, BOOL * stopTracker) {
                        if ([tracker rangeOfString: searchString options: (NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch)].location != NSNotFound)
                        {
                            found = YES;
                            *stopTracker = YES;
                        }
                    }];
                    if (!found)
                    {
                        removeTextField = YES;
                        *stop = YES;
                    }
                }];
            }
            else
            {
                [searchStrings enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: ^(id searchString, NSUInteger idx, BOOL * stop) {
                    if ([[torrent name] rangeOfString: searchString options: (NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch)].location == NSNotFound)
                    {
                        removeTextField = YES;
                        *stop = YES;
                    }
                }];
            }

            if (removeTextField)
                return NO;
        }

        return YES;
    }];

    NSArray * allTorrents = [fTorrents objectsAtIndexes: indexesOfNonFilteredTorrents];

    //set button tooltips
    if (fFilterBar)
        [fFilterBar setCountAll: [fTorrents count] active: active downloading: downloading seeding: seeding paused: paused];

    //if either the previous or current lists are blank, set its value to the other
    const BOOL groupRows = [allTorrents count] > 0 ? [fDefaults boolForKey: @"SortByGroup"] : ([fDisplayedTorrents count] > 0 && [[fDisplayedTorrents objectAtIndex: 0] isKindOfClass: [TorrentGroup class]]);
    const BOOL wasGroupRows = [fDisplayedTorrents count] > 0 ? [[fDisplayedTorrents objectAtIndex: 0] isKindOfClass: [TorrentGroup class]] : groupRows;

    #warning could probably be merged with later code somehow
    //clear display cache for not-shown torrents
    if ([fDisplayedTorrents count] > 0)
    {
        //for each torrent, removes the previous piece info if it's not in allTorrents, and keeps track of which torrents we already found in allTorrents
        void (^removePreviousFinishedPieces)(id, NSUInteger, BOOL *) = ^(Torrent * torrent, NSUInteger idx, BOOL * stop) {
            //we used to keep track of which torrents we already found in allTorrents, but it wasn't safe fo concurrent enumeration
            if (![allTorrents containsObject: torrent])
                [torrent setPreviousFinishedPieces: nil];
        };

        if (wasGroupRows)
            [fDisplayedTorrents enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: ^(id obj, NSUInteger idx, BOOL * stop) {
                [[(TorrentGroup *)obj torrents] enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: removePreviousFinishedPieces];
            }];
        else
            [fDisplayedTorrents enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: removePreviousFinishedPieces];
    }

    BOOL beganUpdates = NO;

    //don't animate torrents when first launching
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        [[NSAnimationContext currentContext] setDuration: 0];
    });
    [NSAnimationContext beginGrouping];

    //add/remove torrents (and rearrange for groups), one by one
    if (!groupRows && !wasGroupRows)
    {
        NSMutableIndexSet * addIndexes = [NSMutableIndexSet indexSet],
                        * removePreviousIndexes = [NSMutableIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fDisplayedTorrents count])];

        //for each of the torrents to add, find if it already exists (and keep track of those we've already added & those we need to remove)
        [allTorrents enumerateObjectsWithOptions: 0 usingBlock: ^(id objAll, NSUInteger previousIndex, BOOL * stop) {
            const NSUInteger currentIndex = [fDisplayedTorrents indexOfObjectAtIndexes: removePreviousIndexes options: NSEnumerationConcurrent passingTest: ^(id objDisplay, NSUInteger idx, BOOL *stop) {
                return (BOOL)(objAll == objDisplay);
            }];
            if (currentIndex == NSNotFound)
                [addIndexes addIndex: previousIndex];
            else
                [removePreviousIndexes removeIndex: currentIndex];
        }];

        if ([addIndexes count] > 0 || [removePreviousIndexes count] > 0)
        {
            beganUpdates = YES;
            [fTableView beginUpdates];

            //remove torrents we didn't find
            if ([removePreviousIndexes count] > 0)
            {
                [fDisplayedTorrents removeObjectsAtIndexes: removePreviousIndexes];
                [fTableView removeItemsAtIndexes: removePreviousIndexes inParent: nil withAnimation: NSTableViewAnimationSlideDown];
            }

            //add new torrents
            if ([addIndexes count] > 0)
            {
                //slide new torrents in differently
                if (fAddingTransfers)
                {
                    NSIndexSet * newAddIndexes = [allTorrents indexesOfObjectsAtIndexes: addIndexes options: NSEnumerationConcurrent passingTest: ^BOOL(id obj, NSUInteger idx, BOOL * stop) {
                        return [fAddingTransfers containsObject: obj];
                    }];

                    [addIndexes removeIndexes: newAddIndexes];

                    [fDisplayedTorrents addObjectsFromArray: [allTorrents objectsAtIndexes: newAddIndexes]];
                    [fTableView insertItemsAtIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange([fDisplayedTorrents count] - [newAddIndexes count], [newAddIndexes count])] inParent: nil withAnimation: NSTableViewAnimationSlideLeft];
                }

                [fDisplayedTorrents addObjectsFromArray: [allTorrents objectsAtIndexes: addIndexes]];
                [fTableView insertItemsAtIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange([fDisplayedTorrents count] - [addIndexes count], [addIndexes count])] inParent: nil withAnimation: NSTableViewAnimationSlideDown];
            }
        }
    }
    else if (groupRows && wasGroupRows)
    {
        NSAssert(groupRows && wasGroupRows, @"Should have had group rows and should remain with group rows");

        #warning don't always do?
        beganUpdates = YES;
        [fTableView beginUpdates];

        NSMutableIndexSet * unusedAllTorrentsIndexes = [NSMutableIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [allTorrents count])];

        NSMutableDictionary * groupsByIndex = [NSMutableDictionary dictionaryWithCapacity: [fDisplayedTorrents count]];
        for (TorrentGroup * group in fDisplayedTorrents)
            [groupsByIndex setObject: group forKey: [NSNumber numberWithInteger: [group groupIndex]]];

        const NSUInteger originalGroupCount = [fDisplayedTorrents count];
        for (NSUInteger index = 0; index < originalGroupCount; ++index)
        {
            TorrentGroup * group = [fDisplayedTorrents objectAtIndex: index];

            NSMutableIndexSet * removeIndexes = [NSMutableIndexSet indexSet];

            //needs to be a signed integer
            for (NSInteger indexInGroup = 0; indexInGroup < [[group torrents] count]; ++indexInGroup)
            {
                Torrent * torrent = [[group torrents] objectAtIndex: indexInGroup];
                const NSUInteger allIndex = [allTorrents indexOfObjectAtIndexes: unusedAllTorrentsIndexes options: NSEnumerationConcurrent passingTest: ^(id obj, NSUInteger idx, BOOL * stop) {
                    return (BOOL)(obj == torrent);
                }];
                if (allIndex == NSNotFound)
                    [removeIndexes addIndex: indexInGroup];
                else
                {
                    BOOL markTorrentAsUsed = YES;

                    const NSInteger groupValue = [torrent groupValue];
                    if (groupValue != [group groupIndex])
                    {
                        TorrentGroup * newGroup = [groupsByIndex objectForKey: [NSNumber numberWithInteger: groupValue]];
                        if (!newGroup)
                        {
                            newGroup = [[[TorrentGroup alloc] initWithGroup: groupValue] autorelease];
                            [groupsByIndex setObject: newGroup forKey: [NSNumber numberWithInteger: groupValue]];
                            [fDisplayedTorrents addObject: newGroup];

                            [fTableView insertItemsAtIndexes: [NSIndexSet indexSetWithIndex: [fDisplayedTorrents count]-1] inParent: nil withAnimation: NSTableViewAnimationEffectFade];
                            [fTableView isGroupCollapsed: groupValue] ? [fTableView collapseItem: newGroup] : [fTableView expandItem: newGroup];
                        }
                        else //if we haven't processed the other group yet, we have to make sure we don't flag it for removal the next time
                        {
                            //ugggh, but shouldn't happen too often
                            if ([fDisplayedTorrents indexOfObject: newGroup inRange: NSMakeRange(index+1, originalGroupCount-(index+1))] != NSNotFound)
                                markTorrentAsUsed = NO;
                        }

                        [[group torrents] removeObjectAtIndex: indexInGroup];
                        [[newGroup torrents] addObject: torrent];

                        [fTableView moveItemAtIndex: indexInGroup inParent: group toIndex: [[newGroup torrents] count]-1 inParent: newGroup];

                        --indexInGroup;
                    }

                    if (markTorrentAsUsed)
                        [unusedAllTorrentsIndexes removeIndex: allIndex];
                }
            }

            if ([removeIndexes count] > 0)
            {
                [[group torrents] removeObjectsAtIndexes: removeIndexes];
                [fTableView removeItemsAtIndexes: removeIndexes inParent: group withAnimation: NSTableViewAnimationEffectFade];
            }
        }

        //add remaining new torrents
        for (Torrent * torrent in [allTorrents objectsAtIndexes: unusedAllTorrentsIndexes])
        {
            const NSInteger groupValue = [torrent groupValue];
            TorrentGroup * group = [groupsByIndex objectForKey: [NSNumber numberWithInteger: groupValue]];
            if (!group)
            {
                group = [[[TorrentGroup alloc] initWithGroup: groupValue] autorelease];
                [groupsByIndex setObject: group forKey: [NSNumber numberWithInteger: groupValue]];
                [fDisplayedTorrents addObject: group];

                [fTableView insertItemsAtIndexes: [NSIndexSet indexSetWithIndex: [fDisplayedTorrents count]-1] inParent: nil withAnimation: NSTableViewAnimationEffectFade];
                [fTableView isGroupCollapsed: groupValue] ? [fTableView collapseItem: group] : [fTableView expandItem: group];
            }

            [[group torrents] addObject: torrent];

            const BOOL newTorrent = fAddingTransfers && [fAddingTransfers containsObject: torrent];
            [fTableView insertItemsAtIndexes: [NSIndexSet indexSetWithIndex: [[group torrents] count]-1] inParent: group withAnimation: newTorrent ? NSTableViewAnimationSlideLeft : NSTableViewAnimationSlideDown];
        }

        //remove empty groups
        NSIndexSet * removeGroupIndexes = [fDisplayedTorrents indexesOfObjectsAtIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, originalGroupCount)] options: NSEnumerationConcurrent passingTest: ^BOOL(id obj, NSUInteger idx, BOOL * stop) {
            return [[(TorrentGroup *)obj torrents] count] == 0;
        }];

        if ([removeGroupIndexes count] > 0)
        {
            [fDisplayedTorrents removeObjectsAtIndexes: removeGroupIndexes];
            [fTableView removeItemsAtIndexes: removeGroupIndexes inParent: nil withAnimation: NSTableViewAnimationEffectFade];
        }

        //now that all groups are there, sort them - don't insert on the fly in case groups were reordered in prefs
        NSSortDescriptor * groupDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"groupOrderValue" ascending: YES];
        [self rearrangeTorrentTableArray: fDisplayedTorrents forParent: nil withSortDescriptors: [NSArray arrayWithObject: groupDescriptor] beganTableUpdate: &beganUpdates];
    }
    else
    {
        NSAssert(groupRows != wasGroupRows, @"Trying toggling group-torrent reordering when we weren't expecting to.");

        //set all groups as expanded
        [fTableView removeAllCollapsedGroups];

        //since we're not doing this the right way (boo buggy animation), we need to remember selected values
        #warning when Lion-only and using views instead of cells, this likely won't be needed
        NSArray * selectedValues = [fTableView selectedValues];

        beganUpdates = YES;
        [fTableView beginUpdates];

        [fTableView removeItemsAtIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fDisplayedTorrents count])] inParent: nil withAnimation: NSTableViewAnimationSlideDown];

        if (groupRows)
        {
            //a map for quickly finding groups
            NSMutableDictionary * groupsByIndex = [NSMutableDictionary dictionaryWithCapacity: [[GroupsController groups] numberOfGroups]];
            for (Torrent * torrent in allTorrents)
            {
                const NSInteger groupValue = [torrent groupValue];
                TorrentGroup * group = [groupsByIndex objectForKey: [NSNumber numberWithInteger: groupValue]];
                if (!group)
                {
                    group = [[[TorrentGroup alloc] initWithGroup: groupValue] autorelease];
                    [groupsByIndex setObject: group forKey: [NSNumber numberWithInteger: groupValue]];
                }

                [[group torrents] addObject: torrent];
            }

            [fDisplayedTorrents setArray: [groupsByIndex allValues]];

            //we need the groups to be sorted, and we can do it without moving items in the table, too!
            NSSortDescriptor * groupDescriptor = [NSSortDescriptor sortDescriptorWithKey: @"groupOrderValue" ascending: YES];
            [fDisplayedTorrents sortUsingDescriptors: [NSArray arrayWithObject: groupDescriptor]];
        }
        else
            [fDisplayedTorrents setArray: allTorrents];

        [fTableView insertItemsAtIndexes: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fDisplayedTorrents count])] inParent: nil withAnimation: NSTableViewAnimationEffectFade];

        if (groupRows)
        {
            //actually expand group rows
            for (TorrentGroup * group in fDisplayedTorrents)
                [fTableView expandItem: group];
        }

        if (selectedValues)
            [fTableView selectValues: selectedValues];
    }

    //sort the torrents (won't sort the groups, though)
    [self sortTorrentsCallUpdates: !beganUpdates includeQueueOrder: YES];

    if (beganUpdates)
        [fTableView endUpdates];
    [fTableView setNeedsDisplay: YES];

    [NSAnimationContext endGrouping];

    [self resetInfo]; //if group is already selected, but the torrents in it change

    [self setBottomCountText: groupRows || filterStatus || filterGroup || searchStrings];

    [self setWindowSizeToFit];

    if (fAddingTransfers)
    {
        [fAddingTransfers release];
        fAddingTransfers = nil;
    }
}

- (void) switchFilter: (id) sender
{
    [fFilterBar switchFilter: sender == fNextFilterItem];
}

- (IBAction) showGlobalPopover: (id) sender
{
    if (fGlobalPopoverShown)
        return;

    NSPopover * popover = [[NSPopover alloc] init];
    [popover setBehavior: NSPopoverBehaviorTransient];
    GlobalOptionsPopoverViewController * viewController = [[GlobalOptionsPopoverViewController alloc] initWithHandle: fLib];
    [popover setContentViewController: viewController];
    [popover setDelegate: self];

    [popover showRelativeToRect: [sender frame] ofView: sender preferredEdge: NSMaxYEdge];

    [viewController release];
    [popover release];
}

//don't show multiple popovers when clicking the gear button repeatedly
- (void) popoverWillShow: (NSNotification *) notification
{
    fGlobalPopoverShown = YES;
}

- (void) popoverWillClose: (NSNotification *) notification
{
    fGlobalPopoverShown = NO;
}

- (void) menuNeedsUpdate: (NSMenu *) menu
{
    if (menu == fGroupsSetMenu || menu == fGroupsSetContextMenu)
    {
        for (NSInteger i = [menu numberOfItems]-1; i >= 0; i--)
            [menu removeItemAtIndex: i];

        NSMenu * groupMenu = [[GroupsController groups] groupMenuWithTarget: self action: @selector(setGroup:) isSmall: NO];

        const NSInteger groupMenuCount = [groupMenu numberOfItems];
        for (NSInteger i = 0; i < groupMenuCount; i++)
        {
            NSMenuItem * item = [[groupMenu itemAtIndex: 0] retain];
            [groupMenu removeItemAtIndex: 0];
            [menu addItem: item];
            [item release];
        }
    }
    else if (menu == fShareMenu || menu == fShareContextMenu) {
        [menu removeAllItems];

        for (NSMenuItem * item in [[ShareTorrentFileHelper sharedHelper] menuItems])
        {
            [menu addItem:item];
        }
    }
    else;
}

- (void) setGroup: (id) sender
{
    for (Torrent * torrent in [fTableView selectedTorrents])
    {
        [fTableView removeCollapsedGroup: [torrent groupValue]]; //remove old collapsed group

        [torrent setGroupValue: [(NSMenuItem *)sender tag] determinationType: TorrentDeterminationUserSpecified];
    }

    [self applyFilter];
    [self updateUI];
    [self updateTorrentHistory];
}

- (void) toggleSpeedLimit: (id) sender
{
    [fDefaults setBool: ![fDefaults boolForKey: @"SpeedLimit"] forKey: @"SpeedLimit"];
    [self speedLimitChanged: sender];
}

- (void) speedLimitChanged: (id) sender
{
    tr_sessionUseAltSpeed(fLib, [fDefaults boolForKey: @"SpeedLimit"]);
    [fStatusBar updateSpeedFieldsToolTips];
}

//dict has been retained
- (void) altSpeedToggledCallbackIsLimited: (NSDictionary *) dict
{
    const BOOL isLimited = [[dict objectForKey: @"Active"] boolValue];

    [fDefaults setBool: isLimited forKey: @"SpeedLimit"];
    [fStatusBar updateSpeedFieldsToolTips];

    if (![[dict objectForKey: @"ByUser"] boolValue])
        [GrowlApplicationBridge notifyWithTitle: isLimited ? NSLocalizedString(@"Speed Limit Auto Enabled", "Growl notification title") : NSLocalizedString(@"Speed Limit Auto Disabled", "Growl notification title")
                                    description: NSLocalizedString(@"Bandwidth settings changed", "Growl notification description")
                               notificationName: GROWL_AUTO_SPEED_LIMIT
                                       iconData: nil
                                       priority: 0
                                       isSticky: NO
                                   clickContext: nil
                                     identifier: GROWL_AUTO_SPEED_LIMIT];

    [dict release];
}

- (void) sound: (NSSound *) sound didFinishPlaying: (BOOL) finishedPlaying
{
    fSoundPlaying = NO;
}

-(void) VDKQueue: (VDKQueue *) queue receivedNotification: (NSString*) notification forPath: (NSString*) fpath
{
    //don't assume that just because we're watching for write notification, we'll only receive write notifications

    if (![fDefaults boolForKey: @"AutoImport"] || ![fDefaults stringForKey: @"AutoImportDirectory"])
        return;

    if ([fAutoImportTimer isValid])
        [fAutoImportTimer invalidate];
    [fAutoImportTimer release];

    //check again in 10 seconds in case torrent file wasn't complete
    fAutoImportTimer = [[NSTimer scheduledTimerWithTimeInterval: 10.0 target: self
        selector: @selector(checkAutoImportDirectory) userInfo: nil repeats: NO] retain];

    [self checkAutoImportDirectory];
}

- (void) changeAutoImport
{
    if ([fAutoImportTimer isValid])
        [fAutoImportTimer invalidate];
    [fAutoImportTimer release];
    fAutoImportTimer = nil;

    [fAutoImportedNames release];
    fAutoImportedNames = nil;

    [self checkAutoImportDirectory];
}

- (void) checkAutoImportDirectory
{
    NSString * path;
    if (![fDefaults boolForKey: @"AutoImport"] || !(path = [fDefaults stringForKey: @"AutoImportDirectory"]))
        return;

    path = [path stringByExpandingTildeInPath];

    NSArray * importedNames;
    if (!(importedNames = [[NSFileManager defaultManager] contentsOfDirectoryAtPath: path error: NULL]))
        return;

    //only check files that have not been checked yet
    NSMutableArray * newNames = [importedNames mutableCopy];

    if (fAutoImportedNames)
        [newNames removeObjectsInArray: fAutoImportedNames];
    else
        fAutoImportedNames = [[NSMutableArray alloc] init];
    [fAutoImportedNames setArray: importedNames];

    for (NSString * file in newNames)
    {
        if ([file hasPrefix: @"."])
            continue;

        NSString * fullFile = [path stringByAppendingPathComponent: file];

        if (!([[[NSWorkspace sharedWorkspace] typeOfFile: fullFile error: NULL] isEqualToString: @"org.bittorrent.torrent"]
                || [[fullFile pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame))
            continue;

        tr_ctor * ctor = tr_ctorNew(fLib);
        tr_ctorSetMetainfoFromFile(ctor, [fullFile UTF8String]);

        switch (tr_torrentParse(ctor, NULL))
        {
            case TR_PARSE_OK:
                [self openFiles: [NSArray arrayWithObject: fullFile] addType: ADD_AUTO forcePath: nil];

                NSString * notificationTitle = NSLocalizedString(@"Torrent File Auto Added", "notification title");
                if ([NSApp isOnMountainLionOrBetter])
                {
                    NSUserNotification* notification = [[NSUserNotificationMtLion alloc] init];
                    [notification setTitle: notificationTitle];
                    [notification setInformativeText: file];

                    [notification setHasActionButton: NO];

                    [[NSUserNotificationCenterMtLion defaultUserNotificationCenter] deliverNotification: notification];
                    [notification release];
                }

                [GrowlApplicationBridge notifyWithTitle: notificationTitle
                    description: file notificationName: GROWL_AUTO_ADD iconData: nil priority: 0 isSticky: NO
                    clickContext: nil];
                break;

            case TR_PARSE_ERR:
                [fAutoImportedNames removeObject: file];
                break;

            case TR_PARSE_DUPLICATE: //let's ignore this (but silence a warning)
                break;
        }

        tr_ctorFree(ctor);
    }

    [newNames release];
}

- (void) beginCreateFile: (NSNotification *) notification
{
    if (![fDefaults boolForKey: @"AutoImport"])
        return;

    NSString * location = [(NSURL *)[notification object] path],
            * path = [fDefaults stringForKey: @"AutoImportDirectory"];

    if (location && path && [[[location stringByDeletingLastPathComponent] stringByExpandingTildeInPath]
                                    isEqualToString: [path stringByExpandingTildeInPath]])
        [fAutoImportedNames addObject: [location lastPathComponent]];
}

- (NSInteger) outlineView: (NSOutlineView *) outlineView numberOfChildrenOfItem: (id) item
{
    if (item)
        return [[item torrents] count];
    else
        return [fDisplayedTorrents count];
}

- (id) outlineView: (NSOutlineView *) outlineView child: (NSInteger) index ofItem: (id) item
{
    if (item)
        return [[item torrents] objectAtIndex: index];
    else
        return [fDisplayedTorrents objectAtIndex: index];
}

- (BOOL) outlineView: (NSOutlineView *) outlineView isItemExpandable: (id) item
{
    return ![item isKindOfClass: [Torrent class]];
}

- (id) outlineView: (NSOutlineView *) outlineView objectValueForTableColumn: (NSTableColumn *) tableColumn byItem: (id) item
{
    if ([item isKindOfClass: [Torrent class]]) {
        if (tableColumn)
            return nil;
        return [item hashString];
    }
    else
    {
        NSString * ident = [tableColumn identifier];
        if ([ident isEqualToString: @"Group"])
        {
            NSInteger group = [item groupIndex];
            return group != -1 ? [[GroupsController groups] nameForIndex: group]
                                : NSLocalizedString(@"No Group", "Group table row");
        }
        else if ([ident isEqualToString: @"Color"])
        {
            NSInteger group = [item groupIndex];
            return [[GroupsController groups] imageForIndex: group];
        }
        else if ([ident isEqualToString: @"DL Image"])
            return [NSImage imageNamed: @"DownArrowGroupTemplate"];
        else if ([ident isEqualToString: @"UL Image"])
            return [NSImage imageNamed: [fDefaults boolForKey: @"DisplayGroupRowRatio"]
                                        ? @"YingYangGroupTemplate" : @"UpArrowGroupTemplate"];
        else
        {
            TorrentGroup * group = (TorrentGroup *)item;

            if ([fDefaults boolForKey: @"DisplayGroupRowRatio"])
                return [NSString stringForRatio: [group ratio]];
            else
            {
                CGFloat rate = [ident isEqualToString: @"UL"] ? [group uploadRate] : [group downloadRate];
                return [NSString stringForSpeed: rate];
            }
        }
    }
}

- (BOOL) outlineView: (NSOutlineView *) outlineView writeItems: (NSArray *) items toPasteboard: (NSPasteboard *) pasteboard
{
    //only allow reordering of rows if sorting by order
    if ([fDefaults boolForKey: @"SortByGroup"] || [[fDefaults stringForKey: @"Sort"] isEqualToString: SORT_ORDER])
    {
        NSMutableIndexSet * indexSet = [NSMutableIndexSet indexSet];
        for (id torrent in items)
        {
            if (![torrent isKindOfClass: [Torrent class]])
                return NO;

            [indexSet addIndex: [fTableView rowForItem: torrent]];
        }

        [pasteboard declareTypes: [NSArray arrayWithObject: TORRENT_TABLE_VIEW_DATA_TYPE] owner: self];
        [pasteboard setData: [NSKeyedArchiver archivedDataWithRootObject: indexSet] forType: TORRENT_TABLE_VIEW_DATA_TYPE];
        return YES;
    }
    return NO;
}

- (NSDragOperation) outlineView: (NSOutlineView *) outlineView validateDrop: (id < NSDraggingInfo >) info proposedItem: (id) item
    proposedChildIndex: (NSInteger) index
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: TORRENT_TABLE_VIEW_DATA_TYPE])
    {
        if ([fDefaults boolForKey: @"SortByGroup"])
        {
            if (!item)
                return NSDragOperationNone;

            if ([[fDefaults stringForKey: @"Sort"] isEqualToString: SORT_ORDER])
            {
                if ([item isKindOfClass: [Torrent class]])
                {
                    TorrentGroup * group = [fTableView parentForItem: item];
                    index = [[group torrents] indexOfObject: item] + 1;
                    item = group;
                }
            }
            else
            {
                if ([item isKindOfClass: [Torrent class]])
                    item = [fTableView parentForItem: item];
                index = NSOutlineViewDropOnItemIndex;
            }
        }
        else
        {
            if (index == NSOutlineViewDropOnItemIndex)
                return NSDragOperationNone;

            if (item)
            {
                index = [fTableView rowForItem: item] + 1;
                item = nil;
            }
        }

        [fTableView setDropItem: item dropChildIndex: index];
        return NSDragOperationGeneric;
    }

    return NSDragOperationNone;
}

- (BOOL) outlineView: (NSOutlineView *) outlineView acceptDrop: (id < NSDraggingInfo >) info item: (id) item childIndex: (NSInteger) newRow
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: TORRENT_TABLE_VIEW_DATA_TYPE])
    {
        NSIndexSet * indexes = [NSKeyedUnarchiver unarchiveObjectWithData: [pasteboard dataForType: TORRENT_TABLE_VIEW_DATA_TYPE]];

        //get the torrents to move
        NSMutableArray * movingTorrents = [NSMutableArray arrayWithCapacity: [indexes count]];
        for (NSUInteger i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
        {
            Torrent * torrent = [fTableView itemAtRow: i];
            [movingTorrents addObject: torrent];
        }

        //change groups
        if (item)
        {
            const NSInteger groupIndex = [item groupIndex];

            for (Torrent * torrent in movingTorrents)
                [torrent setGroupValue: groupIndex determinationType: TorrentDeterminationUserSpecified];
        }

        //reorder queue order
        if (newRow != NSOutlineViewDropOnItemIndex)
        {
            //find torrent to place under
            NSArray * groupTorrents = item ? [item torrents] : fDisplayedTorrents;
            Torrent * topTorrent = nil;
            for (NSInteger i = newRow-1; i >= 0; i--)
            {
                Torrent * tempTorrent = [groupTorrents objectAtIndex: i];
                if (![movingTorrents containsObject: tempTorrent])
                {
                    topTorrent = tempTorrent;
                    break;
                }
            }

            //remove objects to reinsert
            [fTorrents removeObjectsInArray: movingTorrents];

            //insert objects at new location
            const NSUInteger insertIndex = topTorrent ? [fTorrents indexOfObject: topTorrent] + 1 : 0;
            NSIndexSet * insertIndexes = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(insertIndex, [movingTorrents count])];
            [fTorrents insertObjects: movingTorrents atIndexes: insertIndexes];

            //we need to make sure the queue order is updated in the Torrent object before we sort - safest to just reset all queue positions
            NSUInteger i = 0;
            for (Torrent * torrent in fTorrents)
            {
                [torrent setQueuePosition: i++];
                [torrent update];
            }

            //do the drag animation here so that the dragged torrents are the ones that are animated as moving, and not the torrents around them
            [fTableView beginUpdates];

            NSUInteger insertDisplayIndex = topTorrent ? [groupTorrents indexOfObject: topTorrent] + 1 : 0;

            for (Torrent * torrent in movingTorrents)
            {
                TorrentGroup * oldParent = item ? [fTableView parentForItem: torrent] : nil;
                NSMutableArray * oldTorrents = oldParent ? [oldParent torrents] : fDisplayedTorrents;
                const NSUInteger oldIndex = [oldTorrents indexOfObject: torrent];

                if (item == oldParent)
                {
                    if (oldIndex < insertDisplayIndex)
                        --insertDisplayIndex;
                    [oldTorrents moveObjectAtIndex: oldIndex toIndex: insertDisplayIndex];
                }
                else
                {
                    NSAssert(item && oldParent, @"Expected to be dragging between group rows");

                    NSMutableArray * newTorrents = [(TorrentGroup *)item torrents];
                    [newTorrents insertObject: torrent atIndex: insertDisplayIndex];
                    [oldTorrents removeObjectAtIndex: oldIndex];
                }

                [fTableView moveItemAtIndex: oldIndex inParent: oldParent toIndex: insertDisplayIndex inParent: item];

                ++insertDisplayIndex;
            }

            [fTableView endUpdates];
        }

        [self applyFilter];
    }

    return YES;
}

- (void) torrentTableViewSelectionDidChange: (NSNotification *) notification
{
    [self resetInfo];
    [[fWindow toolbar] validateVisibleItems];
}

- (NSDragOperation) draggingEntered: (id <NSDraggingInfo>) info
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: NSFilenamesPboardType])
    {
        //check if any torrent files can be added
        BOOL torrent = NO;
        NSArray * files = [pasteboard propertyListForType: NSFilenamesPboardType];
        for (NSString * file in files)
        {
            if ([[[NSWorkspace sharedWorkspace] typeOfFile: file error: NULL] isEqualToString: @"org.bittorrent.torrent"]
                    || [[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
            {
                torrent = YES;
                tr_ctor * ctor = tr_ctorNew(fLib);
                tr_ctorSetMetainfoFromFile(ctor, [file UTF8String]);
                if (tr_torrentParse(ctor, NULL) == TR_PARSE_OK)
                {
                    if (!fOverlayWindow)
                        fOverlayWindow = [[DragOverlayWindow alloc] initWithLib: fLib forWindow: fWindow];
                    [fOverlayWindow setTorrents: files];

                    return NSDragOperationCopy;
                }
                tr_ctorFree(ctor);
            }
        }

        //create a torrent file if a single file
        if (!torrent && [files count] == 1)
        {
            if (!fOverlayWindow)
                fOverlayWindow = [[DragOverlayWindow alloc] initWithLib: fLib forWindow: fWindow];
            [fOverlayWindow setFile: [[files objectAtIndex: 0] lastPathComponent]];

            return NSDragOperationCopy;
        }
    }
    else if ([[pasteboard types] containsObject: NSURLPboardType])
    {
        if (!fOverlayWindow)
            fOverlayWindow = [[DragOverlayWindow alloc] initWithLib: fLib forWindow: fWindow];
        [fOverlayWindow setURL: [[NSURL URLFromPasteboard: pasteboard] relativeString]];

        return NSDragOperationCopy;
    }
    else;

    return NSDragOperationNone;
}

- (void) draggingExited: (id <NSDraggingInfo>) info
{
    if (fOverlayWindow)
        [fOverlayWindow fadeOut];
}

- (BOOL) performDragOperation: (id <NSDraggingInfo>) info
{
    if (fOverlayWindow)
        [fOverlayWindow fadeOut];

    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: NSFilenamesPboardType])
    {
        BOOL torrent = NO, accept = YES;

        //create an array of files that can be opened
        NSArray * files = [pasteboard propertyListForType: NSFilenamesPboardType];
        NSMutableArray * filesToOpen = [NSMutableArray arrayWithCapacity: [files count]];
        for (NSString * file in files)
        {
            if ([[[NSWorkspace sharedWorkspace] typeOfFile: file error: NULL] isEqualToString: @"org.bittorrent.torrent"]
                    || [[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
            {
                torrent = YES;
                tr_ctor * ctor = tr_ctorNew(fLib);
                tr_ctorSetMetainfoFromFile(ctor, [file UTF8String]);
                if (tr_torrentParse(ctor, NULL) == TR_PARSE_OK)
                    [filesToOpen addObject: file];
                tr_ctorFree(ctor);
            }
        }

        if ([filesToOpen count] > 0)
            [self application: NSApp openFiles: filesToOpen];
        else
        {
            if (!torrent && [files count] == 1)
                [CreatorWindowController createTorrentFile: fLib forFile: [NSURL fileURLWithPath: [files objectAtIndex: 0]]];
            else
                accept = NO;
        }

        return accept;
    }
    else if ([[pasteboard types] containsObject: NSURLPboardType])
    {
        NSURL * url;
        if ((url = [NSURL URLFromPasteboard: pasteboard]))
        {
            [self openURL: [url absoluteString]];
            return YES;
        }
    }
    else;

    return NO;
}

- (void) toggleSmallView: (id) sender
{
    BOOL makeSmall = ![fDefaults boolForKey: @"SmallView"];
    [fDefaults setBool: makeSmall forKey: @"SmallView"];

    [fTableView setUsesAlternatingRowBackgroundColors: !makeSmall];

    [fTableView setRowHeight: makeSmall ? ROW_HEIGHT_SMALL : ROW_HEIGHT_REGULAR];

    [fTableView beginUpdates];
    [fTableView noteHeightOfRowsWithIndexesChanged: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTableView numberOfRows])]];
    [fTableView endUpdates];

    //resize for larger min height if not set to auto size
    if (![fDefaults boolForKey: @"AutoSize"])
    {
        const NSSize contentSize = [[fWindow contentView] frame].size;

        NSSize contentMinSize = [fWindow contentMinSize];
        contentMinSize.height = [self minWindowContentSizeAllowed];
        [fWindow setContentMinSize: contentMinSize];

        //make sure the window already isn't too small
        if (!makeSmall && contentSize.height < contentMinSize.height)
        {
            NSRect frame = [fWindow frame];
            CGFloat heightChange = contentMinSize.height - contentSize.height;
            frame.size.height += heightChange;
            frame.origin.y -= heightChange;

            [fWindow setFrame: frame display: YES];
        }
    }
    else
        [self setWindowSizeToFit];
}

- (void) togglePiecesBar: (id) sender
{
    [fDefaults setBool: ![fDefaults boolForKey: @"PiecesBar"] forKey: @"PiecesBar"];
    [fTableView togglePiecesBar];
}

- (void) toggleAvailabilityBar: (id) sender
{
    [fDefaults setBool: ![fDefaults boolForKey: @"DisplayProgressBarAvailable"] forKey: @"DisplayProgressBarAvailable"];
    [fTableView display];
}

- (NSRect) windowFrameByAddingHeight: (CGFloat) height checkLimits: (BOOL) check
{
    NSScrollView * scrollView = [fTableView enclosingScrollView];

    //convert pixels to points
    NSRect windowFrame = [fWindow frame];
    NSSize windowSize = [scrollView convertSize: windowFrame.size fromView: nil];
    windowSize.height += height;

    if (check)
    {
        //we can't call minSize, since it might be set to the current size (auto size)
        const CGFloat minHeight = [self minWindowContentSizeAllowed]
                                    + (NSHeight([fWindow frame]) - NSHeight([[fWindow contentView] frame])); //contentView to window

        if (windowSize.height <= minHeight)
            windowSize.height = minHeight;
        else
        {
            NSScreen * screen = [fWindow screen];
            if (screen)
            {
                NSSize maxSize = [scrollView convertSize: [screen visibleFrame].size fromView: nil];
                if (!fStatusBar)
                    maxSize.height -= STATUS_BAR_HEIGHT;
                if (!fFilterBar)
                    maxSize.height -= FILTER_BAR_HEIGHT;
                if (windowSize.height > maxSize.height)
                    windowSize.height = maxSize.height;
            }
        }
    }

    //convert points to pixels
    windowSize = [scrollView convertSize: windowSize toView: nil];

    windowFrame.origin.y -= (windowSize.height - windowFrame.size.height);
    windowFrame.size.height = windowSize.height;
    return windowFrame;
}

- (void) toggleStatusBar: (id) sender
{
    const BOOL show = fStatusBar == nil;
    [self showStatusBar: show animate: YES];
    [fDefaults setBool: show forKey: @"StatusBar"];
}

//doesn't save shown state
- (void) showStatusBar: (BOOL) show animate: (BOOL) animate
{
    const BOOL prevShown = fStatusBar != nil;
    if (show == prevShown)
        return;

    if (show)
    {
        fStatusBar = [[StatusBarController alloc] initWithLib: fLib];

        NSView * contentView = [fWindow contentView];
        const NSSize windowSize = [contentView convertSize: [fWindow frame].size fromView: nil];

        NSRect statusBarFrame = [[fStatusBar view] frame];
        statusBarFrame.size.width = windowSize.width;
        [[fStatusBar view] setFrame: statusBarFrame];

        [contentView addSubview: [fStatusBar view]];
        [[fStatusBar view] setFrameOrigin: NSMakePoint(0.0, NSMaxY([contentView frame]))];
    }

    CGFloat heightChange = [[fStatusBar view] frame].size.height;
    if (!show)
        heightChange *= -1;

    //allow bar to show even if not enough room
    if (show && ![fDefaults boolForKey: @"AutoSize"])
    {
        NSRect frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];

        NSScreen * screen = [fWindow screen];
        if (screen)
        {
            CGFloat change = [screen visibleFrame].size.height - frame.size.height;
            if (change < 0.0)
            {
                frame = [fWindow frame];
                frame.size.height += change;
                frame.origin.y -= change;
                [fWindow setFrame: frame display: NO animate: NO];
            }
        }
    }

    [self updateUI];

    NSScrollView * scrollView = [fTableView enclosingScrollView];

    //set views to not autoresize
    const NSUInteger statsMask = [[fStatusBar view] autoresizingMask];
    [[fStatusBar view] setAutoresizingMask: NSViewNotSizable];
    NSUInteger filterMask;
    if (fFilterBar)
    {
        filterMask = [[fFilterBar view] autoresizingMask];
        [[fFilterBar view] setAutoresizingMask: NSViewNotSizable];
    }
    const NSUInteger scrollMask = [scrollView autoresizingMask];
    [scrollView setAutoresizingMask: NSViewNotSizable];

    NSRect frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];
    [fWindow setFrame: frame display: YES animate: animate];

    //re-enable autoresize
    [[fStatusBar view] setAutoresizingMask: statsMask];
    if (fFilterBar)
        [[fFilterBar view] setAutoresizingMask: filterMask];
    [scrollView setAutoresizingMask: scrollMask];

    if (!show)
    {
        [[fStatusBar view] removeFromSuperviewWithoutNeedingDisplay];
        [fStatusBar release];
        fStatusBar = nil;
    }

    if ([fDefaults boolForKey: @"AutoSize"])
        [self setWindowMinMaxToCurrent];
    else
    {
        //change min size
        NSSize minSize = [fWindow contentMinSize];
        minSize.height += heightChange;
        [fWindow setContentMinSize: minSize];
    }
}

- (void) toggleFilterBar: (id) sender
{
    const BOOL show = fFilterBar == nil;

    //disable filtering when hiding (have to do before showFilterBar:animate:)
    if (!show)
        [fFilterBar reset: NO];

    [self showFilterBar: show animate: YES];
    [fDefaults setBool: show forKey: @"FilterBar"];
    [[fWindow toolbar] validateVisibleItems];

    [self applyFilter]; //do even if showing to ensure tooltips are updated
}

//doesn't save shown state
- (void) showFilterBar: (BOOL) show animate: (BOOL) animate
{
    const BOOL prevShown = fFilterBar != nil;
    if (show == prevShown)
        return;

    if (show)
    {
        fFilterBar = [[FilterBarController alloc] init];

        NSView * contentView = [fWindow contentView];
        const NSSize windowSize = [contentView convertSize: [fWindow frame].size fromView: nil];

        NSRect filterBarFrame = [[fFilterBar view] frame];
        filterBarFrame.size.width = windowSize.width;
        [[fFilterBar view] setFrame: filterBarFrame];

        if (fStatusBar)
            [contentView addSubview: [fFilterBar view] positioned: NSWindowBelow relativeTo: [fStatusBar view]];
        else
            [contentView addSubview: [fFilterBar view]];
        const CGFloat originY = fStatusBar ? NSMinY([[fStatusBar view] frame]) : NSMaxY([contentView frame]);
        [[fFilterBar view] setFrameOrigin: NSMakePoint(0.0, originY)];
    }
    else
        [fWindow makeFirstResponder: fTableView];

    CGFloat heightChange = NSHeight([[fFilterBar view] frame]);
    if (!show)
        heightChange *= -1;

    //allow bar to show even if not enough room
    if (show && ![fDefaults boolForKey: @"AutoSize"])
    {
        NSRect frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];

        NSScreen * screen = [fWindow screen];
        if (screen)
        {
            CGFloat change = [screen visibleFrame].size.height - frame.size.height;
            if (change < 0.0)
            {
                frame = [fWindow frame];
                frame.size.height += change;
                frame.origin.y -= change;
                [fWindow setFrame: frame display: NO animate: NO];
            }
        }
    }

    NSScrollView * scrollView = [fTableView enclosingScrollView];

    //set views to not autoresize
    const NSUInteger filterMask = [[fFilterBar view] autoresizingMask];
    const NSUInteger scrollMask = [scrollView autoresizingMask];
    [[fFilterBar view] setAutoresizingMask: NSViewNotSizable];
    [scrollView setAutoresizingMask: NSViewNotSizable];

    const NSRect frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];
    [fWindow setFrame: frame display: YES animate: animate];

    //re-enable autoresize
    [[fFilterBar view] setAutoresizingMask: filterMask];
    [scrollView setAutoresizingMask: scrollMask];

    if (!show)
    {
        [[fFilterBar view] removeFromSuperviewWithoutNeedingDisplay];
        [fFilterBar release];
        fFilterBar = nil;
    }

    if ([fDefaults boolForKey: @"AutoSize"])
        [self setWindowMinMaxToCurrent];
    else
    {
        //change min size
        NSSize minSize = [fWindow contentMinSize];
        minSize.height += heightChange;
        [fWindow setContentMinSize: minSize];
    }
}

- (void) focusFilterField
{
    if (!fFilterBar)
        [self toggleFilterBar: self];
    [fFilterBar focusSearchField];
}

- (BOOL) acceptsPreviewPanelControl: (QLPreviewPanel *) panel
{
    return !fQuitting;
}

- (void) beginPreviewPanelControl: (QLPreviewPanel *) panel
{
    fPreviewPanel = [panel retain];
    [fPreviewPanel setDelegate: self];
    [fPreviewPanel setDataSource: self];
}

- (void) endPreviewPanelControl: (QLPreviewPanel *) panel
{
    [fPreviewPanel release];
    fPreviewPanel = nil;
}

- (NSArray *) quickLookableTorrents
{
    NSArray * selectedTorrents = [fTableView selectedTorrents];
    NSMutableArray * qlArray = [NSMutableArray arrayWithCapacity: [selectedTorrents count]];

    for (Torrent * torrent in selectedTorrents)
        if (([torrent isFolder] || [torrent isComplete]) && [torrent dataLocation])
            [qlArray addObject: torrent];

    return qlArray;
}

- (NSInteger) numberOfPreviewItemsInPreviewPanel: (QLPreviewPanel *) panel
{
    if ([fInfoController canQuickLook])
        return [[fInfoController quickLookURLs] count];
    else
        return [[self quickLookableTorrents] count];
}

- (id <QLPreviewItem>) previewPanel: (QLPreviewPanel *) panel previewItemAtIndex: (NSInteger) index
{
    if ([fInfoController canQuickLook])
        return [[fInfoController quickLookURLs] objectAtIndex: index];
    else
        return [[self quickLookableTorrents] objectAtIndex: index];
}

- (BOOL) previewPanel: (QLPreviewPanel *) panel handleEvent: (NSEvent *) event
{
    /*if ([event type] == NSKeyDown)
    {
        [super keyDown: event];
        return YES;
    }*/

    return NO;
}

- (NSRect) previewPanel: (QLPreviewPanel *) panel sourceFrameOnScreenForPreviewItem: (id <QLPreviewItem>) item
{
    if ([fInfoController canQuickLook])
        return [fInfoController quickLookSourceFrameForPreviewItem: item];
    else
    {
        if (![fWindow isVisible])
            return NSZeroRect;

        const NSInteger row = [fTableView rowForItem: item];
        if (row == -1)
            return NSZeroRect;

        NSRect frame = [fTableView iconRectForRow: row];

        if (!NSIntersectsRect([fTableView visibleRect], frame))
            return NSZeroRect;

        frame.origin = [fTableView convertPoint: frame.origin toView: nil];
        frame = [fWindow convertRectToScreen: frame];
        frame.origin.y -= frame.size.height;
        return frame;
    }
}

- (void) showToolbarShare: (id) sender
{
    NSParameterAssert([sender isKindOfClass:[NSButton class]]);

    NSSharingServicePicker * picker = [[NSSharingServicePicker alloc] initWithItems: [[ShareTorrentFileHelper sharedHelper] shareTorrentURLs]];
    picker.delegate = self;

    [picker showRelativeToRect:[sender bounds] ofView:sender preferredEdge:NSMinYEdge];
}

- (id<NSSharingServiceDelegate>)sharingServicePicker:(NSSharingServicePicker *)sharingServicePicker delegateForSharingService:(NSSharingService *)sharingService
{
    return self;
}

- (NSWindow *)sharingService:(NSSharingService *)sharingService sourceWindowForShareItems:(NSArray *)items sharingContentScope:(NSSharingContentScope *)sharingContentScope
{
    return fWindow;
}

- (ButtonToolbarItem *) standardToolbarButtonWithIdentifier: (NSString *) ident
{
    return [self toolbarButtonWithIdentifier: ident forToolbarButtonClass: [ButtonToolbarItem class]];
}

- (id) toolbarButtonWithIdentifier: (NSString *) ident forToolbarButtonClass:(Class)class
{
    ButtonToolbarItem * item = [[class alloc] initWithItemIdentifier: ident];

    NSButton * button = [[NSButton alloc] init];
    [button setBezelStyle: NSTexturedRoundedBezelStyle];
    [button setStringValue: @""];

    [item setView: button];
    [button release];

    const NSSize buttonSize = NSMakeSize(36.0, 25.0);
    [item setMinSize: buttonSize];
    [item setMaxSize: buttonSize];

    return [item autorelease];
}

- (NSToolbarItem *) toolbar: (NSToolbar *) toolbar itemForItemIdentifier: (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    if ([ident isEqualToString: TOOLBAR_CREATE])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];

        [item setLabel: NSLocalizedString(@"Create", "Create toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Create Torrent File", "Create toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Create torrent file", "Create toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarCreateTemplate"]];
        [item setTarget: self];
        [item setAction: @selector(createFile:)];
        [item setAutovalidates: NO];

        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_OPEN_FILE])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];

        [item setLabel: NSLocalizedString(@"Open", "Open toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Open Torrent Files", "Open toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Open torrent files", "Open toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarOpenTemplate"]];
        [item setTarget: self];
        [item setAction: @selector(openShowSheet:)];
        [item setAutovalidates: NO];

        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_OPEN_WEB])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];

        [item setLabel: NSLocalizedString(@"Open Address", "Open address toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Open Torrent Address", "Open address toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Open torrent web address", "Open address toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarOpenWebTemplate"]];
        [item setTarget: self];
        [item setAction: @selector(openURLShowSheet:)];
        [item setAutovalidates: NO];

        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_REMOVE])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];

        [item setLabel: NSLocalizedString(@"Remove", "Remove toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Remove Selected", "Remove toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Remove selected transfers", "Remove toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarRemoveTemplate"]];
        [item setTarget: self];
        [item setAction: @selector(removeNoDelete:)];
        [item setVisibilityPriority: NSToolbarItemVisibilityPriorityHigh];

        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_INFO])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        [[(NSButton *)[item view] cell] setShowsStateBy: NSContentsCellMask]; //blue when enabled

        [item setLabel: NSLocalizedString(@"Inspector", "Inspector toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Toggle Inspector", "Inspector toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Toggle the torrent inspector", "Inspector toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarInfoTemplate"]];
        [item setTarget: self];
        [item setAction: @selector(showInfo:)];

        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_PAUSE_RESUME_ALL])
    {
        GroupToolbarItem * groupItem = [[GroupToolbarItem alloc] initWithItemIdentifier: ident];

        NSSegmentedControl * segmentedControl = [[NSSegmentedControl alloc] initWithFrame: NSZeroRect];
        [segmentedControl setCell: [[[ToolbarSegmentedCell alloc] init] autorelease]];
        [groupItem setView: segmentedControl];
        NSSegmentedCell * segmentedCell = (NSSegmentedCell *)[segmentedControl cell];

        if ([NSApp isOnYosemiteOrBetter]) {
            segmentedControl.segmentStyle = NSSegmentStyleSeparated;
        }

        [segmentedControl setSegmentCount: 2];
        [segmentedCell setTrackingMode: NSSegmentSwitchTrackingMomentary];

        const NSSize groupSize = NSMakeSize(72.0, 25.0);
        [groupItem setMinSize: groupSize];
        [groupItem setMaxSize: groupSize];

        [groupItem setLabel: NSLocalizedString(@"Apply All", "All toolbar item -> label")];
        [groupItem setPaletteLabel: NSLocalizedString(@"Pause / Resume All", "All toolbar item -> palette label")];
        [groupItem setTarget: self];
        [groupItem setAction: @selector(allToolbarClicked:)];

        [groupItem setIdentifiers: [NSArray arrayWithObjects: TOOLBAR_PAUSE_ALL, TOOLBAR_RESUME_ALL, nil]];

        [segmentedCell setTag: TOOLBAR_PAUSE_TAG forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"ToolbarPauseAllTemplate"] forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Pause all transfers",
                                    "All toolbar item -> tooltip") forSegment: TOOLBAR_PAUSE_TAG];

        [segmentedCell setTag: TOOLBAR_RESUME_TAG forSegment: TOOLBAR_RESUME_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"ToolbarResumeAllTemplate"] forSegment: TOOLBAR_RESUME_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Resume all transfers",
                                    "All toolbar item -> tooltip") forSegment: TOOLBAR_RESUME_TAG];

        [groupItem createMenu: [NSArray arrayWithObjects: NSLocalizedString(@"Pause All", "All toolbar item -> label"),
                                        NSLocalizedString(@"Resume All", "All toolbar item -> label"), nil]];

        [segmentedControl release];

        [groupItem setVisibilityPriority: NSToolbarItemVisibilityPriorityHigh];

        return [groupItem autorelease];
    }
    else if ([ident isEqualToString: TOOLBAR_PAUSE_RESUME_SELECTED])
    {
        GroupToolbarItem * groupItem = [[GroupToolbarItem alloc] initWithItemIdentifier: ident];

        NSSegmentedControl * segmentedControl = [[NSSegmentedControl alloc] initWithFrame: NSZeroRect];
        [segmentedControl setCell: [[[ToolbarSegmentedCell alloc] init] autorelease]];
        [groupItem setView: segmentedControl];
        NSSegmentedCell * segmentedCell = (NSSegmentedCell *)[segmentedControl cell];

        if ([NSApp isOnYosemiteOrBetter]) {
            segmentedControl.segmentStyle = NSSegmentStyleSeparated;
        }

        [segmentedControl setSegmentCount: 2];
        [segmentedCell setTrackingMode: NSSegmentSwitchTrackingMomentary];

        const NSSize groupSize = NSMakeSize(72.0, 25.0);
        [groupItem setMinSize: groupSize];
        [groupItem setMaxSize: groupSize];

        [groupItem setLabel: NSLocalizedString(@"Apply Selected", "Selected toolbar item -> label")];
        [groupItem setPaletteLabel: NSLocalizedString(@"Pause / Resume Selected", "Selected toolbar item -> palette label")];
        [groupItem setTarget: self];
        [groupItem setAction: @selector(selectedToolbarClicked:)];

        [groupItem setIdentifiers: [NSArray arrayWithObjects: TOOLBAR_PAUSE_SELECTED, TOOLBAR_RESUME_SELECTED, nil]];

        [segmentedCell setTag: TOOLBAR_PAUSE_TAG forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"ToolbarPauseSelectedTemplate"] forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Pause selected transfers",
                                    "Selected toolbar item -> tooltip") forSegment: TOOLBAR_PAUSE_TAG];

        [segmentedCell setTag: TOOLBAR_RESUME_TAG forSegment: TOOLBAR_RESUME_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"ToolbarResumeSelectedTemplate"] forSegment: TOOLBAR_RESUME_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Resume selected transfers",
                                    "Selected toolbar item -> tooltip") forSegment: TOOLBAR_RESUME_TAG];

        [groupItem createMenu: [NSArray arrayWithObjects: NSLocalizedString(@"Pause Selected", "Selected toolbar item -> label"),
                                        NSLocalizedString(@"Resume Selected", "Selected toolbar item -> label"), nil]];

        [segmentedControl release];

        [groupItem setVisibilityPriority: NSToolbarItemVisibilityPriorityHigh];

        return [groupItem autorelease];
    }
    else if ([ident isEqualToString: TOOLBAR_FILTER])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        [[(NSButton *)[item view] cell] setShowsStateBy: NSContentsCellMask]; //blue when enabled

        [item setLabel: NSLocalizedString(@"Filter", "Filter toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Toggle Filter", "Filter toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Toggle the filter bar", "Filter toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"ToolbarFilterTemplate"]];
        [item setTarget: self];
        [item setAction: @selector(toggleFilterBar:)];

        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_QUICKLOOK])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        [[(NSButton *)[item view] cell] setShowsStateBy: NSContentsCellMask]; //blue when enabled

        [item setLabel: NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: NSImageNameQuickLookTemplate]];
        [item setTarget: self];
        [item setAction: @selector(toggleQuickLook:)];
        [item setVisibilityPriority: NSToolbarItemVisibilityPriorityLow];

        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_SHARE])
    {
        ShareToolbarItem * item = [self toolbarButtonWithIdentifier: ident forToolbarButtonClass: [ShareToolbarItem class]];

        [item setLabel: NSLocalizedString(@"Share", "Share toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Share", "Share toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Share torrent file", "Share toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: NSImageNameShareTemplate]];
        [item setVisibilityPriority: NSToolbarItemVisibilityPriorityLow];

        NSButton *itemButton = (NSButton *)[item view];
        [itemButton setTarget: self];
        [itemButton setAction: @selector(showToolbarShare:)];
        [itemButton sendActionOn:NSLeftMouseDownMask];

        return item;
    }
    else
        return nil;
}

- (void) allToolbarClicked: (id) sender
{
    NSInteger tagValue = [sender isKindOfClass: [NSSegmentedControl class]]
                    ? [(NSSegmentedCell *)[sender cell] tagForSegment: [sender selectedSegment]] : [(NSControl *)sender tag];
    switch (tagValue)
    {
        case TOOLBAR_PAUSE_TAG:
            [self stopAllTorrents: sender];
            break;
        case TOOLBAR_RESUME_TAG:
            [self resumeAllTorrents: sender];
            break;
    }
}

- (void) selectedToolbarClicked: (id) sender
{
    NSInteger tagValue = [sender isKindOfClass: [NSSegmentedControl class]]
                    ? [(NSSegmentedCell *)[sender cell] tagForSegment: [sender selectedSegment]] : [(NSControl *)sender tag];
    switch (tagValue)
    {
        case TOOLBAR_PAUSE_TAG:
            [self stopSelectedTorrents: sender];
            break;
        case TOOLBAR_RESUME_TAG:
            [self resumeSelectedTorrents: sender];
            break;
    }
}

- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar *) toolbar
{
    NSMutableArray *items = [NSMutableArray arrayWithObjects:
                             TOOLBAR_CREATE, TOOLBAR_OPEN_FILE, TOOLBAR_OPEN_WEB, TOOLBAR_REMOVE,
                             TOOLBAR_PAUSE_RESUME_SELECTED, TOOLBAR_PAUSE_RESUME_ALL,
                             TOOLBAR_SHARE, TOOLBAR_QUICKLOOK, TOOLBAR_FILTER, TOOLBAR_INFO,
                             NSToolbarSeparatorItemIdentifier,
                             NSToolbarSpaceItemIdentifier,
                             NSToolbarFlexibleSpaceItemIdentifier,
                             NSToolbarCustomizeToolbarItemIdentifier, nil];
    if (![NSApp isOnMountainLionOrBetter]) {
        [items removeObject:TOOLBAR_SHARE];
    }
    return items;
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) toolbar
{
    NSMutableArray *items = [NSMutableArray arrayWithObjects:
                             TOOLBAR_CREATE, TOOLBAR_OPEN_FILE, TOOLBAR_REMOVE, NSToolbarSpaceItemIdentifier,
                             TOOLBAR_PAUSE_RESUME_ALL, NSToolbarFlexibleSpaceItemIdentifier,
                             TOOLBAR_SHARE, TOOLBAR_QUICKLOOK, TOOLBAR_FILTER, TOOLBAR_INFO, nil];
    if (![NSApp isOnMountainLionOrBetter]) {
        [items removeObject:TOOLBAR_SHARE];
    }
    return items;
}

- (BOOL) validateToolbarItem: (NSToolbarItem *) toolbarItem
{
    NSString * ident = [toolbarItem itemIdentifier];

    //enable remove item
    if ([ident isEqualToString: TOOLBAR_REMOVE])
        return [fTableView numberOfSelectedRows] > 0;

    //enable pause all item
    if ([ident isEqualToString: TOOLBAR_PAUSE_ALL])
    {
        for (Torrent * torrent in fTorrents)
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable resume all item
    if ([ident isEqualToString: TOOLBAR_RESUME_ALL])
    {
        for (Torrent * torrent in fTorrents)
            if (![torrent isActive] && ![torrent waitingToStart] && ![torrent isFinishedSeeding])
                return YES;
        return NO;
    }

    //enable pause item
    if ([ident isEqualToString: TOOLBAR_PAUSE_SELECTED])
    {
        for (Torrent * torrent in [fTableView selectedTorrents])
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable resume item
    if ([ident isEqualToString: TOOLBAR_RESUME_SELECTED])
    {
        for (Torrent * torrent in [fTableView selectedTorrents])
            if (![torrent isActive] && ![torrent waitingToStart])
                return YES;
        return NO;
    }

    //set info item
    if ([ident isEqualToString: TOOLBAR_INFO])
    {
        [(NSButton *)[toolbarItem view] setState: [[fInfoController window] isVisible]];
        return YES;
    }

    //set filter item
    if ([ident isEqualToString: TOOLBAR_FILTER])
    {
        [(NSButton *)[toolbarItem view] setState: fFilterBar != nil];
        return YES;
    }

    //set quick look item
    if ([ident isEqualToString: TOOLBAR_QUICKLOOK])
    {
        [(NSButton *)[toolbarItem view] setState: [QLPreviewPanel sharedPreviewPanelExists] && [[QLPreviewPanel sharedPreviewPanel] isVisible]];
        return YES;
    }

    //enable share item
    if ([ident isEqualToString: TOOLBAR_SHARE])
        return [fTableView numberOfSelectedRows] > 0;

    return YES;
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];

    if (action == @selector(toggleSpeedLimit:))
    {
        [menuItem setState: [fDefaults boolForKey: @"SpeedLimit"] ? NSOnState : NSOffState];
        return YES;
    }

    //only enable some items if it is in a context menu or the window is useable
    BOOL canUseTable = [fWindow isKeyWindow] || [[menuItem menu] supermenu] != [NSApp mainMenu];

    //enable open items
    if (action == @selector(openShowSheet:) || action == @selector(openURLShowSheet:))
        return [fWindow attachedSheet] == nil;

    //enable sort options
    if (action == @selector(setSort:))
    {
        NSString * sortType;
        switch ([menuItem tag])
        {
            case SORT_ORDER_TAG:
                sortType = SORT_ORDER;
                break;
            case SORT_DATE_TAG:
                sortType = SORT_DATE;
                break;
            case SORT_NAME_TAG:
                sortType = SORT_NAME;
                break;
            case SORT_PROGRESS_TAG:
                sortType = SORT_PROGRESS;
                break;
            case SORT_STATE_TAG:
                sortType = SORT_STATE;
                break;
            case SORT_TRACKER_TAG:
                sortType = SORT_TRACKER;
                break;
            case SORT_ACTIVITY_TAG:
                sortType = SORT_ACTIVITY;
                break;
            case SORT_SIZE_TAG:
                sortType = SORT_SIZE;
                break;
            default:
                NSAssert1(NO, @"Unknown sort tag received: %ld", [menuItem tag]);
        }

        [menuItem setState: [sortType isEqualToString: [fDefaults stringForKey: @"Sort"]] ? NSOnState : NSOffState];
        return [fWindow isVisible];
    }

    if (action == @selector(setGroup:))
    {
        BOOL checked = NO;

        NSInteger index = [menuItem tag];
        for (Torrent * torrent in [fTableView selectedTorrents])
            if (index == [torrent groupValue])
            {
                checked = YES;
                break;
            }

        [menuItem setState: checked ? NSOnState : NSOffState];
        return canUseTable && [fTableView numberOfSelectedRows] > 0;
    }

    if (action == @selector(toggleSmallView:))
    {
        [menuItem setState: [fDefaults boolForKey: @"SmallView"] ? NSOnState : NSOffState];
        return [fWindow isVisible];
    }

    if (action == @selector(togglePiecesBar:))
    {
        [menuItem setState: [fDefaults boolForKey: @"PiecesBar"] ? NSOnState : NSOffState];
        return [fWindow isVisible];
    }

    if (action == @selector(toggleAvailabilityBar:))
    {
        [menuItem setState: [fDefaults boolForKey: @"DisplayProgressBarAvailable"] ? NSOnState : NSOffState];
        return [fWindow isVisible];
    }

    //enable show info
    if (action == @selector(showInfo:))
    {
        NSString * title = [[fInfoController window] isVisible] ? NSLocalizedString(@"Hide Inspector", "View menu -> Inspector")
                            : NSLocalizedString(@"Show Inspector", "View menu -> Inspector");
        [menuItem setTitle: title];

        return YES;
    }

    //enable prev/next inspector tab
    if (action == @selector(setInfoTab:))
        return [[fInfoController window] isVisible];

    //enable toggle status bar
    if (action == @selector(toggleStatusBar:))
    {
        NSString * title = !fStatusBar ? NSLocalizedString(@"Show Status Bar", "View menu -> Status Bar")
                            : NSLocalizedString(@"Hide Status Bar", "View menu -> Status Bar");
        [menuItem setTitle: title];

        return [fWindow isVisible];
    }

    //enable toggle filter bar
    if (action == @selector(toggleFilterBar:))
    {
        NSString * title = !fFilterBar ? NSLocalizedString(@"Show Filter Bar", "View menu -> Filter Bar")
                            : NSLocalizedString(@"Hide Filter Bar", "View menu -> Filter Bar");
        [menuItem setTitle: title];

        return [fWindow isVisible];
    }

    //enable prev/next filter button
    if (action == @selector(switchFilter:))
        return [fWindow isVisible] && fFilterBar;

    //enable reveal in finder
    if (action == @selector(revealFile:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;

    //enable renaming file/folder
    if (action == @selector(renameSelected:))
        return canUseTable && [fTableView numberOfSelectedRows] == 1;

    //enable remove items
    if (action == @selector(removeNoDelete:) || action == @selector(removeDeleteData:))
    {
        BOOL warning = NO;

        for (Torrent * torrent in [fTableView selectedTorrents])
        {
            if ([torrent isActive])
            {
                if ([fDefaults boolForKey: @"CheckRemoveDownloading"] ? ![torrent isSeeding] : YES)
                {
                    warning = YES;
                    break;
                }
            }
        }

        //append or remove ellipsis when needed
        NSString * title = [menuItem title], * ellipsis = [NSString ellipsis];
        if (warning && [fDefaults boolForKey: @"CheckRemove"])
        {
            if (![title hasSuffix: ellipsis])
                [menuItem setTitle: [title stringByAppendingEllipsis]];
        }
        else
        {
            if ([title hasSuffix: ellipsis])
                [menuItem setTitle: [title substringToIndex: [title rangeOfString: ellipsis].location]];
        }

        return canUseTable && [fTableView numberOfSelectedRows] > 0;
    }

    //remove all completed transfers item
    if (action == @selector(clearCompleted:))
    {
        //append or remove ellipsis when needed
        NSString * title = [menuItem title], * ellipsis = [NSString ellipsis];
        if ([fDefaults boolForKey: @"WarningRemoveCompleted"])
        {
            if (![title hasSuffix: ellipsis])
                [menuItem setTitle: [title stringByAppendingEllipsis]];
        }
        else
        {
            if ([title hasSuffix: ellipsis])
                [menuItem setTitle: [title substringToIndex: [title rangeOfString: ellipsis].location]];
        }

        for (Torrent * torrent in fTorrents)
            if ([torrent isFinishedSeeding])
                return YES;
        return NO;
    }

    //enable pause all item
    if (action == @selector(stopAllTorrents:))
    {
        for (Torrent * torrent in fTorrents)
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable resume all item
    if (action == @selector(resumeAllTorrents:))
    {
        for (Torrent * torrent in fTorrents)
            if (![torrent isActive] && ![torrent waitingToStart] && ![torrent isFinishedSeeding])
                return YES;
        return NO;
    }

    //enable resume all waiting item
    if (action == @selector(resumeWaitingTorrents:))
    {
        if (![fDefaults boolForKey: @"Queue"] && ![fDefaults boolForKey: @"QueueSeed"])
            return NO;

        for (Torrent * torrent in fTorrents)
            if ([torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable resume selected waiting item
    if (action == @selector(resumeSelectedTorrentsNoWait:))
    {
        if (!canUseTable)
            return NO;

        for (Torrent * torrent in [fTableView selectedTorrents])
            if (![torrent isActive])
                return YES;
        return NO;
    }

    //enable pause item
    if (action == @selector(stopSelectedTorrents:))
    {
        if (!canUseTable)
            return NO;

        for (Torrent * torrent in [fTableView selectedTorrents])
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable resume item
    if (action == @selector(resumeSelectedTorrents:))
    {
        if (!canUseTable)
            return NO;

        for (Torrent * torrent in [fTableView selectedTorrents])
            if (![torrent isActive] && ![torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable manual announce item
    if (action == @selector(announceSelectedTorrents:))
    {
        if (!canUseTable)
            return NO;

        for (Torrent * torrent in [fTableView selectedTorrents])
            if ([torrent canManualAnnounce])
                return YES;
        return NO;
    }

    //enable reset cache item
    if (action == @selector(verifySelectedTorrents:))
    {
        if (!canUseTable)
            return NO;

        for (Torrent * torrent in [fTableView selectedTorrents])
            if (![torrent isMagnet])
                return YES;
        return NO;
    }

    //enable move torrent file item
    if (action == @selector(moveDataFilesSelected:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;

    //enable copy torrent file item
    if (action == @selector(copyTorrentFiles:))
    {
        if (!canUseTable)
            return NO;

        for (Torrent * torrent in [fTableView selectedTorrents])
            if (![torrent isMagnet])
                return YES;
        return NO;
    }

    //enable copy torrent file item
    if (action == @selector(copyMagnetLinks:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;

    //enable reverse sort item
    if (action == @selector(setSortReverse:))
    {
        const BOOL isReverse = [menuItem tag] == SORT_DESC_TAG;
        [menuItem setState: (isReverse == [fDefaults boolForKey: @"SortReverse"]) ? NSOnState : NSOffState];
        return ![[fDefaults stringForKey: @"Sort"] isEqualToString: SORT_ORDER];
    }

    //enable group sort item
    if (action == @selector(setSortByGroup:))
    {
        [menuItem setState: [fDefaults boolForKey: @"SortByGroup"] ? NSOnState : NSOffState];
        return YES;
    }

    if (action == @selector(toggleQuickLook:))
    {
        const BOOL visible =[QLPreviewPanel sharedPreviewPanelExists] && [[QLPreviewPanel sharedPreviewPanel] isVisible];
        //text consistent with Finder
        NSString * title = !visible ? NSLocalizedString(@"Quick Look", "View menu -> Quick Look")
                                    : NSLocalizedString(@"Close Quick Look", "View menu -> Quick Look");
        [menuItem setTitle: title];

        return YES;
    }

    return YES;
}

- (void) sleepCallback: (natural_t) messageType argument: (void *) messageArgument
{
    switch (messageType)
    {
        case kIOMessageSystemWillSleep:
        {
            //stop all transfers (since some are active) before going to sleep and remember to resume when we wake up
            BOOL anyActive = NO;
            for (Torrent * torrent in fTorrents)
            {
                if ([torrent isActive])
                    anyActive = YES;
                [torrent sleep]; //have to call on all, regardless if they are active
            }

            //if there are any running transfers, wait 15 seconds for them to stop
            if (anyActive)
            {
                sleep(15);
            }

            IOAllowPowerChange(fRootPort, (long) messageArgument);
            break;
        }

        case kIOMessageCanSystemSleep:
            if ([fDefaults boolForKey: @"SleepPrevent"])
            {
                //prevent idle sleep unless no torrents are active
                for (Torrent * torrent in fTorrents)
                    if ([torrent isActive] && ![torrent isStalled] && ![torrent isError])
                    {
                        IOCancelPowerChange(fRootPort, (long) messageArgument);
                        return;
                    }
            }

            IOAllowPowerChange(fRootPort, (long) messageArgument);
            break;

        case kIOMessageSystemHasPoweredOn:
            //resume sleeping transfers after we wake up
            for (Torrent * torrent in fTorrents)
                [torrent wakeUp];
            break;
    }
}

- (NSMenu *) applicationDockMenu: (NSApplication *) sender
{
    if (fQuitting)
        return nil;

    NSUInteger seeding = 0, downloading = 0;
    for (Torrent * torrent in fTorrents)
    {
        if ([torrent isSeeding])
            seeding++;
        else if ([torrent isActive])
            downloading++;
        else;
    }

    NSMenu * menu = [[NSMenu alloc] init];

    if (seeding > 0)
    {
        NSString * title = [NSString stringWithFormat: NSLocalizedString(@"%d Seeding", "Dock item - Seeding"), seeding];
        [menu addItemWithTitle: title action: nil keyEquivalent: @""];
    }

    if (downloading > 0)
    {
        NSString * title = [NSString stringWithFormat: NSLocalizedString(@"%d Downloading", "Dock item - Downloading"), downloading];
        [menu addItemWithTitle: title action: nil keyEquivalent: @""];
    }

    if (seeding > 0 || downloading > 0)
        [menu addItem: [NSMenuItem separatorItem]];

    [menu addItemWithTitle: NSLocalizedString(@"Pause All", "Dock item") action: @selector(stopAllTorrents:) keyEquivalent: @""];
    [menu addItemWithTitle: NSLocalizedString(@"Resume All", "Dock item") action: @selector(resumeAllTorrents:) keyEquivalent: @""];
    [menu addItem: [NSMenuItem separatorItem]];
    [menu addItemWithTitle: NSLocalizedString(@"Speed Limit", "Dock item") action: @selector(toggleSpeedLimit:) keyEquivalent: @""];

    return [menu autorelease];
}

- (NSRect) windowWillUseStandardFrame: (NSWindow *) window defaultFrame: (NSRect) defaultFrame
{
    //if auto size is enabled, the current frame shouldn't need to change
    NSRect frame = [fDefaults boolForKey: @"AutoSize"] ? [window frame] : [self sizedWindowFrame];

    frame.size.width = [fDefaults boolForKey: @"SmallView"] ? [fWindow minSize].width : WINDOW_REGULAR_WIDTH;
    return frame;
}

- (void) setWindowSizeToFit
{
    if ([fDefaults boolForKey: @"AutoSize"])
    {
        NSScrollView * scrollView = [fTableView enclosingScrollView];

        [scrollView setHasVerticalScroller: NO];
        [fWindow setFrame: [self sizedWindowFrame] display: YES animate: YES];
        [scrollView setHasVerticalScroller: YES];

        [self setWindowMinMaxToCurrent];
    }
}

- (NSRect) sizedWindowFrame
{
    NSUInteger groups = ([fDisplayedTorrents count] > 0 && ![[fDisplayedTorrents objectAtIndex: 0] isKindOfClass: [Torrent class]])
                        ? [fDisplayedTorrents count] : 0;

    CGFloat heightChange = (GROUP_SEPARATOR_HEIGHT + [fTableView intercellSpacing].height) * groups
                        + ([fTableView rowHeight] + [fTableView intercellSpacing].height) * ([fTableView numberOfRows] - groups)
                        - NSHeight([[fTableView enclosingScrollView] frame]);

    return [self windowFrameByAddingHeight: heightChange checkLimits: YES];
}

- (void) updateForAutoSize
{
    if ([fDefaults boolForKey: @"AutoSize"])
        [self setWindowSizeToFit];
    else
    {
        NSSize contentMinSize = [fWindow contentMinSize];
        contentMinSize.height = [self minWindowContentSizeAllowed];

        [fWindow setContentMinSize: contentMinSize];

        NSSize contentMaxSize = [fWindow contentMaxSize];
        contentMaxSize.height = FLT_MAX;
        [fWindow setContentMaxSize: contentMaxSize];
    }
}

- (void) setWindowMinMaxToCurrent
{
    const CGFloat height = NSHeight([[fWindow contentView] frame]);

    NSSize minSize = [fWindow contentMinSize],
            maxSize = [fWindow contentMaxSize];
    minSize.height = height;
    maxSize.height = height;

    [fWindow setContentMinSize: minSize];
    [fWindow setContentMaxSize: maxSize];
}

- (CGFloat) minWindowContentSizeAllowed
{
    CGFloat contentMinHeight = NSHeight([[fWindow contentView] frame]) - NSHeight([[fTableView enclosingScrollView] frame])
                                + [fTableView rowHeight] + [fTableView intercellSpacing].height;
    return contentMinHeight;
}

- (void) updateForExpandCollape
{
    [self setWindowSizeToFit];
    [self setBottomCountText: YES];
}

- (void) showMainWindow: (id) sender
{
    [fWindow makeKeyAndOrderFront: nil];
}

- (void) windowDidBecomeMain: (NSNotification *) notification
{
    [fBadger clearCompleted];
    [self updateUI];
}

- (void) applicationWillUnhide: (NSNotification *) notification
{
    [self updateUI];
}

- (void) toggleQuickLook: (id) sender
{
    if ([[QLPreviewPanel sharedPreviewPanel] isVisible])
        [[QLPreviewPanel sharedPreviewPanel] orderOut: nil];
    else
        [[QLPreviewPanel sharedPreviewPanel] makeKeyAndOrderFront: nil];
}

- (void) linkHomepage: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: WEBSITE_URL]];
}

- (void) linkForums: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: FORUM_URL]];
}

- (void) linkGitHub: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: GITHUB_URL]];
}

- (void) linkDonate: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: DONATE_URL]];
}

- (void) updaterWillRelaunchApplication: (SUUpdater *) updater
{
    fQuitRequested = YES;
}

- (NSDictionary *) registrationDictionaryForGrowl
{
    NSArray * notifications = @[GROWL_DOWNLOAD_COMPLETE, GROWL_SEEDING_COMPLETE, GROWL_AUTO_ADD, GROWL_AUTO_SPEED_LIMIT];

    return @{GROWL_NOTIFICATIONS_ALL : notifications,
                GROWL_NOTIFICATIONS_DEFAULT : notifications };
}

- (void) growlNotificationWasClicked: (id) clickContext
{
    if (![clickContext isKindOfClass: [NSDictionary class]])
        return;

    NSString * type = [clickContext objectForKey: @"Type"], * location;
    if (([type isEqualToString: GROWL_DOWNLOAD_COMPLETE] || [type isEqualToString: GROWL_SEEDING_COMPLETE])
            && (location = [clickContext objectForKey: @"Location"]))
    {
        NSURL * file = [NSURL fileURLWithPath: location];
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs: [NSArray arrayWithObject: file]];
    }
}

- (void) rpcCallback: (tr_rpc_callback_type) type forTorrentStruct: (struct tr_torrent *) torrentStruct
{
    @autoreleasepool
    {
        //get the torrent
        __block Torrent * torrent = nil;
        if (torrentStruct != NULL && (type != TR_RPC_TORRENT_ADDED && type != TR_RPC_SESSION_CHANGED && type != TR_RPC_SESSION_CLOSE))
        {
            [fTorrents enumerateObjectsWithOptions: NSEnumerationConcurrent usingBlock: ^(Torrent * checkTorrent, NSUInteger idx, BOOL *stop) {
                if (torrentStruct == [checkTorrent torrentStruct])
                {
                    torrent = checkTorrent;
                    *stop = YES;
                }
            }];

            if (!torrent)
            {
                NSLog(@"No torrent found matching the given torrent struct from the RPC callback!");
                return;
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            switch (type)
            {
                case TR_RPC_TORRENT_ADDED:
                    [self rpcAddTorrentStruct: torrentStruct];
                    break;

                case TR_RPC_TORRENT_STARTED:
                case TR_RPC_TORRENT_STOPPED:
                    [self rpcStartedStoppedTorrent: torrent];
                    break;

                case TR_RPC_TORRENT_REMOVING:
                    [self rpcRemoveTorrent: torrent deleteData: NO];
                    break;

                case TR_RPC_TORRENT_TRASHING:
                    [self rpcRemoveTorrent: torrent deleteData: YES];
                    break;

                case TR_RPC_TORRENT_CHANGED:
                    [self rpcChangedTorrent: torrent];
                    break;

                case TR_RPC_TORRENT_MOVED:
                    [self rpcMovedTorrent: torrent];
                    break;

                case TR_RPC_SESSION_QUEUE_POSITIONS_CHANGED:
                    [self rpcUpdateQueue];
                    break;

                case TR_RPC_SESSION_CHANGED:
                    [fPrefsController rpcUpdatePrefs];
                    break;

                case TR_RPC_SESSION_CLOSE:
                    fQuitRequested = YES;
                    [NSApp terminate: self];
                    break;

                default:
                    NSAssert1(NO, @"Unknown RPC command received: %d", type);
            }
        });
    }
}

- (void) rpcAddTorrentStruct: (struct tr_torrent *) torrentStruct
{
    NSString * location = nil;
    if (tr_torrentGetDownloadDir(torrentStruct) != NULL)
        location = [NSString stringWithUTF8String: tr_torrentGetDownloadDir(torrentStruct)];

    Torrent * torrent = [[Torrent alloc] initWithTorrentStruct: torrentStruct location: location lib: fLib];

    //change the location if the group calls for it (this has to wait until after the torrent is created)
    if ([[GroupsController groups] usesCustomDownloadLocationForIndex: [torrent groupValue]])
    {
        location = [[GroupsController groups] customDownloadLocationForIndex: [torrent groupValue]];
        [torrent changeDownloadFolderBeforeUsing: location determinationType: TorrentDeterminationAutomatic];
    }

    [torrent update];
    [fTorrents addObject: torrent];
    [torrent release];

    if (!fAddingTransfers)
        fAddingTransfers = [[NSMutableSet alloc] init];
    [fAddingTransfers addObject: torrent];

    [self fullUpdateUI];
}

- (void) rpcRemoveTorrent: (Torrent *) torrent deleteData: (BOOL) deleteData
{
    [self confirmRemoveTorrents: @[ torrent ] deleteData: deleteData];
}

- (void) rpcStartedStoppedTorrent: (Torrent *) torrent
{
    [torrent update];

    [self updateUI];
    [self applyFilter];
    [self updateTorrentHistory];
}

- (void) rpcChangedTorrent: (Torrent *) torrent
{
    [torrent update];

    if ([[fTableView selectedTorrents] containsObject: torrent])
    {
        [fInfoController updateInfoStats]; //this will reload the file table
        [fInfoController updateOptions];
    }
}

- (void) rpcMovedTorrent: (Torrent *) torrent
{
    [torrent update];
    [torrent updateTimeMachineExclude];

    if ([[fTableView selectedTorrents] containsObject: torrent])
        [fInfoController updateInfoStats];
}

- (void) rpcUpdateQueue
{
    for (Torrent * torrent in fTorrents)
        [torrent update];

    NSSortDescriptor * descriptor = [NSSortDescriptor sortDescriptorWithKey: @"queuePosition" ascending: YES];
    NSArray * descriptors = [NSArray arrayWithObject: descriptor];
    [fTorrents sortUsingDescriptors: descriptors];

    [self sortTorrents: YES];
}

@end
