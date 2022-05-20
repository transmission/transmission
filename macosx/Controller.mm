// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <IOKit/IOMessage.h>
#import <IOKit/pwr_mgt/IOPMLib.h>
#import <Carbon/Carbon.h>

#import <Sparkle/Sparkle.h>

#include <atomic> /* atomic, atomic_fetch_add_explicit, memory_order_relaxed */

#include <libtransmission/transmission.h>

#include <libtransmission/log.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

#import "VDKQueue.h"

#import "CocoaCompatibility.h"

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
#import "NSImageAdditions.h"
#import "NSMutableArrayAdditions.h"
#import "NSStringAdditions.h"
#import "ExpandedPathToPathTransformer.h"
#import "ExpandedPathToIconTransformer.h"

#define TOOLBAR_CREATE @"Toolbar Create"
#define TOOLBAR_OPEN_FILE @"Toolbar Open"
#define TOOLBAR_OPEN_WEB @"Toolbar Open Web"
#define TOOLBAR_REMOVE @"Toolbar Remove"
#define TOOLBAR_INFO @"Toolbar Info"
#define TOOLBAR_PAUSE_ALL @"Toolbar Pause All"
#define TOOLBAR_RESUME_ALL @"Toolbar Resume All"
#define TOOLBAR_PAUSE_RESUME_ALL @"Toolbar Pause / Resume All"
#define TOOLBAR_PAUSE_SELECTED @"Toolbar Pause Selected"
#define TOOLBAR_RESUME_SELECTED @"Toolbar Resume Selected"
#define TOOLBAR_PAUSE_RESUME_SELECTED @"Toolbar Pause / Resume Selected"
#define TOOLBAR_FILTER @"Toolbar Toggle Filter"
#define TOOLBAR_QUICKLOOK @"Toolbar QuickLook"
#define TOOLBAR_SHARE @"Toolbar Share"

typedef NS_ENUM(unsigned int, toolbarGroupTag) { //
    TOOLBAR_PAUSE_TAG = 0,
    TOOLBAR_RESUME_TAG = 1
};

#define SORT_DATE @"Date"
#define SORT_NAME @"Name"
#define SORT_STATE @"State"
#define SORT_PROGRESS @"Progress"
#define SORT_TRACKER @"Tracker"
#define SORT_ORDER @"Order"
#define SORT_ACTIVITY @"Activity"
#define SORT_SIZE @"Size"

typedef NS_ENUM(unsigned int, sortTag) {
    SORT_ORDER_TAG = 0,
    SORT_DATE_TAG = 1,
    SORT_NAME_TAG = 2,
    SORT_PROGRESS_TAG = 3,
    SORT_STATE_TAG = 4,
    SORT_TRACKER_TAG = 5,
    SORT_ACTIVITY_TAG = 6,
    SORT_SIZE_TAG = 7
};

typedef NS_ENUM(unsigned int, sortOrderTag) { //
    SORT_ASC_TAG = 0,
    SORT_DESC_TAG = 1
};

#define TORRENT_TABLE_VIEW_DATA_TYPE @"TorrentTableViewDataType"

#define ROW_HEIGHT_REGULAR 62.0
#define ROW_HEIGHT_SMALL 22.0
#define WINDOW_REGULAR_WIDTH 468.0

#define STATUS_BAR_HEIGHT 21.0
#define FILTER_BAR_HEIGHT 23.0

#define UPDATE_UI_SECONDS 1.0

#define TRANSFER_PLIST @"Transfers.plist"

#define WEBSITE_URL @"https://transmissionbt.com/"
#define FORUM_URL @"https://forum.transmissionbt.com/"
#define GITHUB_URL @"https://github.com/transmission/transmission"
#define DONATE_URL @"https://transmissionbt.com/donate/"

#define DONATE_NAG_TIME (60 * 60 * 24 * 7)

static void altSpeedToggledCallback([[maybe_unused]] tr_session* handle, bool active, bool byUser, void* controller)
{
    NSDictionary* dict = [[NSDictionary alloc] initWithObjects:@[ @(active), @(byUser) ] forKeys:@[ @"Active", @"ByUser" ]];
    [(__bridge Controller*)controller performSelectorOnMainThread:@selector(altSpeedToggledCallbackIsLimited:) withObject:dict
                                                    waitUntilDone:NO];
}

static tr_rpc_callback_status rpcCallback([[maybe_unused]] tr_session* handle, tr_rpc_callback_type type, struct tr_torrent* torrentStruct, void* controller)
{
    [(__bridge Controller*)controller rpcCallback:type forTorrentStruct:torrentStruct];
    return TR_RPC_NOREMOVE; //we'll do the remove manually
}

static void sleepCallback(void* controller, io_service_t y, natural_t messageType, void* messageArgument)
{
    [(__bridge Controller*)controller sleepCallback:messageType argument:messageArgument];
}

// 2.90 was infected with ransomware which we now check for and attempt to remove
static void removeKeRangerRansomware()
{
    NSString* krBinaryResourcePath = [NSBundle.mainBundle pathForResource:@"General" ofType:@"rtf"];

    NSString* userLibraryDirPath = [NSHomeDirectory() stringByAppendingString:@"/Library"];
    NSString* krLibraryKernelServicePath = [userLibraryDirPath stringByAppendingString:@"/kernel_service"];

    NSFileManager* fileManager = NSFileManager.defaultManager;

    NSArray<NSString*>* krFilePaths = @[
        krBinaryResourcePath ? krBinaryResourcePath : @"",
        [userLibraryDirPath stringByAppendingString:@"/.kernel_pid"],
        [userLibraryDirPath stringByAppendingString:@"/.kernel_time"],
        [userLibraryDirPath stringByAppendingString:@"/.kernel_complete"],
        krLibraryKernelServicePath
    ];

    BOOL foundKrFiles = NO;
    for (NSString* krFilePath in krFilePaths)
    {
        if (krFilePath.length == 0 || ![fileManager fileExistsAtPath:krFilePath])
        {
            continue;
        }

        foundKrFiles = YES;
        break;
    }

    if (!foundKrFiles)
    {
        return;
    }

    NSLog(@"Detected OSX.KeRanger.A ransomware, trying to remove it");

    if ([fileManager fileExistsAtPath:krLibraryKernelServicePath])
    {
        // The forgiving way: kill process which has the file opened
        NSTask* lsofTask = [[NSTask alloc] init];
        lsofTask.launchPath = @"/usr/sbin/lsof";
        lsofTask.arguments = @[ @"-F", @"pid", @"--", krLibraryKernelServicePath ];
        lsofTask.standardOutput = [NSPipe pipe];
        lsofTask.standardInput = [NSPipe pipe];
        lsofTask.standardError = lsofTask.standardOutput;
        [lsofTask launch];
        NSData* lsofOutputData = [[lsofTask.standardOutput fileHandleForReading] readDataToEndOfFile];
        [lsofTask waitUntilExit];
        NSString* lsofOutput = [[NSString alloc] initWithData:lsofOutputData encoding:NSUTF8StringEncoding];
        for (NSString* line in [lsofOutput componentsSeparatedByString:@"\n"])
        {
            if (![line hasPrefix:@"p"])
            {
                continue;
            }
            pid_t const krProcessId = [line substringFromIndex:1].intValue;
            if (kill(krProcessId, SIGKILL) == -1)
            {
                NSLog(@"Unable to forcibly terminate ransomware process (kernel_service, pid %d), please do so manually", (int)krProcessId);
            }
        }
    }
    else
    {
        // The harsh way: kill all processes with matching name
        NSTask* killTask = [NSTask launchedTaskWithLaunchPath:@"/usr/bin/killall" arguments:@[ @"-9", @"kernel_service" ]];
        [killTask waitUntilExit];
        if (killTask.terminationStatus != 0)
        {
            NSLog(@"Unable to forcibly terminate ransomware process (kernel_service), please do so manually if it's currently running");
        }
    }

    for (NSString* krFilePath in krFilePaths)
    {
        if (krFilePath.length == 0 || ![fileManager fileExistsAtPath:krFilePath])
        {
            continue;
        }

        if (![fileManager removeItemAtPath:krFilePath error:NULL])
        {
            NSLog(@"Unable to remove ransomware file at %@, please do so manually", krFilePath);
        }
    }

    NSLog(@"OSX.KeRanger.A ransomware removal completed, proceeding to normal operation");
}

@interface Controller ()

@property(nonatomic) IBOutlet NSWindow* fWindow;
@property(nonatomic) IBOutlet TorrentTableView* fTableView;

@property(nonatomic) IBOutlet NSMenuItem* fOpenIgnoreDownloadFolder;
@property(nonatomic) IBOutlet NSButton* fActionButton;
@property(nonatomic) IBOutlet NSButton* fSpeedLimitButton;
@property(nonatomic) IBOutlet NSButton* fClearCompletedButton;
@property(nonatomic) IBOutlet NSTextField* fTotalTorrentsField;
@property(nonatomic) IBOutlet NSMenuItem* fNextFilterItem;

@property(nonatomic) IBOutlet NSMenuItem* fNextInfoTabItem;
@property(nonatomic) IBOutlet NSMenuItem* fPrevInfoTabItem;

@property(nonatomic) IBOutlet NSMenu* fSortMenu;

@property(nonatomic) IBOutlet NSMenu* fGroupsSetMenu;
@property(nonatomic) IBOutlet NSMenu* fGroupsSetContextMenu;

@property(nonatomic) IBOutlet NSMenu* fShareMenu;
@property(nonatomic) IBOutlet NSMenu* fShareContextMenu;
@property(nonatomic) IBOutlet NSMenuItem* fShareMenuItem; // remove when dropping 10.6
@property(nonatomic) IBOutlet NSMenuItem* fShareContextMenuItem; // remove when dropping 10.6

@property(nonatomic, readonly) tr_session* fLib;

@property(nonatomic, readonly) NSMutableArray<Torrent*>* fTorrents;
@property(nonatomic, readonly) NSMutableArray* fDisplayedTorrents;
@property(nonatomic, readonly) NSMutableDictionary<NSString*, Torrent*>* fTorrentHashes;

@property(nonatomic, readonly) InfoWindowController* fInfoController;
@property(nonatomic) MessageWindowController* fMessageController;

@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic, readonly) NSString* fConfigDirectory;

@property(nonatomic) DragOverlayWindow* fOverlayWindow;

@property(nonatomic) io_connect_t fRootPort;
@property(nonatomic) NSTimer* fTimer;

@property(nonatomic) StatusBarController* fStatusBar;

@property(nonatomic) FilterBarController* fFilterBar;

@property(nonatomic) QLPreviewPanel* fPreviewPanel;
@property(nonatomic) BOOL fQuitting;
@property(nonatomic) BOOL fQuitRequested;
@property(nonatomic, readonly) BOOL fPauseOnLaunch;

@property(nonatomic) Badger* fBadger;

@property(nonatomic) NSMutableArray<NSString*>* fAutoImportedNames;
@property(nonatomic) NSTimer* fAutoImportTimer;

@property(nonatomic) NSMutableDictionary<NSURL*, id>* fPendingTorrentDownloads;

@property(nonatomic) NSMutableSet<Torrent*>* fAddingTransfers;

@property(nonatomic) NSMutableSet<NSWindowController*>* fAddWindows;
@property(nonatomic) URLSheetWindowController* fUrlSheetController;

@property(nonatomic) BOOL fGlobalPopoverShown;
@property(nonatomic) BOOL fSoundPlaying;
@property(nonatomic) id fNoNapActivity;

@end

@implementation Controller

+ (void)initialize
{
    removeKeRangerRansomware();

    //make sure another Transmission.app isn't running already
    NSArray* apps = [NSRunningApplication runningApplicationsWithBundleIdentifier:NSBundle.mainBundle.bundleIdentifier];
    if (apps.count > 1)
    {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle:NSLocalizedString(@"OK", "Transmission already running alert -> button")];
        alert.messageText = NSLocalizedString(@"Transmission is already running.", "Transmission already running alert -> title");
        alert.informativeText = NSLocalizedString(
            @"There is already a copy of Transmission running. "
             "This copy cannot be opened until that instance is quit.",
            "Transmission already running alert -> message");
        alert.alertStyle = NSAlertStyleCritical;

        [alert runModal];

        //kill ourselves right away
        exit(0);
    }

    [NSUserDefaults.standardUserDefaults
        registerDefaults:[NSDictionary dictionaryWithContentsOfFile:[NSBundle.mainBundle pathForResource:@"Defaults" ofType:@"plist"]]];

    //set custom value transformers
    ExpandedPathToPathTransformer* pathTransformer = [[ExpandedPathToPathTransformer alloc] init];
    [NSValueTransformer setValueTransformer:pathTransformer forName:@"ExpandedPathToPathTransformer"];

    ExpandedPathToIconTransformer* iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
    [NSValueTransformer setValueTransformer:iconTransformer forName:@"ExpandedPathToIconTransformer"];

    //cover our asses
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"WarningLegal"])
    {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle:NSLocalizedString(@"I Accept", "Legal alert -> button")];
        [alert addButtonWithTitle:NSLocalizedString(@"Quit", "Legal alert -> button")];
        alert.messageText = NSLocalizedString(@"Welcome to Transmission", "Legal alert -> title");
        alert.informativeText = NSLocalizedString(
            @"Transmission is a file-sharing program."
             " When you run a torrent, its data will be made available to others by means of upload."
             " You and you alone are fully responsible for exercising proper judgement and abiding by your local laws.",
            "Legal alert -> message");
        alert.alertStyle = NSAlertStyleInformational;

        if ([alert runModal] == NSAlertSecondButtonReturn)
        {
            exit(0);
        }

        [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningLegal"];
    }
}

- (instancetype)init
{
    if ((self = [super init]))
    {
        _fDefaults = NSUserDefaults.standardUserDefaults;

        //checks for old version speeds of -1
        if ([_fDefaults integerForKey:@"UploadLimit"] < 0)
        {
            [_fDefaults removeObjectForKey:@"UploadLimit"];
            [_fDefaults setBool:NO forKey:@"CheckUpload"];
        }
        if ([_fDefaults integerForKey:@"DownloadLimit"] < 0)
        {
            [_fDefaults removeObjectForKey:@"DownloadLimit"];
            [_fDefaults setBool:NO forKey:@"CheckDownload"];
        }

        //upgrading from versions < 2.40: clear recent items
        [NSDocumentController.sharedDocumentController clearRecentDocuments:nil];

        tr_variant settings;
        tr_variantInitDict(&settings, 41);
        tr_sessionGetDefaultSettings(&settings);

        BOOL const usesSpeedLimitSched = [_fDefaults boolForKey:@"SpeedLimitAuto"];
        if (!usesSpeedLimitSched)
        {
            tr_variantDictAddBool(&settings, TR_KEY_alt_speed_enabled, [_fDefaults boolForKey:@"SpeedLimit"]);
        }

        tr_variantDictAddInt(&settings, TR_KEY_alt_speed_up, [_fDefaults integerForKey:@"SpeedLimitUploadLimit"]);
        tr_variantDictAddInt(&settings, TR_KEY_alt_speed_down, [_fDefaults integerForKey:@"SpeedLimitDownloadLimit"]);

        tr_variantDictAddBool(&settings, TR_KEY_alt_speed_time_enabled, [_fDefaults boolForKey:@"SpeedLimitAuto"]);
        tr_variantDictAddInt(&settings, TR_KEY_alt_speed_time_begin, [PrefsController dateToTimeSum:[_fDefaults objectForKey:@"SpeedLimitAutoOnDate"]]);
        tr_variantDictAddInt(&settings, TR_KEY_alt_speed_time_end, [PrefsController dateToTimeSum:[_fDefaults objectForKey:@"SpeedLimitAutoOffDate"]]);
        tr_variantDictAddInt(&settings, TR_KEY_alt_speed_time_day, [_fDefaults integerForKey:@"SpeedLimitAutoDay"]);

        tr_variantDictAddInt(&settings, TR_KEY_speed_limit_down, [_fDefaults integerForKey:@"DownloadLimit"]);
        tr_variantDictAddBool(&settings, TR_KEY_speed_limit_down_enabled, [_fDefaults boolForKey:@"CheckDownload"]);
        tr_variantDictAddInt(&settings, TR_KEY_speed_limit_up, [_fDefaults integerForKey:@"UploadLimit"]);
        tr_variantDictAddBool(&settings, TR_KEY_speed_limit_up_enabled, [_fDefaults boolForKey:@"CheckUpload"]);

        //hidden prefs
        if ([_fDefaults objectForKey:@"BindAddressIPv4"])
        {
            tr_variantDictAddStr(&settings, TR_KEY_bind_address_ipv4, [_fDefaults stringForKey:@"BindAddressIPv4"].UTF8String);
        }
        if ([_fDefaults objectForKey:@"BindAddressIPv6"])
        {
            tr_variantDictAddStr(&settings, TR_KEY_bind_address_ipv6, [_fDefaults stringForKey:@"BindAddressIPv6"].UTF8String);
        }

        tr_variantDictAddBool(&settings, TR_KEY_blocklist_enabled, [_fDefaults boolForKey:@"BlocklistNew"]);
        if ([_fDefaults objectForKey:@"BlocklistURL"])
            tr_variantDictAddStr(&settings, TR_KEY_blocklist_url, [_fDefaults stringForKey:@"BlocklistURL"].UTF8String);
        tr_variantDictAddBool(&settings, TR_KEY_dht_enabled, [_fDefaults boolForKey:@"DHTGlobal"]);
        tr_variantDictAddStr(
            &settings,
            TR_KEY_download_dir,
            [_fDefaults stringForKey:@"DownloadFolder"].stringByExpandingTildeInPath.UTF8String);
        tr_variantDictAddBool(&settings, TR_KEY_download_queue_enabled, [_fDefaults boolForKey:@"Queue"]);
        tr_variantDictAddInt(&settings, TR_KEY_download_queue_size, [_fDefaults integerForKey:@"QueueDownloadNumber"]);
        tr_variantDictAddInt(&settings, TR_KEY_idle_seeding_limit, [_fDefaults integerForKey:@"IdleLimitMinutes"]);
        tr_variantDictAddBool(&settings, TR_KEY_idle_seeding_limit_enabled, [_fDefaults boolForKey:@"IdleLimitCheck"]);
        tr_variantDictAddStr(
            &settings,
            TR_KEY_incomplete_dir,
            [_fDefaults stringForKey:@"IncompleteDownloadFolder"].stringByExpandingTildeInPath.UTF8String);
        tr_variantDictAddBool(&settings, TR_KEY_incomplete_dir_enabled, [_fDefaults boolForKey:@"UseIncompleteDownloadFolder"]);
        tr_variantDictAddBool(&settings, TR_KEY_lpd_enabled, [_fDefaults boolForKey:@"LocalPeerDiscoveryGlobal"]);
        tr_variantDictAddInt(&settings, TR_KEY_message_level, TR_LOG_DEBUG);
        tr_variantDictAddInt(&settings, TR_KEY_peer_limit_global, [_fDefaults integerForKey:@"PeersTotal"]);
        tr_variantDictAddInt(&settings, TR_KEY_peer_limit_per_torrent, [_fDefaults integerForKey:@"PeersTorrent"]);

        BOOL const randomPort = [_fDefaults boolForKey:@"RandomPort"];
        tr_variantDictAddBool(&settings, TR_KEY_peer_port_random_on_start, randomPort);
        if (!randomPort)
        {
            tr_variantDictAddInt(&settings, TR_KEY_peer_port, [_fDefaults integerForKey:@"BindPort"]);
        }

        //hidden pref
        if ([_fDefaults objectForKey:@"PeerSocketTOS"])
        {
            tr_variantDictAddStr(&settings, TR_KEY_peer_socket_tos, [_fDefaults stringForKey:@"PeerSocketTOS"].UTF8String);
        }

        tr_variantDictAddBool(&settings, TR_KEY_pex_enabled, [_fDefaults boolForKey:@"PEXGlobal"]);
        tr_variantDictAddBool(&settings, TR_KEY_port_forwarding_enabled, [_fDefaults boolForKey:@"NatTraversal"]);
        tr_variantDictAddBool(&settings, TR_KEY_queue_stalled_enabled, [_fDefaults boolForKey:@"CheckStalled"]);
        tr_variantDictAddInt(&settings, TR_KEY_queue_stalled_minutes, [_fDefaults integerForKey:@"StalledMinutes"]);
        tr_variantDictAddReal(&settings, TR_KEY_ratio_limit, [_fDefaults floatForKey:@"RatioLimit"]);
        tr_variantDictAddBool(&settings, TR_KEY_ratio_limit_enabled, [_fDefaults boolForKey:@"RatioCheck"]);
        tr_variantDictAddBool(&settings, TR_KEY_rename_partial_files, [_fDefaults boolForKey:@"RenamePartialFiles"]);
        tr_variantDictAddBool(&settings, TR_KEY_rpc_authentication_required, [_fDefaults boolForKey:@"RPCAuthorize"]);
        tr_variantDictAddBool(&settings, TR_KEY_rpc_enabled, [_fDefaults boolForKey:@"RPC"]);
        tr_variantDictAddInt(&settings, TR_KEY_rpc_port, [_fDefaults integerForKey:@"RPCPort"]);
        tr_variantDictAddStr(&settings, TR_KEY_rpc_username, [_fDefaults stringForKey:@"RPCUsername"].UTF8String);
        tr_variantDictAddBool(&settings, TR_KEY_rpc_whitelist_enabled, [_fDefaults boolForKey:@"RPCUseWhitelist"]);
        tr_variantDictAddBool(&settings, TR_KEY_rpc_host_whitelist_enabled, [_fDefaults boolForKey:@"RPCUseHostWhitelist"]);
        tr_variantDictAddBool(&settings, TR_KEY_seed_queue_enabled, [_fDefaults boolForKey:@"QueueSeed"]);
        tr_variantDictAddInt(&settings, TR_KEY_seed_queue_size, [_fDefaults integerForKey:@"QueueSeedNumber"]);
        tr_variantDictAddBool(&settings, TR_KEY_start_added_torrents, [_fDefaults boolForKey:@"AutoStartDownload"]);
        tr_variantDictAddBool(&settings, TR_KEY_utp_enabled, [_fDefaults boolForKey:@"UTPGlobal"]);

        tr_variantDictAddBool(&settings, TR_KEY_script_torrent_done_enabled, [_fDefaults boolForKey:@"DoneScriptEnabled"]);
        NSString* prefs_string = [_fDefaults stringForKey:@"DoneScriptPath"];
        if (prefs_string != nil)
        {
            tr_variantDictAddStr(&settings, TR_KEY_script_torrent_done_filename, prefs_string.UTF8String);
        }

        // TODO: Add to GUI
        if ([_fDefaults objectForKey:@"RPCHostWhitelist"])
        {
            tr_variantDictAddStr(&settings, TR_KEY_rpc_host_whitelist, [_fDefaults stringForKey:@"RPCHostWhitelist"].UTF8String);
        }

        NSByteCountFormatter* unitFormatter = [[NSByteCountFormatter alloc] init];
        unitFormatter.includesCount = NO;
        unitFormatter.allowsNonnumericFormatting = NO;

        unitFormatter.allowedUnits = NSByteCountFormatterUseKB;
        // use a random value to avoid possible pluralization issues with 1 or 0 (an example is if we use 1 for bytes,
        // we'd get "byte" when we'd want "bytes" for the generic libtransmission value at least)
        NSString* kbString = [unitFormatter stringFromByteCount:17];

        unitFormatter.allowedUnits = NSByteCountFormatterUseMB;
        NSString* mbString = [unitFormatter stringFromByteCount:17];

        unitFormatter.allowedUnits = NSByteCountFormatterUseGB;
        NSString* gbString = [unitFormatter stringFromByteCount:17];

        unitFormatter.allowedUnits = NSByteCountFormatterUseTB;
        NSString* tbString = [unitFormatter stringFromByteCount:17];

        tr_formatter_size_init(1000, kbString.UTF8String, mbString.UTF8String, gbString.UTF8String, tbString.UTF8String);

        tr_formatter_speed_init(
            1000,
            NSLocalizedString(@"KB/s", "Transfer speed (kilobytes per second)").UTF8String,
            NSLocalizedString(@"MB/s", "Transfer speed (megabytes per second)").UTF8String,
            NSLocalizedString(@"GB/s", "Transfer speed (gigabytes per second)").UTF8String,
            NSLocalizedString(@"TB/s", "Transfer speed (terabytes per second)").UTF8String); //why not?

        tr_formatter_mem_init(1000, kbString.UTF8String, mbString.UTF8String, gbString.UTF8String, tbString.UTF8String);

        char const* configDir = tr_getDefaultConfigDir("Transmission");
        _fLib = tr_sessionInit(configDir, YES, &settings);
        tr_variantFree(&settings);

        _fConfigDirectory = [[NSString alloc] initWithUTF8String:configDir];

        NSApp.delegate = self;

        //register for magnet URLs (has to be in init)
        [[NSAppleEventManager sharedAppleEventManager] setEventHandler:self
                                                           andSelector:@selector(handleOpenContentsEvent:replyEvent:)
                                                         forEventClass:kInternetEventClass
                                                            andEventID:kAEGetURL];

        _fTorrents = [[NSMutableArray alloc] init];
        _fDisplayedTorrents = [[NSMutableArray alloc] init];
        _fTorrentHashes = [[NSMutableDictionary alloc] init];

        _fInfoController = [[InfoWindowController alloc] init];

        //needs to be done before init-ing the prefs controller
        _fileWatcherQueue = [[VDKQueue alloc] init];
        _fileWatcherQueue.delegate = self;

        _prefsController = [[PrefsController alloc] initWithHandle:_fLib];

        _fQuitting = NO;
        _fGlobalPopoverShown = NO;
        _fSoundPlaying = NO;

        tr_sessionSetAltSpeedFunc(_fLib, altSpeedToggledCallback, (__bridge void*)(self));
        if (usesSpeedLimitSched)
        {
            [_fDefaults setBool:tr_sessionUsesAltSpeed(_fLib) forKey:@"SpeedLimit"];
        }

        tr_sessionSetRPCCallback(_fLib, rpcCallback, (__bridge void*)(self));

        [SUUpdater sharedUpdater].delegate = self;
        _fQuitRequested = NO;

        _fPauseOnLaunch = (GetCurrentKeyModifiers() & (optionKey | rightOptionKey)) != 0;
    }
    return self;
}

- (void)awakeFromNib
{
    NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:@"TRMainToolbar"];
    toolbar.delegate = self;
    toolbar.allowsUserCustomization = YES;
    toolbar.autosavesConfiguration = YES;
    toolbar.displayMode = NSToolbarDisplayModeIconOnly;
    self.fWindow.toolbar = toolbar;

    self.fWindow.delegate = self; //do manually to avoid placement issue

    [self.fWindow makeFirstResponder:self.fTableView];
    self.fWindow.excludedFromWindowsMenu = YES;

    //set table size
    BOOL const small = [self.fDefaults boolForKey:@"SmallView"];
    if (small)
    {
        self.fTableView.rowHeight = ROW_HEIGHT_SMALL;
    }
    self.fTableView.usesAlternatingRowBackgroundColors = !small;

    [self.fWindow setContentBorderThickness:NSMinY(self.fTableView.enclosingScrollView.frame) forEdge:NSMinYEdge];
    self.fWindow.movableByWindowBackground = YES;

    self.fTotalTorrentsField.cell.backgroundStyle = NSBackgroundStyleRaised;

    //set up filter bar
    [self showFilterBar:[self.fDefaults boolForKey:@"FilterBar"] animate:NO];

    //set up status bar
    [self showStatusBar:[self.fDefaults boolForKey:@"StatusBar"] animate:NO];

    self.fActionButton.toolTip = NSLocalizedString(@"Shortcuts for changing global settings.", "Main window -> 1st bottom left button (action) tooltip");
    self.fSpeedLimitButton.toolTip = NSLocalizedString(
        @"Speed Limit overrides the total bandwidth limits with its own limits.",
        "Main window -> 2nd bottom left button (turtle) tooltip");

    if (@available(macOS 11.0, *))
    {
        self.fActionButton.image = [NSImage imageWithSystemSymbolName:@"gearshape.fill" accessibilityDescription:nil];
        self.fSpeedLimitButton.image = [NSImage imageWithSystemSymbolName:@"tortoise.fill" accessibilityDescription:nil];
    }
    self.fClearCompletedButton.toolTip = NSLocalizedString(
        @"Remove all transfers that have completed seeding.",
        "Main window -> 3rd bottom left button (remove all) tooltip");

    [self.fTableView registerForDraggedTypes:@[ TORRENT_TABLE_VIEW_DATA_TYPE ]];
    [self.fWindow registerForDraggedTypes:@[ NSFilenamesPboardType, NSURLPboardType ]];

    //sort the sort menu items (localization is from strings file)
    NSMutableArray* sortMenuItems = [NSMutableArray arrayWithCapacity:7];
    NSUInteger sortMenuIndex = 0;
    BOOL foundSortItem = NO;
    for (NSMenuItem* item in self.fSortMenu.itemArray)
    {
        //assume all sort items are together and the Queue Order item is first
        if (item.action == @selector(setSort:) && item.tag != SORT_ORDER_TAG)
        {
            [sortMenuItems addObject:item];
            [self.fSortMenu removeItemAtIndex:sortMenuIndex];
            foundSortItem = YES;
        }
        else
        {
            if (foundSortItem)
            {
                break;
            }
            ++sortMenuIndex;
        }
    }

    [sortMenuItems sortUsingDescriptors:@[ [NSSortDescriptor sortDescriptorWithKey:@"title" ascending:YES
                                                                          selector:@selector(localizedCompare:)] ]];

    for (NSMenuItem* item in sortMenuItems)
    {
        [self.fSortMenu insertItem:item atIndex:sortMenuIndex++];
    }

    //you would think this would be called later in this method from updateUI, but it's not reached in awakeFromNib
    //this must be called after showStatusBar:
    [self.fStatusBar updateWithDownload:0.0 upload:0.0];

    //this should also be after the rest of the setup
    [self updateForAutoSize];

    //register for sleep notifications
    IONotificationPortRef notify;
    io_object_t iterator;
    if ((self.fRootPort = IORegisterForSystemPower((__bridge void*)(self), &notify, sleepCallback, &iterator)))
    {
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notify), kCFRunLoopCommonModes);
    }
    else
    {
        NSLog(@"Could not IORegisterForSystemPower");
    }

    auto* const session = self.fLib;

    //load previous transfers
    tr_ctor* ctor = tr_ctorNew(session);
    tr_ctorSetPaused(ctor, TR_FORCE, true); // paused by default; unpause below after checking state history
    int n_torrents = 0;
    tr_torrent** loaded_torrents = tr_sessionLoadTorrents(session, ctor, &n_torrents);
    tr_ctorFree(ctor);

    // process the loaded torrents
    for (int i = 0; i < n_torrents; ++i)
    {
        struct tr_torrent* tor = loaded_torrents[i];
        NSString* location;
        if (tr_torrentGetDownloadDir(tor) != NULL)
        {
            location = @(tr_torrentGetDownloadDir(tor));
        }
        Torrent* torrent = [[Torrent alloc] initWithTorrentStruct:tor location:location lib:self.fLib];
        [self.fTorrents addObject:torrent];
        self.fTorrentHashes[torrent.hashString] = torrent;
    }

    //update previous transfers state by recreating a torrent from history
    //and comparing to torrents already loaded via tr_sessionLoadTorrents
    NSString* historyFile = [self.fConfigDirectory stringByAppendingPathComponent:TRANSFER_PLIST];
    NSArray* history = [NSArray arrayWithContentsOfFile:historyFile];
    if (!history)
    {
        //old version saved transfer info in prefs file
        if ((history = [self.fDefaults arrayForKey:@"History"]))
        {
            [self.fDefaults removeObjectForKey:@"History"];
        }
    }

    if (history)
    {
        // theoretical max without doing a lot of work
        NSMutableArray* waitToStartTorrents = [NSMutableArray
            arrayWithCapacity:((history.count > 0 && !self.fPauseOnLaunch) ? history.count - 1 : 0)];

        Torrent* t = [[Torrent alloc] init];
        for (NSDictionary* historyItem in history)
        {
            NSString* hash = historyItem[@"TorrentHash"];
            if ([self.fTorrentHashes.allKeys containsObject:hash])
            {
                Torrent* torrent = self.fTorrentHashes[hash];
                [t setResumeStatusForTorrent:torrent withHistory:historyItem forcePause:self.fPauseOnLaunch];

                NSNumber* waitToStart;
                if (!self.fPauseOnLaunch && (waitToStart = historyItem[@"WaitToStart"]) && waitToStart.boolValue)
                {
                    [waitToStartTorrents addObject:torrent];
                }
            }
        }

        //now that all are loaded, let's set those in the queue to waiting
        for (Torrent* torrent in waitToStartTorrents)
        {
            [torrent startTransfer];
        }
    }

    self.fBadger = [[Badger alloc] initWithLib:self.fLib];

    NSUserNotificationCenter.defaultUserNotificationCenter.delegate = self;

    //observe notifications
    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;

    [nc addObserver:self selector:@selector(updateUI) name:@"UpdateUI" object:nil];

    [nc addObserver:self selector:@selector(torrentFinishedDownloading:) name:@"TorrentFinishedDownloading" object:nil];

    [nc addObserver:self selector:@selector(torrentRestartedDownloading:) name:@"TorrentRestartedDownloading" object:nil];

    [nc addObserver:self selector:@selector(torrentFinishedSeeding:) name:@"TorrentFinishedSeeding" object:nil];

    [nc addObserver:self selector:@selector(applyFilter) name:kTorrentDidChangeGroupNotification object:nil];

    //avoids need of setting delegate
    [nc addObserver:self selector:@selector(torrentTableViewSelectionDidChange:)
               name:NSOutlineViewSelectionDidChangeNotification
             object:self.fTableView];

    [nc addObserver:self selector:@selector(changeAutoImport) name:@"AutoImportSettingChange" object:nil];

    [nc addObserver:self selector:@selector(updateForAutoSize) name:@"AutoSizeSettingChange" object:nil];

    [nc addObserver:self selector:@selector(updateForExpandCollapse) name:@"OutlineExpandCollapse" object:nil];

    [nc addObserver:self.fWindow selector:@selector(makeKeyWindow) name:@"MakeWindowKey" object:nil];

#warning rename
    [nc addObserver:self selector:@selector(fullUpdateUI) name:@"UpdateQueue" object:nil];

    [nc addObserver:self selector:@selector(applyFilter) name:@"ApplyFilter" object:nil];

    //open newly created torrent file
    [nc addObserver:self selector:@selector(beginCreateFile:) name:@"BeginCreateTorrentFile" object:nil];

    //open newly created torrent file
    [nc addObserver:self selector:@selector(openCreatedFile:) name:@"OpenCreatedTorrentFile" object:nil];

    [nc addObserver:self selector:@selector(applyFilter) name:@"UpdateGroups" object:nil];

    //timer to update the interface every second
    [self updateUI];
    self.fTimer = [NSTimer scheduledTimerWithTimeInterval:UPDATE_UI_SECONDS target:self selector:@selector(updateUI)
                                                 userInfo:nil
                                                  repeats:YES];
    [NSRunLoop.currentRunLoop addTimer:self.fTimer forMode:NSModalPanelRunLoopMode];
    [NSRunLoop.currentRunLoop addTimer:self.fTimer forMode:NSEventTrackingRunLoopMode];

    [self applyFilter];

    [self.fWindow makeKeyAndOrderFront:nil];

    if ([self.fDefaults boolForKey:@"InfoVisible"])
    {
        [self showInfo:nil];
    }
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    NSApp.servicesProvider = self;

    self.fNoNapActivity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                                                                       reason:NSLocalizedString(@"No napping on the job!", nil)];

    //register for dock icon drags (has to be in applicationDidFinishLaunching: to work)
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler:self andSelector:@selector(handleOpenContentsEvent:replyEvent:)
                                                     forEventClass:kCoreEventClass
                                                        andEventID:kAEOpenContents];

    //if we were opened from a user notification, do the corresponding action
    NSUserNotification* launchNotification = notification.userInfo[NSApplicationLaunchUserNotificationKey];
    if (launchNotification)
    {
        [self userNotificationCenter:nil didActivateNotification:launchNotification];
    }

    //auto importing
    [self checkAutoImportDirectory];

    //registering the Web UI to Bonjour
    if ([self.fDefaults boolForKey:@"RPC"] && [self.fDefaults boolForKey:@"RPCWebDiscovery"])
    {
        [BonjourController.defaultController startWithPort:[self.fDefaults integerForKey:@"RPCPort"]];
    }

    //shamelessly ask for donations
    if ([self.fDefaults boolForKey:@"WarningDonate"])
    {
        tr_session_stats stats;
        tr_sessionGetCumulativeStats(self.fLib, &stats);
        BOOL const firstLaunch = stats.sessionCount <= 1;

        NSDate* lastDonateDate = [self.fDefaults objectForKey:@"DonateAskDate"];
        BOOL const timePassed = !lastDonateDate || (-1 * lastDonateDate.timeIntervalSinceNow) >= DONATE_NAG_TIME;

        if (!firstLaunch && timePassed)
        {
            [self.fDefaults setObject:[NSDate date] forKey:@"DonateAskDate"];

            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = NSLocalizedString(@"Support open-source indie software", "Donation beg -> title");

            NSString* donateMessage = [NSString
                stringWithFormat:@"%@\n\n%@",
                                 NSLocalizedString(
                                     @"Transmission is a full-featured torrent application."
                                      " A lot of time and effort have gone into development, coding, and refinement."
                                      " If you enjoy using it, please consider showing your love with a donation.",
                                     "Donation beg -> message"),
                                 NSLocalizedString(@"Donate or not, there will be no difference to your torrenting experience.", "Donation beg -> message")];

            alert.informativeText = donateMessage;
            alert.alertStyle = NSAlertStyleInformational;

            [alert addButtonWithTitle:[NSLocalizedString(@"Donate", "Donation beg -> button") stringByAppendingEllipsis]];
            NSButton* noDonateButton = [alert addButtonWithTitle:NSLocalizedString(@"Nope", "Donation beg -> button")];
            noDonateButton.keyEquivalent = @"\e"; //escape key

            // hide the "don't show again" check the first time - give them time to try the app
            BOOL const allowNeverAgain = lastDonateDate != nil;
            alert.showsSuppressionButton = allowNeverAgain;
            if (allowNeverAgain)
            {
                alert.suppressionButton.title = NSLocalizedString(@"Don't bug me about this ever again.", "Donation beg -> button");
            }

            NSInteger const donateResult = [alert runModal];
            if (donateResult == NSAlertFirstButtonReturn)
            {
                [self linkDonate:self];
            }

            if (allowNeverAgain)
            {
                [self.fDefaults setBool:(alert.suppressionButton.state != NSControlStateValueOn) forKey:@"WarningDonate"];
            }
        }
    }
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)app hasVisibleWindows:(BOOL)visibleWindows
{
    NSWindow* mainWindow = NSApp.mainWindow;
    if (!mainWindow || !mainWindow.visible)
    {
        [self.fWindow makeKeyAndOrderFront:nil];
    }

    return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    if (!self.fQuitRequested && [self.fDefaults boolForKey:@"CheckQuit"])
    {
        NSUInteger active = 0, downloading = 0;
        for (Torrent* torrent in self.fTorrents)
        {
            if (torrent.active && !torrent.stalled)
            {
                active++;
                if (!torrent.allDownloaded)
                {
                    downloading++;
                }
            }
        }

        if ([self.fDefaults boolForKey:@"CheckQuitDownloading"] ? downloading > 0 : active > 0)
        {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.alertStyle = NSAlertStyleInformational;
            alert.messageText = NSLocalizedString(@"Are you sure you want to quit?", "Confirm Quit panel -> title");
            alert.informativeText = active == 1 ?
                NSLocalizedString(
                    @"There is an active transfer that will be paused on quit."
                     " The transfer will automatically resume on the next launch.",
                    "Confirm Quit panel -> message") :
                [NSString stringWithFormat:NSLocalizedString(
                                               @"There are %lu active transfers that will be paused on quit."
                                                " The transfers will automatically resume on the next launch.",
                                               "Confirm Quit panel -> message"),
                                           active];
            [alert addButtonWithTitle:NSLocalizedString(@"Quit", "Confirm Quit panel -> button")];
            [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Confirm Quit panel -> button")];

            [alert beginSheetModalForWindow:self.fWindow completionHandler:^(NSModalResponse returnCode) {
                [NSApp replyToApplicationShouldTerminate:returnCode == NSAlertFirstButtonReturn];
            }];
            return NSTerminateLater;
        }
    }

    return NSTerminateNow;
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    self.fQuitting = YES;

    [NSProcessInfo.processInfo endActivity:self.fNoNapActivity];

    //stop the Bonjour service
    if (BonjourController.defaultControllerExists)
    {
        [BonjourController.defaultController stop];
    }

    //stop blocklist download
    if (BlocklistDownloader.isRunning)
    {
        [[BlocklistDownloader downloader] cancelDownload];
    }

    //stop timers and notification checking
    [NSNotificationCenter.defaultCenter removeObserver:self];

    [self.fTimer invalidate];

    if (self.fAutoImportTimer)
    {
        if (self.fAutoImportTimer.valid)
        {
            [self.fAutoImportTimer invalidate];
        }
    }

    //remove all torrent downloads
    if (self.fPendingTorrentDownloads)
    {
        for (NSDictionary* downloadDict in self.fPendingTorrentDownloads)
        {
            NSURLDownload* download = downloadDict[@"Download"];
            [download cancel];
        }
    }

    //remember window states and close all windows
    [self.fDefaults setBool:self.fInfoController.window.visible forKey:@"InfoVisible"];

    if ([QLPreviewPanel sharedPreviewPanelExists] && [QLPreviewPanel sharedPreviewPanel].visible)
    {
        [[QLPreviewPanel sharedPreviewPanel] updateController];
    }

    for (NSWindow* window in NSApp.windows)
    {
        [window close];
    }

    [self showStatusBar:NO animate:NO];
    [self showFilterBar:NO animate:NO];

    //save history
    [self updateTorrentHistory];
    [self.fTableView saveCollapsedGroups];

    _fileWatcherQueue = nil;

    //complete cleanup
    tr_sessionClose(self.fLib);
}

- (tr_session*)sessionHandle
{
    return self.fLib;
}

- (void)handleOpenContentsEvent:(NSAppleEventDescriptor*)event replyEvent:(NSAppleEventDescriptor*)replyEvent
{
    NSString* urlString = nil;

    NSAppleEventDescriptor* directObject = [event paramDescriptorForKeyword:keyDirectObject];
    if (directObject.descriptorType == typeAEList)
    {
        for (NSInteger i = 1; i <= directObject.numberOfItems; i++)
        {
            if ((urlString = [directObject descriptorAtIndex:i].stringValue))
            {
                break;
            }
        }
    }
    else
    {
        urlString = directObject.stringValue;
    }

    if (urlString)
    {
        [self openURL:urlString];
    }
}

- (void)download:(NSURLDownload*)download decideDestinationWithSuggestedFilename:(NSString*)suggestedName
{
    if ([suggestedName.pathExtension caseInsensitiveCompare:@"torrent"] != NSOrderedSame)
    {
        [download cancel];

        [self.fPendingTorrentDownloads removeObjectForKey:download.request.URL];
        if (self.fPendingTorrentDownloads.count == 0)
        {
            self.fPendingTorrentDownloads = nil;
        }

        NSString* message = [NSString
            stringWithFormat:NSLocalizedString(@"It appears that the file \"%@\" from %@ is not a torrent file.", "Download not a torrent -> message"),
                             suggestedName,
                             [download.request.URL.absoluteString stringByRemovingPercentEncoding]];

        NSAlert* alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle:NSLocalizedString(@"OK", "Download not a torrent -> button")];
        alert.messageText = NSLocalizedString(@"Torrent download failed", "Download not a torrent -> title");
        alert.informativeText = message;
        [alert runModal];
    }
    else
    {
        [download setDestination:[NSTemporaryDirectory() stringByAppendingPathComponent:suggestedName.lastPathComponent]
                  allowOverwrite:NO];
    }
}

- (void)download:(NSURLDownload*)download didCreateDestination:(NSString*)path
{
    NSMutableDictionary* dict = self.fPendingTorrentDownloads[download.request.URL];
    dict[@"Path"] = path;
}

- (void)download:(NSURLDownload*)download didFailWithError:(NSError*)error
{
    NSString* message = [NSString
        stringWithFormat:NSLocalizedString(@"The torrent could not be downloaded from %@: %@.", "Torrent download failed -> message"),
                         [download.request.URL.absoluteString stringByRemovingPercentEncoding],
                         error.localizedDescription];

    NSAlert* alert = [[NSAlert alloc] init];
    [alert addButtonWithTitle:NSLocalizedString(@"OK", "Torrent download failed -> button")];
    alert.messageText = NSLocalizedString(@"Torrent download failed", "Torrent download error -> title");
    alert.informativeText = message;
    [alert runModal];

    [self.fPendingTorrentDownloads removeObjectForKey:download.request.URL];
    if (self.fPendingTorrentDownloads.count == 0)
    {
        self.fPendingTorrentDownloads = nil;
    }
}

- (void)downloadDidFinish:(NSURLDownload*)download
{
    NSString* path = self.fPendingTorrentDownloads[download.request.URL][@"Path"];

    [self openFiles:@[ path ] addType:ADD_URL forcePath:nil];

    //delete the torrent file after opening
    [NSFileManager.defaultManager removeItemAtPath:path error:NULL];

    [self.fPendingTorrentDownloads removeObjectForKey:download.request.URL];
    if (self.fPendingTorrentDownloads.count == 0)
    {
        self.fPendingTorrentDownloads = nil;
    }
}

- (void)application:(NSApplication*)app openFiles:(NSArray*)filenames
{
    [self openFiles:filenames addType:ADD_MANUAL forcePath:nil];
}

- (void)openFiles:(NSArray*)filenames addType:(addType)type forcePath:(NSString*)path
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
        deleteTorrentFile = [self.fDefaults boolForKey:@"DeleteOriginalTorrent"];
        canToggleDelete = YES;
    }

    for (NSString* torrentPath in filenames)
    {
        auto metainfo = tr_torrent_metainfo{};
        if (!metainfo.parseTorrentFile(torrentPath.UTF8String)) // invalid torrent
        {
            if (type != ADD_AUTO)
            {
                [self invalidOpenAlert:torrentPath.lastPathComponent];
            }
            continue;
        }

        if (tr_torrentFindFromMetainfo(self.fLib, &metainfo) != nullptr) // dupe torrent
        {
            [self duplicateOpenAlert:@(metainfo.name().c_str())];
            continue;
        }

        //determine download location
        NSString* location;
        BOOL lockDestination = NO; //don't override the location with a group location if it has a hardcoded path
        if (path)
        {
            location = path.stringByExpandingTildeInPath;
            lockDestination = YES;
        }
        else if ([self.fDefaults boolForKey:@"DownloadLocationConstant"])
        {
            location = [self.fDefaults stringForKey:@"DownloadFolder"].stringByExpandingTildeInPath;
        }
        else if (type != ADD_URL)
        {
            location = torrentPath.stringByDeletingLastPathComponent;
        }
        else
        {
            location = nil;
        }

        //determine to show the options window
        auto const is_multifile = metainfo.fileCount() > 1;
        BOOL const showWindow = type == ADD_SHOW_OPTIONS ||
            ([self.fDefaults boolForKey:@"DownloadAsk"] && (is_multifile || ![self.fDefaults boolForKey:@"DownloadAskMulti"]) &&
             (type != ADD_AUTO || ![self.fDefaults boolForKey:@"DownloadAskManual"]));

        Torrent* torrent;
        if (!(torrent = [[Torrent alloc] initWithPath:torrentPath location:location
                                    deleteTorrentFile:showWindow ? NO : deleteTorrentFile
                                                  lib:self.fLib]))
        {
            continue;
        }

        //change the location if the group calls for it (this has to wait until after the torrent is created)
        if (!lockDestination && [GroupsController.groups usesCustomDownloadLocationForIndex:torrent.groupValue])
        {
            location = [GroupsController.groups customDownloadLocationForIndex:torrent.groupValue];
            [torrent changeDownloadFolderBeforeUsing:location determinationType:TorrentDeterminationAutomatic];
        }

        //verify the data right away if it was newly created
        if (type == ADD_CREATED)
        {
            [torrent resetCache];
        }

        //show the add window or add directly
        if (showWindow || !location)
        {
            AddWindowController* addController = [[AddWindowController alloc] initWithTorrent:torrent destination:location
                                                                              lockDestination:lockDestination
                                                                                   controller:self
                                                                                  torrentFile:torrentPath
                                                            deleteTorrentCheckEnableInitially:deleteTorrentFile
                                                                              canToggleDelete:canToggleDelete];
            [addController showWindow:self];

            if (!self.fAddWindows)
            {
                self.fAddWindows = [[NSMutableSet alloc] init];
            }
            [self.fAddWindows addObject:addController];
        }
        else
        {
            if ([self.fDefaults boolForKey:@"AutoStartDownload"])
            {
                [torrent startTransfer];
            }

            [torrent update];
            [self.fTorrents addObject:torrent];

            if (!self.fAddingTransfers)
            {
                self.fAddingTransfers = [[NSMutableSet alloc] init];
            }
            [self.fAddingTransfers addObject:torrent];
        }
    }

    [self fullUpdateUI];
}

- (void)askOpenConfirmed:(AddWindowController*)addController add:(BOOL)add
{
    Torrent* torrent = addController.torrent;

    if (add)
    {
        torrent.queuePosition = self.fTorrents.count;

        [torrent update];
        [self.fTorrents addObject:torrent];

        if (!self.fAddingTransfers)
        {
            self.fAddingTransfers = [[NSMutableSet alloc] init];
        }
        [self.fAddingTransfers addObject:torrent];

        [self fullUpdateUI];
    }
    else
    {
        [torrent closeRemoveTorrent:NO];
    }

    [self.fAddWindows removeObject:addController];
    if (self.fAddWindows.count == 0)
    {
        self.fAddWindows = nil;
    }
}

- (void)openMagnet:(NSString*)address
{
    tr_torrent* duplicateTorrent;
    if ((duplicateTorrent = tr_torrentFindFromMagnetLink(self.fLib, address.UTF8String)))
    {
        NSString* name = @(tr_torrentName(duplicateTorrent));
        [self duplicateOpenMagnetAlert:address transferName:name];
        return;
    }

    //determine download location
    NSString* location = nil;
    if ([self.fDefaults boolForKey:@"DownloadLocationConstant"])
    {
        location = [self.fDefaults stringForKey:@"DownloadFolder"].stringByExpandingTildeInPath;
    }

    Torrent* torrent;
    if (!(torrent = [[Torrent alloc] initWithMagnetAddress:address location:location lib:self.fLib]))
    {
        [self invalidOpenMagnetAlert:address];
        return;
    }

    //change the location if the group calls for it (this has to wait until after the torrent is created)
    if ([GroupsController.groups usesCustomDownloadLocationForIndex:torrent.groupValue])
    {
        location = [GroupsController.groups customDownloadLocationForIndex:torrent.groupValue];
        [torrent changeDownloadFolderBeforeUsing:location determinationType:TorrentDeterminationAutomatic];
    }

    if ([self.fDefaults boolForKey:@"MagnetOpenAsk"] || !location)
    {
        AddMagnetWindowController* addController = [[AddMagnetWindowController alloc] initWithTorrent:torrent destination:location
                                                                                           controller:self];
        [addController showWindow:self];

        if (!self.fAddWindows)
        {
            self.fAddWindows = [[NSMutableSet alloc] init];
        }
        [self.fAddWindows addObject:addController];
    }
    else
    {
        if ([self.fDefaults boolForKey:@"AutoStartDownload"])
        {
            [torrent startTransfer];
        }

        [torrent update];
        [self.fTorrents addObject:torrent];

        if (!self.fAddingTransfers)
        {
            self.fAddingTransfers = [[NSMutableSet alloc] init];
        }
        [self.fAddingTransfers addObject:torrent];
    }

    [self fullUpdateUI];
}

- (void)askOpenMagnetConfirmed:(AddMagnetWindowController*)addController add:(BOOL)add
{
    Torrent* torrent = addController.torrent;

    if (add)
    {
        torrent.queuePosition = self.fTorrents.count;

        [torrent update];
        [self.fTorrents addObject:torrent];

        if (!self.fAddingTransfers)
        {
            self.fAddingTransfers = [[NSMutableSet alloc] init];
        }
        [self.fAddingTransfers addObject:torrent];

        [self fullUpdateUI];
    }
    else
    {
        [torrent closeRemoveTorrent:NO];
    }

    [self.fAddWindows removeObject:addController];
    if (self.fAddWindows.count == 0)
    {
        self.fAddWindows = nil;
    }
}

- (void)openCreatedFile:(NSNotification*)notification
{
    NSDictionary* dict = notification.userInfo;
    [self openFiles:@[ dict[@"File"] ] addType:ADD_CREATED forcePath:dict[@"Path"]];
}

- (void)openFilesWithDict:(NSDictionary*)dictionary
{
    [self openFiles:dictionary[@"Filenames"] addType:static_cast<addType>([dictionary[@"AddType"] intValue]) forcePath:nil];
}

//called on by applescript
- (void)open:(NSArray*)files
{
    NSDictionary* dict = [[NSDictionary alloc] initWithObjects:@[ files, @(ADD_MANUAL) ] forKeys:@[ @"Filenames", @"AddType" ]];
    [self performSelectorOnMainThread:@selector(openFilesWithDict:) withObject:dict waitUntilDone:NO];
}

- (void)openShowSheet:(id)sender
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];

    panel.allowsMultipleSelection = YES;
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;

    panel.allowedFileTypes = @[ @"org.bittorrent.torrent", @"torrent" ];

    [panel beginSheetModalForWindow:self.fWindow completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK)
        {
            NSMutableArray* filenames = [NSMutableArray arrayWithCapacity:panel.URLs.count];
            for (NSURL* url in panel.URLs)
            {
                [filenames addObject:url.path];
            }

            NSDictionary* dictionary = [[NSDictionary alloc]
                initWithObjects:@[ filenames, sender == self.fOpenIgnoreDownloadFolder ? @(ADD_SHOW_OPTIONS) : @(ADD_MANUAL) ]
                        forKeys:@[ @"Filenames", @"AddType" ]];
            [self performSelectorOnMainThread:@selector(openFilesWithDict:) withObject:dictionary waitUntilDone:NO];
        }
    }];
}

- (void)invalidOpenAlert:(NSString*)filename
{
    if (![self.fDefaults boolForKey:@"WarningInvalidOpen"])
    {
        return;
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = [NSString
        stringWithFormat:NSLocalizedString(@"\"%@\" is not a valid torrent file.", "Open invalid alert -> title"), filename];
    alert.informativeText = NSLocalizedString(@"The torrent file cannot be opened because it contains invalid data.", "Open invalid alert -> message");

    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:NSLocalizedString(@"OK", "Open invalid alert -> button")];

    [alert runModal];
    if (alert.suppressionButton.state == NSControlStateValueOn)
    {
        [self.fDefaults setBool:NO forKey:@"WarningInvalidOpen"];
    }
}

- (void)invalidOpenMagnetAlert:(NSString*)address
{
    if (![self.fDefaults boolForKey:@"WarningInvalidOpen"])
    {
        return;
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = NSLocalizedString(@"Adding magnetized transfer failed.", "Magnet link failed -> title");
    alert.informativeText = [NSString stringWithFormat:NSLocalizedString(
                                                           @"There was an error when adding the magnet link \"%@\"."
                                                            " The transfer will not occur.",
                                                           "Magnet link failed -> message"),
                                                       address];
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:NSLocalizedString(@"OK", "Magnet link failed -> button")];

    [alert runModal];
    if (alert.suppressionButton.state == NSControlStateValueOn)
    {
        [self.fDefaults setBool:NO forKey:@"WarningInvalidOpen"];
    }
}

- (void)duplicateOpenAlert:(NSString*)name
{
    if (![self.fDefaults boolForKey:@"WarningDuplicate"])
    {
        return;
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = [NSString
        stringWithFormat:NSLocalizedString(@"A transfer of \"%@\" already exists.", "Open duplicate alert -> title"), name];
    alert.informativeText = NSLocalizedString(
        @"The transfer cannot be added because it is a duplicate of an already existing transfer.",
        "Open duplicate alert -> message");

    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:NSLocalizedString(@"OK", "Open duplicate alert -> button")];
    alert.showsSuppressionButton = YES;

    [alert runModal];
    if (alert.suppressionButton.state)
    {
        [self.fDefaults setBool:NO forKey:@"WarningDuplicate"];
    }
}

- (void)duplicateOpenMagnetAlert:(NSString*)address transferName:(NSString*)name
{
    if (![self.fDefaults boolForKey:@"WarningDuplicate"])
    {
        return;
    }

    NSAlert* alert = [[NSAlert alloc] init];
    if (name)
    {
        alert.messageText = [NSString
            stringWithFormat:NSLocalizedString(@"A transfer of \"%@\" already exists.", "Open duplicate magnet alert -> title"), name];
    }
    else
    {
        alert.messageText = NSLocalizedString(@"Magnet link is a duplicate of an existing transfer.", "Open duplicate magnet alert -> title");
    }
    alert.informativeText = [NSString
        stringWithFormat:NSLocalizedString(
                             @"The magnet link  \"%@\" cannot be added because it is a duplicate of an already existing transfer.",
                             "Open duplicate magnet alert -> message"),
                         address];
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:NSLocalizedString(@"OK", "Open duplicate magnet alert -> button")];
    alert.showsSuppressionButton = YES;

    [alert runModal];
    if (alert.suppressionButton.state)
    {
        [self.fDefaults setBool:NO forKey:@"WarningDuplicate"];
    }
}

- (void)openURL:(NSString*)urlString
{
    if ([urlString rangeOfString:@"magnet:" options:(NSAnchoredSearch | NSCaseInsensitiveSearch)].location != NSNotFound)
    {
        [self openMagnet:urlString];
    }
    else
    {
        if ([urlString rangeOfString:@"://"].location == NSNotFound)
        {
            if ([urlString rangeOfString:@"."].location == NSNotFound)
            {
                NSInteger beforeCom;
                if ((beforeCom = [urlString rangeOfString:@"/"].location) != NSNotFound)
                {
                    urlString = [NSString stringWithFormat:@"http://www.%@.com/%@",
                                                           [urlString substringToIndex:beforeCom],
                                                           [urlString substringFromIndex:beforeCom + 1]];
                }
                else
                {
                    urlString = [NSString stringWithFormat:@"http://www.%@.com/", urlString];
                }
            }
            else
            {
                urlString = [@"http://" stringByAppendingString:urlString];
            }
        }

        NSURL* url = [NSURL URLWithString:urlString];
        if (url == nil)
        {
            NSLog(@"Detected non-URL string \"%@\". Ignoring.", urlString);
            return;
        }

        NSURLRequest* request = [NSURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalAndRemoteCacheData
                                             timeoutInterval:60];

        if (self.fPendingTorrentDownloads[request.URL])
        {
            NSLog(@"Already downloading %@", request.URL);
            return;
        }

        NSURLDownload* download = [[NSURLDownload alloc] initWithRequest:request delegate:self];

        if (!self.fPendingTorrentDownloads)
        {
            self.fPendingTorrentDownloads = [[NSMutableDictionary alloc] init];
        }
        NSMutableDictionary* dict = [NSMutableDictionary dictionaryWithObject:download forKey:@"Download"];
        self.fPendingTorrentDownloads[request.URL] = dict;
    }
}

- (void)openURLShowSheet:(id)sender
{
    if (!self.fUrlSheetController)
    {
        self.fUrlSheetController = [[URLSheetWindowController alloc] init];

        [self.fWindow beginSheet:self.fUrlSheetController.window completionHandler:^(NSModalResponse returnCode) {
            if (returnCode == 1)
            {
                NSString* urlString = [self.fUrlSheetController urlString];
                urlString = [urlString stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self openURL:urlString];
                });
            }
            self.fUrlSheetController = nil;
        }];
    }
}

- (void)createFile:(id)sender
{
    [CreatorWindowController createTorrentFile:self.fLib];
}

- (void)resumeSelectedTorrents:(id)sender
{
    [self resumeTorrents:self.fTableView.selectedTorrents];
}

- (void)resumeAllTorrents:(id)sender
{
    NSMutableArray* torrents = [NSMutableArray arrayWithCapacity:self.fTorrents.count];

    for (Torrent* torrent in self.fTorrents)
    {
        if (!torrent.finishedSeeding)
        {
            [torrents addObject:torrent];
        }
    }

    [self resumeTorrents:torrents];
}

- (void)resumeTorrents:(NSArray*)torrents
{
    for (Torrent* torrent in torrents)
    {
        [torrent startTransfer];
    }

    [self fullUpdateUI];
}

- (void)resumeSelectedTorrentsNoWait:(id)sender
{
    [self resumeTorrentsNoWait:self.fTableView.selectedTorrents];
}

- (void)resumeWaitingTorrents:(id)sender
{
    NSMutableArray* torrents = [NSMutableArray arrayWithCapacity:self.fTorrents.count];

    for (Torrent* torrent in self.fTorrents)
    {
        if (torrent.waitingToStart)
        {
            [torrents addObject:torrent];
        }
    }

    [self resumeTorrentsNoWait:torrents];
}

- (void)resumeTorrentsNoWait:(NSArray<Torrent*>*)torrents
{
    //iterate through instead of all at once to ensure no conflicts
    for (Torrent* torrent in torrents)
    {
        [torrent startTransferNoQueue];
    }

    [self fullUpdateUI];
}

- (void)stopSelectedTorrents:(id)sender
{
    [self stopTorrents:self.fTableView.selectedTorrents];
}

- (void)stopAllTorrents:(id)sender
{
    [self stopTorrents:self.fTorrents];
}

- (void)stopTorrents:(NSArray<Torrent*>*)torrents
{
    //don't want any of these starting then stopping
    for (Torrent* torrent in torrents)
    {
        if (torrent.waitingToStart)
        {
            [torrent stopTransfer];
        }
    }

    for (Torrent* torrent in torrents)
    {
        [torrent stopTransfer];
    }

    [self fullUpdateUI];
}

- (void)removeTorrents:(NSArray<Torrent*>*)torrents deleteData:(BOOL)deleteData
{
    if ([self.fDefaults boolForKey:@"CheckRemove"])
    {
        NSUInteger active = 0, downloading = 0;
        for (Torrent* torrent in torrents)
        {
            if (torrent.active)
            {
                ++active;
                if (!torrent.seeding)
                {
                    ++downloading;
                }
            }
        }

        if ([self.fDefaults boolForKey:@"CheckRemoveDownloading"] ? downloading > 0 : active > 0)
        {
            NSString *title, *message;

            NSUInteger const selected = torrents.count;
            if (selected == 1)
            {
                NSString* torrentName = torrents[0].name;

                if (deleteData)
                {
                    title = [NSString stringWithFormat:NSLocalizedString(
                                                           @"Are you sure you want to remove \"%@\" from the transfer list"
                                                            " and trash the data file?",
                                                           "Removal confirm panel -> title"),
                                                       torrentName];
                }
                else
                {
                    title = [NSString
                        stringWithFormat:NSLocalizedString(@"Are you sure you want to remove \"%@\" from the transfer list?", "Removal confirm panel -> title"),
                                         torrentName];
                }

                message = NSLocalizedString(
                    @"This transfer is active."
                     " Once removed, continuing the transfer will require the torrent file or magnet link.",
                    "Removal confirm panel -> message");
            }
            else
            {
                if (deleteData)
                {
                    title = [NSString stringWithFormat:NSLocalizedString(
                                                           @"Are you sure you want to remove %lu transfers from the transfer list"
                                                            " and trash the data files?",
                                                           "Removal confirm panel -> title"),
                                                       selected];
                }
                else
                {
                    title = [NSString stringWithFormat:NSLocalizedString(
                                                           @"Are you sure you want to remove %lu transfers from the transfer list?",
                                                           "Removal confirm panel -> title"),
                                                       selected];
                }

                if (selected == active)
                {
                    message = [NSString stringWithFormat:NSLocalizedString(@"There are %lu active transfers.", "Removal confirm panel -> message part 1"),
                                                         active];
                }
                else
                {
                    message = [NSString stringWithFormat:NSLocalizedString(@"There are %1$lu transfers (%2$lu active).", "Removal confirm panel -> message part 1"),
                                                         selected,
                                                         active];
                }
                message = [message stringByAppendingFormat:@" %@",
                                                           NSLocalizedString(
                                                               @"Once removed, continuing the transfers will require the torrent files or magnet links.",
                                                               "Removal confirm panel -> message part 2")];
            }

            NSAlert* alert = [[NSAlert alloc] init];
            alert.alertStyle = NSAlertStyleInformational;
            alert.messageText = title;
            alert.informativeText = message;
            [alert addButtonWithTitle:NSLocalizedString(@"Remove", "Removal confirm panel -> button")];
            [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Removal confirm panel -> button")];

            [alert beginSheetModalForWindow:self.fWindow completionHandler:^(NSModalResponse returnCode) {
                if (returnCode == NSAlertFirstButtonReturn)
                {
                    [self confirmRemoveTorrents:torrents deleteData:deleteData];
                }
            }];
            return;
        }
    }

    [self confirmRemoveTorrents:torrents deleteData:deleteData];
}

- (void)confirmRemoveTorrents:(NSArray<Torrent*>*)torrents deleteData:(BOOL)deleteData
{
    //miscellaneous
    for (Torrent* torrent in torrents)
    {
        //don't want any of these starting then stopping
        if (torrent.waitingToStart)
        {
            [torrent stopTransfer];
        }

        //let's expand all groups that have removed items - they either don't exist anymore, are already expanded, or are collapsed (rpc)
        [self.fTableView removeCollapsedGroup:torrent.groupValue];

        //we can't assume the window is active - RPC removal, for example
        [self.fBadger removeTorrent:torrent];
    }

    //#5106 - don't try to remove torrents that have already been removed (fix for a bug, but better safe than crash anyway)
    NSIndexSet* indexesToRemove = [torrents indexesOfObjectsWithOptions:NSEnumerationConcurrent
                                                            passingTest:^BOOL(Torrent* torrent, NSUInteger idx, BOOL* stop) {
                                                                return [self.fTorrents indexOfObjectIdenticalTo:torrent] != NSNotFound;
                                                            }];
    if (torrents.count != indexesToRemove.count)
    {
        NSLog(
            @"trying to remove %ld transfers, but %ld have already been removed",
            torrents.count,
            torrents.count - indexesToRemove.count);
        torrents = [torrents objectsAtIndexes:indexesToRemove];

        if (indexesToRemove.count == 0)
        {
            [self fullUpdateUI];
            return;
        }
    }

    [self.fTorrents removeObjectsInArray:torrents];

    //set up helpers to remove from the table
    __block BOOL beganUpdate = NO;

    void (^doTableRemoval)(NSMutableArray*, id) = ^(NSMutableArray* displayedTorrents, id parent) {
        NSIndexSet* indexes = [displayedTorrents indexesOfObjectsWithOptions:NSEnumerationConcurrent
                                                                 passingTest:^(id obj, NSUInteger idx, BOOL* stop) {
                                                                     return [torrents containsObject:obj];
                                                                 }];

        if (indexes.count > 0)
        {
            if (!beganUpdate)
            {
                [NSAnimationContext beginGrouping]; //this has to be before we set the completion handler (#4874)

                //we can't closeRemoveTorrent: until it's no longer in the GUI at all
                NSAnimationContext.currentContext.completionHandler = ^{
                    for (Torrent* torrent in torrents)
                    {
                        [torrent closeRemoveTorrent:deleteData];
                    }
                };

                [self.fTableView beginUpdates];
                beganUpdate = YES;
            }

            [self.fTableView removeItemsAtIndexes:indexes inParent:parent withAnimation:NSTableViewAnimationSlideLeft];

            [displayedTorrents removeObjectsAtIndexes:indexes];
        }
    };

    //if not removed from the displayed torrents here, fullUpdateUI might cause a crash
    if (self.fDisplayedTorrents.count > 0)
    {
        if ([self.fDisplayedTorrents[0] isKindOfClass:[TorrentGroup class]])
        {
            for (TorrentGroup* group in self.fDisplayedTorrents)
            {
                doTableRemoval(group.torrents, group);
            }
        }
        else
        {
            doTableRemoval(self.fDisplayedTorrents, nil);
        }

        if (beganUpdate)
        {
            [self.fTableView endUpdates];
            [NSAnimationContext endGrouping];
        }
    }

    if (!beganUpdate)
    {
        //do here if we're not doing it at the end of the animation
        for (Torrent* torrent in torrents)
        {
            [torrent closeRemoveTorrent:deleteData];
        }
    }

    [self fullUpdateUI];
}

- (void)removeNoDelete:(id)sender
{
    [self removeTorrents:self.fTableView.selectedTorrents deleteData:NO];
}

- (void)removeDeleteData:(id)sender
{
    [self removeTorrents:self.fTableView.selectedTorrents deleteData:YES];
}

- (void)clearCompleted:(id)sender
{
    NSMutableArray<Torrent*>* torrents = [NSMutableArray array];

    for (Torrent* torrent in self.fTorrents)
    {
        if (torrent.finishedSeeding)
        {
            [torrents addObject:torrent];
        }
    }

    if ([self.fDefaults boolForKey:@"WarningRemoveCompleted"])
    {
        NSString *message, *info;
        if (torrents.count == 1)
        {
            NSString* torrentName = torrents[0].name;
            message = [NSString
                stringWithFormat:NSLocalizedString(@"Are you sure you want to remove \"%@\" from the transfer list?", "Remove completed confirm panel -> title"),
                                 torrentName];

            info = NSLocalizedString(
                @"Once removed, continuing the transfer will require the torrent file or magnet link.",
                "Remove completed confirm panel -> message");
        }
        else
        {
            message = [NSString stringWithFormat:NSLocalizedString(
                                                     @"Are you sure you want to remove %lu completed transfers from the transfer list?",
                                                     "Remove completed confirm panel -> title"),
                                                 torrents.count];

            info = NSLocalizedString(
                @"Once removed, continuing the transfers will require the torrent files or magnet links.",
                "Remove completed confirm panel -> message");
        }

        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = message;
        alert.informativeText = info;
        alert.alertStyle = NSAlertStyleWarning;
        [alert addButtonWithTitle:NSLocalizedString(@"Remove", "Remove completed confirm panel -> button")];
        [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Remove completed confirm panel -> button")];
        alert.showsSuppressionButton = YES;

        NSInteger const returnCode = [alert runModal];
        if (alert.suppressionButton.state)
        {
            [self.fDefaults setBool:NO forKey:@"WarningRemoveCompleted"];
        }

        if (returnCode != NSAlertFirstButtonReturn)
        {
            return;
        }
    }

    [self confirmRemoveTorrents:torrents deleteData:NO];
}

- (void)moveDataFilesSelected:(id)sender
{
    [self moveDataFiles:self.fTableView.selectedTorrents];
}

- (void)moveDataFiles:(NSArray<Torrent*>*)torrents
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.prompt = NSLocalizedString(@"Select", "Move torrent -> prompt");
    panel.allowsMultipleSelection = NO;
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = YES;

    NSUInteger count = torrents.count;
    if (count == 1)
    {
        panel.message = [NSString
            stringWithFormat:NSLocalizedString(@"Select the new folder for \"%@\".", "Move torrent -> select destination folder"),
                             torrents[0].name];
    }
    else
    {
        panel.message = [NSString
            stringWithFormat:NSLocalizedString(@"Select the new folder for %lu data files.", "Move torrent -> select destination folder"), count];
    }

    [panel beginSheetModalForWindow:self.fWindow completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK)
        {
            for (Torrent* torrent in torrents)
            {
                [torrent moveTorrentDataFileTo:panel.URLs[0].path];
            }
        }
    }];
}

- (void)copyTorrentFiles:(id)sender
{
    [self copyTorrentFileForTorrents:[[NSMutableArray alloc] initWithArray:self.fTableView.selectedTorrents]];
}

- (void)copyTorrentFileForTorrents:(NSMutableArray<Torrent*>*)torrents
{
    if (torrents.count == 0)
    {
        return;
    }

    Torrent* torrent = torrents[0];

    if (!torrent.magnet && [NSFileManager.defaultManager fileExistsAtPath:torrent.torrentLocation])
    {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.allowedFileTypes = @[ @"org.bittorrent.torrent", @"torrent" ];
        panel.extensionHidden = NO;

        panel.nameFieldStringValue = torrent.name;

        [panel beginSheetModalForWindow:self.fWindow completionHandler:^(NSInteger result) {
            //copy torrent to new location with name of data file
            if (result == NSModalResponseOK)
            {
                [torrent copyTorrentFileTo:panel.URL.path];
            }

            [torrents removeObjectAtIndex:0];
            [self performSelectorOnMainThread:@selector(copyTorrentFileForTorrents:) withObject:torrents waitUntilDone:NO];
        }];
    }
    else
    {
        if (!torrent.magnet)
        {
            NSAlert* alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle:NSLocalizedString(@"OK", "Torrent file copy alert -> button")];
            alert.messageText = [NSString
                stringWithFormat:NSLocalizedString(@"Copy of \"%@\" Cannot Be Created", "Torrent file copy alert -> title"),
                                 torrent.name];
            alert.informativeText = [NSString
                stringWithFormat:NSLocalizedString(@"The torrent file (%@) cannot be found.", "Torrent file copy alert -> message"),
                                 torrent.torrentLocation];
            alert.alertStyle = NSAlertStyleWarning;

            [alert runModal];
        }

        [torrents removeObjectAtIndex:0];
        [self copyTorrentFileForTorrents:torrents];
    }
}

- (void)copyMagnetLinks:(id)sender
{
    [self.fTableView copy:sender];
}

- (void)revealFile:(id)sender
{
    NSArray* selected = self.fTableView.selectedTorrents;
    NSMutableArray* paths = [NSMutableArray arrayWithCapacity:selected.count];
    for (Torrent* torrent in selected)
    {
        NSString* location = torrent.dataLocation;
        if (location)
        {
            [paths addObject:[NSURL fileURLWithPath:location]];
        }
    }

    if (paths.count > 0)
    {
        [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:paths];
    }
}

- (IBAction)renameSelected:(id)sender
{
    NSArray* selected = self.fTableView.selectedTorrents;
    NSAssert(selected.count == 1, @"1 transfer needs to be selected to rename, but %ld are selected", selected.count);
    Torrent* torrent = selected[0];

    [FileRenameSheetController presentSheetForTorrent:torrent modalForWindow:self.fWindow completionHandler:^(BOOL didRename) {
        if (didRename)
        {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self fullUpdateUI];

                [NSNotificationCenter.defaultCenter postNotificationName:@"ResetInspector" object:self
                                                                userInfo:@{ @"Torrent" : torrent }];
            });
        }
    }];
}

- (void)announceSelectedTorrents:(id)sender
{
    for (Torrent* torrent in self.fTableView.selectedTorrents)
    {
        if (torrent.canManualAnnounce)
        {
            [torrent manualAnnounce];
        }
    }
}

- (void)verifySelectedTorrents:(id)sender
{
    [self verifyTorrents:self.fTableView.selectedTorrents];
}

- (void)verifyTorrents:(NSArray<Torrent*>*)torrents
{
    for (Torrent* torrent in torrents)
    {
        [torrent resetCache];
    }

    [self applyFilter];
}

- (NSArray<Torrent*>*)selectedTorrents
{
    return self.fTableView.selectedTorrents;
}

- (void)showPreferenceWindow:(id)sender
{
    NSWindow* window = _prefsController.window;
    if (!window.visible)
    {
        [window center];
    }

    [window makeKeyAndOrderFront:nil];
}

- (void)showAboutWindow:(id)sender
{
    [AboutWindowController.aboutController showWindow:nil];
}

- (void)showInfo:(id)sender
{
    if (self.fInfoController.window.visible)
    {
        [self.fInfoController close];
    }
    else
    {
        [self.fInfoController updateInfoStats];
        [self.fInfoController.window orderFront:nil];

        if (self.fInfoController.canQuickLook && [QLPreviewPanel sharedPreviewPanelExists] &&
            [QLPreviewPanel sharedPreviewPanel].visible)
        {
            [[QLPreviewPanel sharedPreviewPanel] reloadData];
        }
    }

    [self.fWindow.toolbar validateVisibleItems];
}

- (void)resetInfo
{
    [self.fInfoController setInfoForTorrents:self.fTableView.selectedTorrents];

    if ([QLPreviewPanel sharedPreviewPanelExists] && [QLPreviewPanel sharedPreviewPanel].visible)
    {
        [[QLPreviewPanel sharedPreviewPanel] reloadData];
    }
}

- (void)setInfoTab:(id)sender
{
    if (sender == self.fNextInfoTabItem)
    {
        [self.fInfoController setNextTab];
    }
    else
    {
        [self.fInfoController setPreviousTab];
    }
}

- (MessageWindowController*)messageWindowController
{
    if (!self.fMessageController)
    {
        self.fMessageController = [[MessageWindowController alloc] init];
    }

    return self.fMessageController;
}

- (void)showMessageWindow:(id)sender
{
    [self.messageWindowController showWindow:nil];
}

- (void)showStatsWindow:(id)sender
{
    [StatsWindowController.statsWindow showWindow:nil];
}

- (void)updateUI
{
    CGFloat dlRate = 0.0, ulRate = 0.0;
    BOOL anyCompleted = NO;
    for (Torrent* torrent in self.fTorrents)
    {
        [torrent update];

        //pull the upload and download speeds - most consistent by using current stats
        dlRate += torrent.downloadRate;
        ulRate += torrent.uploadRate;

        anyCompleted |= torrent.finishedSeeding;
    }

    if (!NSApp.hidden)
    {
        if (self.fWindow.visible)
        {
            [self sortTorrentsAndIncludeQueueOrder:NO];

            [self.fStatusBar updateWithDownload:dlRate upload:ulRate];

            self.fClearCompletedButton.hidden = !anyCompleted;
        }

        //update non-constant parts of info window
        if (self.fInfoController.window.visible)
        {
            [self.fInfoController updateInfoStats];
        }
    }

    //badge dock
    [self.fBadger updateBadgeWithDownload:dlRate upload:ulRate];
}

#warning can this be removed or refined?
- (void)fullUpdateUI
{
    [self updateUI];
    [self applyFilter];
    [self.fWindow.toolbar validateVisibleItems];
    [self updateTorrentHistory];
}

- (void)setBottomCountText:(BOOL)filtering
{
    NSString* totalTorrentsString;
    NSUInteger totalCount = self.fTorrents.count;
    if (totalCount != 1)
    {
        totalTorrentsString = [NSString stringWithFormat:NSLocalizedString(@"%lu transfers", "Status bar transfer count"), totalCount];
    }
    else
    {
        totalTorrentsString = NSLocalizedString(@"1 transfer", "Status bar transfer count");
    }

    if (filtering)
    {
        NSUInteger count = self.fTableView.numberOfRows; //have to factor in collapsed rows
        if (count > 0 && ![self.fDisplayedTorrents[0] isKindOfClass:[Torrent class]])
        {
            count -= self.fDisplayedTorrents.count;
        }

        totalTorrentsString = [NSString stringWithFormat:NSLocalizedString(@"%@ of %@", "Status bar transfer count"),
                                                         [NSString formattedUInteger:count],
                                                         totalTorrentsString];
    }

    self.fTotalTorrentsField.stringValue = totalTorrentsString;
}

- (BOOL)userNotificationCenter:(NSUserNotificationCenter*)center shouldPresentNotification:(NSUserNotification*)notification
{
    return YES;
}

- (void)userNotificationCenter:(NSUserNotificationCenter*)center didActivateNotification:(NSUserNotification*)notification
{
    if (!notification.userInfo)
    {
        return;
    }

    if (notification.activationType == NSUserNotificationActivationTypeActionButtonClicked) //reveal
    {
        Torrent* torrent = [self torrentForHash:notification.userInfo[@"Hash"]];
        NSString* location = torrent.dataLocation;
        if (!location)
        {
            location = notification.userInfo[@"Location"];
        }
        if (location)
        {
            [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:location] ]];
        }
    }
    else if (notification.activationType == NSUserNotificationActivationTypeContentsClicked)
    {
        Torrent* torrent = [self torrentForHash:notification.userInfo[@"Hash"]];
        if (!torrent)
        {
            return;
        }
        //select in the table - first see if it's already shown
        NSInteger row = [self.fTableView rowForItem:torrent];
        if (row == -1)
        {
            //if it's not shown, see if it's in a collapsed row
            if ([self.fDefaults boolForKey:@"SortByGroup"])
            {
                __block TorrentGroup* parent = nil;
                [self.fDisplayedTorrents enumerateObjectsWithOptions:NSEnumerationConcurrent
                                                          usingBlock:^(TorrentGroup* group, NSUInteger idx, BOOL* stop) {
                                                              if ([group.torrents containsObject:torrent])
                                                              {
                                                                  parent = group;
                                                                  *stop = YES;
                                                              }
                                                          }];
                if (parent)
                {
                    [[self.fTableView animator] expandItem:parent];
                    row = [self.fTableView rowForItem:torrent];
                }
            }

            if (row == -1)
            {
                //not found - must be filtering
                NSAssert([self.fDefaults boolForKey:@"FilterBar"], @"expected the filter to be enabled");
                [self.fFilterBar reset:YES];

                row = [self.fTableView rowForItem:torrent];

                //if it's not shown, it has to be in a collapsed row...again
                if ([self.fDefaults boolForKey:@"SortByGroup"])
                {
                    __block TorrentGroup* parent = nil;
                    [self.fDisplayedTorrents enumerateObjectsWithOptions:NSEnumerationConcurrent
                                                              usingBlock:^(TorrentGroup* group, NSUInteger idx, BOOL* stop) {
                                                                  if ([group.torrents containsObject:torrent])
                                                                  {
                                                                      parent = group;
                                                                      *stop = YES;
                                                                  }
                                                              }];
                    if (parent)
                    {
                        [[self.fTableView animator] expandItem:parent];
                        row = [self.fTableView rowForItem:torrent];
                    }
                }
            }
        }

        NSAssert1(row != -1, @"expected a row to be found for torrent %@", torrent);

        [self showMainWindow:nil];
        [self.fTableView selectAndScrollToRow:row];
    }
}

- (Torrent*)torrentForHash:(NSString*)hash
{
    NSParameterAssert(hash != nil);

    __block Torrent* torrent = nil;
    [self.fTorrents enumerateObjectsWithOptions:NSEnumerationConcurrent usingBlock:^(Torrent* obj, NSUInteger idx, BOOL* stop) {
        if ([obj.hashString isEqualToString:hash])
        {
            torrent = obj;
            *stop = YES;
        }
    }];
    return torrent;
}

- (void)torrentFinishedDownloading:(NSNotification*)notification
{
    Torrent* torrent = notification.object;

    if ([notification.userInfo[@"WasRunning"] boolValue])
    {
        if (!self.fSoundPlaying && [self.fDefaults boolForKey:@"PlayDownloadSound"])
        {
            NSSound* sound;
            if ((sound = [NSSound soundNamed:[self.fDefaults stringForKey:@"DownloadSound"]]))
            {
                sound.delegate = self;
                self.fSoundPlaying = YES;
                [sound play];
            }
        }

        NSString* location = torrent.dataLocation;

        NSString* notificationTitle = NSLocalizedString(@"Download Complete", "notification title");
        NSUserNotification* notification = [[NSUserNotification alloc] init];
        notification.title = notificationTitle;
        notification.informativeText = torrent.name;

        notification.hasActionButton = YES;
        notification.actionButtonTitle = NSLocalizedString(@"Show", "notification button");

        NSMutableDictionary* userInfo = [NSMutableDictionary dictionaryWithObject:torrent.hashString forKey:@"Hash"];
        if (location)
        {
            userInfo[@"Location"] = location;
        }
        notification.userInfo = userInfo;

        [NSUserNotificationCenter.defaultUserNotificationCenter deliverNotification:notification];

        if (!self.fWindow.mainWindow)
        {
            [self.fBadger addCompletedTorrent:torrent];
        }

        //bounce download stack
        [NSDistributedNotificationCenter.defaultCenter postNotificationName:@"com.apple.DownloadFileFinished"
                                                                     object:torrent.dataLocation];
    }

    [self fullUpdateUI];
}

- (void)torrentRestartedDownloading:(NSNotification*)notification
{
    [self fullUpdateUI];
}

- (void)torrentFinishedSeeding:(NSNotification*)notification
{
    Torrent* torrent = notification.object;

    if (!self.fSoundPlaying && [self.fDefaults boolForKey:@"PlaySeedingSound"])
    {
        NSSound* sound;
        if ((sound = [NSSound soundNamed:[self.fDefaults stringForKey:@"SeedingSound"]]))
        {
            sound.delegate = self;
            self.fSoundPlaying = YES;
            [sound play];
        }
    }

    NSString* location = torrent.dataLocation;

    NSString* notificationTitle = NSLocalizedString(@"Seeding Complete", "notification title");
    NSUserNotification* userNotification = [[NSUserNotification alloc] init];
    userNotification.title = notificationTitle;
    userNotification.informativeText = torrent.name;

    userNotification.hasActionButton = YES;
    userNotification.actionButtonTitle = NSLocalizedString(@"Show", "notification button");

    NSMutableDictionary* userInfo = [NSMutableDictionary dictionaryWithObject:torrent.hashString forKey:@"Hash"];
    if (location)
    {
        userInfo[@"Location"] = location;
    }
    userNotification.userInfo = userInfo;

    [NSUserNotificationCenter.defaultUserNotificationCenter deliverNotification:userNotification];

    //removing from the list calls fullUpdateUI
    if (torrent.removeWhenFinishSeeding)
    {
        [self confirmRemoveTorrents:@[ torrent ] deleteData:NO];
    }
    else
    {
        if (!self.fWindow.mainWindow)
        {
            [self.fBadger addCompletedTorrent:torrent];
        }

        [self fullUpdateUI];

        if ([self.fTableView.selectedTorrents containsObject:torrent])
        {
            [self.fInfoController updateInfoStats];
            [self.fInfoController updateOptions];
        }
    }
}

- (void)updateTorrentHistory
{
    NSMutableArray* history = [NSMutableArray arrayWithCapacity:self.fTorrents.count];

    for (Torrent* torrent in self.fTorrents)
    {
        [history addObject:torrent.history];
        self.fTorrentHashes[torrent.hashString] = torrent;
    }

    NSString* historyFile = [self.fConfigDirectory stringByAppendingPathComponent:TRANSFER_PLIST];
    [history writeToFile:historyFile atomically:YES];
}

- (void)setSort:(id)sender
{
    NSString* sortType;
    NSMenuItem* senderMenuItem = sender;
    switch (senderMenuItem.tag)
    {
    case SORT_ORDER_TAG:
        sortType = SORT_ORDER;
        [self.fDefaults setBool:NO forKey:@"SortReverse"];
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
        NSAssert1(NO, @"Unknown sort tag received: %ld", senderMenuItem.tag);
        return;
    }

    [self.fDefaults setObject:sortType forKey:@"Sort"];

    [self sortTorrentsAndIncludeQueueOrder:YES];
}

- (void)setSortByGroup:(id)sender
{
    BOOL sortByGroup = ![self.fDefaults boolForKey:@"SortByGroup"];
    [self.fDefaults setBool:sortByGroup forKey:@"SortByGroup"];

    [self applyFilter];
}

- (void)setSortReverse:(id)sender
{
    BOOL const setReverse = ((NSMenuItem*)sender).tag == SORT_DESC_TAG;
    if (setReverse != [self.fDefaults boolForKey:@"SortReverse"])
    {
        [self.fDefaults setBool:setReverse forKey:@"SortReverse"];
        [self sortTorrentsAndIncludeQueueOrder:NO];
    }
}

- (void)sortTorrentsAndIncludeQueueOrder:(BOOL)includeQueueOrder
{
    //actually sort
    [self sortTorrentsCallUpdates:YES includeQueueOrder:includeQueueOrder];
    self.fTableView.needsDisplay = YES;
}

- (void)sortTorrentsCallUpdates:(BOOL)callUpdates includeQueueOrder:(BOOL)includeQueueOrder
{
    BOOL const asc = ![self.fDefaults boolForKey:@"SortReverse"];

    NSArray* descriptors;
    NSSortDescriptor* nameDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"name" ascending:asc
                                                                      selector:@selector(localizedStandardCompare:)];

    NSString* sortType = [self.fDefaults stringForKey:@"Sort"];
    if ([sortType isEqualToString:SORT_STATE])
    {
        NSSortDescriptor* stateDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"stateSortKey" ascending:!asc];
        NSSortDescriptor* progressDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"progress" ascending:!asc];
        NSSortDescriptor* ratioDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"ratio" ascending:!asc];

        descriptors = @[ stateDescriptor, progressDescriptor, ratioDescriptor, nameDescriptor ];
    }
    else if ([sortType isEqualToString:SORT_PROGRESS])
    {
        NSSortDescriptor* progressDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"progress" ascending:asc];
        NSSortDescriptor* ratioProgressDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"progressStopRatio" ascending:asc];
        NSSortDescriptor* ratioDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"ratio" ascending:asc];

        descriptors = @[ progressDescriptor, ratioProgressDescriptor, ratioDescriptor, nameDescriptor ];
    }
    else if ([sortType isEqualToString:SORT_TRACKER])
    {
        NSSortDescriptor* trackerDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"trackerSortKey" ascending:asc
                                                                             selector:@selector(localizedCaseInsensitiveCompare:)];

        descriptors = @[ trackerDescriptor, nameDescriptor ];
    }
    else if ([sortType isEqualToString:SORT_ACTIVITY])
    {
        NSSortDescriptor* rateDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"totalRate" ascending:!asc];
        NSSortDescriptor* activityDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"dateActivityOrAdd" ascending:!asc];

        descriptors = @[ rateDescriptor, activityDescriptor, nameDescriptor ];
    }
    else if ([sortType isEqualToString:SORT_DATE])
    {
        NSSortDescriptor* dateDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"dateAdded" ascending:asc];

        descriptors = @[ dateDescriptor, nameDescriptor ];
    }
    else if ([sortType isEqualToString:SORT_SIZE])
    {
        NSSortDescriptor* sizeDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"size" ascending:asc];

        descriptors = @[ sizeDescriptor, nameDescriptor ];
    }
    else if ([sortType isEqualToString:SORT_NAME])
    {
        descriptors = @[ nameDescriptor ];
    }
    else
    {
        NSAssert1([sortType isEqualToString:SORT_ORDER], @"Unknown sort type received: %@", sortType);

        if (!includeQueueOrder)
        {
            return;
        }

        NSSortDescriptor* orderDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"queuePosition" ascending:asc];

        descriptors = @[ orderDescriptor ];
    }

    BOOL beganTableUpdate = !callUpdates;

    //actually sort
    if ([self.fDefaults boolForKey:@"SortByGroup"])
    {
        for (TorrentGroup* group in self.fDisplayedTorrents)
        {
            [self rearrangeTorrentTableArray:group.torrents forParent:group withSortDescriptors:descriptors
                            beganTableUpdate:&beganTableUpdate];
        }
    }
    else
    {
        [self rearrangeTorrentTableArray:self.fDisplayedTorrents forParent:nil withSortDescriptors:descriptors
                        beganTableUpdate:&beganTableUpdate];
    }

    if (beganTableUpdate && callUpdates)
    {
        [self.fTableView endUpdates];
    }
}

#warning redo so that we search a copy once again (best explained by changing sorting from ascending to descending)
- (void)rearrangeTorrentTableArray:(NSMutableArray*)rearrangeArray
                         forParent:parent
               withSortDescriptors:(NSArray*)descriptors
                  beganTableUpdate:(BOOL*)beganTableUpdate
{
    for (NSUInteger currentIndex = 1; currentIndex < rearrangeArray.count; ++currentIndex)
    {
        //manually do the sorting in-place
        NSUInteger const insertIndex = [rearrangeArray indexOfObject:rearrangeArray[currentIndex]
                                                       inSortedRange:NSMakeRange(0, currentIndex)
                                                             options:(NSBinarySearchingInsertionIndex | NSBinarySearchingLastEqual)
                                                     usingComparator:^NSComparisonResult(id obj1, id obj2) {
                                                         for (NSSortDescriptor* descriptor in descriptors)
                                                         {
                                                             NSComparisonResult const result = [descriptor compareObject:obj1
                                                                                                                toObject:obj2];
                                                             if (result != NSOrderedSame)
                                                             {
                                                                 return result;
                                                             }
                                                         }

                                                         return NSOrderedSame;
                                                     }];

        if (insertIndex != currentIndex)
        {
            if (!*beganTableUpdate)
            {
                *beganTableUpdate = YES;
                [self.fTableView beginUpdates];
            }

            [rearrangeArray moveObjectAtIndex:currentIndex toIndex:insertIndex];
            [self.fTableView moveItemAtIndex:currentIndex inParent:parent toIndex:insertIndex inParent:parent];
        }
    }

    NSAssert2(
        [rearrangeArray isEqualToArray:[rearrangeArray sortedArrayUsingDescriptors:descriptors]],
        @"Torrent rearranging didn't work! %@ %@",
        rearrangeArray,
        [rearrangeArray sortedArrayUsingDescriptors:descriptors]);
}

- (void)applyFilter
{
    NSString* filterType = [self.fDefaults stringForKey:@"Filter"];
    BOOL filterActive = NO, filterDownload = NO, filterSeed = NO, filterPause = NO, filterError = NO, filterStatus = YES;
    if ([filterType isEqualToString:FILTER_ACTIVE])
    {
        filterActive = YES;
    }
    else if ([filterType isEqualToString:FILTER_DOWNLOAD])
    {
        filterDownload = YES;
    }
    else if ([filterType isEqualToString:FILTER_SEED])
    {
        filterSeed = YES;
    }
    else if ([filterType isEqualToString:FILTER_PAUSE])
    {
        filterPause = YES;
    }
    else if ([filterType isEqualToString:FILTER_ERROR])
    {
        filterError = YES;
    }
    else
    {
        filterStatus = NO;
    }

    NSInteger const groupFilterValue = [self.fDefaults integerForKey:@"FilterGroup"];
    BOOL const filterGroup = groupFilterValue != GROUP_FILTER_ALL_TAG;

    NSArray* searchStrings = self.fFilterBar.searchStrings;
    if (searchStrings && searchStrings.count == 0)
    {
        searchStrings = nil;
    }
    BOOL const filterTracker = searchStrings && [[self.fDefaults stringForKey:@"FilterSearchType"] isEqualToString:FILTER_TYPE_TRACKER];

    std::atomic<int32_t> active{ 0 }, downloading{ 0 }, seeding{ 0 }, paused{ 0 }, error{ 0 };
    // Pointers to be captured by Obj-C Block as const*
    auto* activeRef = &active;
    auto* downloadingRef = &downloading;
    auto* seedingRef = &seeding;
    auto* pausedRef = &paused;
    auto* errorRef = &error;
    //filter & get counts of each type
    NSIndexSet* indexesOfNonFilteredTorrents = [self.fTorrents
        indexesOfObjectsWithOptions:NSEnumerationConcurrent passingTest:^BOOL(Torrent* torrent, NSUInteger idx, BOOL* stop) {
            //check status
            if (torrent.active && !torrent.checkingWaiting)
            {
                BOOL const isActive = !torrent.stalled;
                if (isActive)
                {
                    std::atomic_fetch_add_explicit(activeRef, 1, std::memory_order_relaxed);
                }

                if (torrent.seeding)
                {
                    std::atomic_fetch_add_explicit(seedingRef, 1, std::memory_order_relaxed);
                    if (filterStatus && !((filterActive && isActive) || filterSeed))
                    {
                        return NO;
                    }
                }
                else
                {
                    std::atomic_fetch_add_explicit(downloadingRef, 1, std::memory_order_relaxed);
                    if (filterStatus && !((filterActive && isActive) || filterDownload))
                    {
                        return NO;
                    }
                }
            }
            else if (torrent.error)
            {
                std::atomic_fetch_add_explicit(errorRef, 1, std::memory_order_relaxed);
                if (filterStatus && !filterError)
                {
                    return NO;
                }
            }
            else
            {
                std::atomic_fetch_add_explicit(pausedRef, 1, std::memory_order_relaxed);
                if (filterStatus && !filterPause)
                {
                    return NO;
                }
            }

            //checkGroup
            if (filterGroup)
                if (torrent.groupValue != groupFilterValue)
                {
                    return NO;
                }

            //check text field
            if (searchStrings)
            {
                __block BOOL removeTextField = NO;
                if (filterTracker)
                {
                    NSArray* trackers = torrent.allTrackersFlat;

                    //to count, we need each string in at least 1 tracker
                    [searchStrings enumerateObjectsWithOptions:NSEnumerationConcurrent usingBlock:^(id searchString, NSUInteger idx, BOOL* stop) {
                        __block BOOL found = NO;
                        [trackers enumerateObjectsWithOptions:NSEnumerationConcurrent
                                                   usingBlock:^(NSString* tracker, NSUInteger idx, BOOL* stopTracker) {
                                                       if ([tracker rangeOfString:searchString
                                                                          options:(NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch)]
                                                               .location != NSNotFound)
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
                    [searchStrings enumerateObjectsWithOptions:NSEnumerationConcurrent usingBlock:^(id searchString, NSUInteger idx, BOOL* stop) {
                        if ([torrent.name rangeOfString:searchString options:(NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch)]
                                .location == NSNotFound)
                        {
                            removeTextField = YES;
                            *stop = YES;
                        }
                    }];
                }

                if (removeTextField)
                {
                    return NO;
                }
            }

            return YES;
        }];

    NSArray<Torrent*>* allTorrents = [self.fTorrents objectsAtIndexes:indexesOfNonFilteredTorrents];

    //set button tooltips
    if (self.fFilterBar)
    {
        [self.fFilterBar setCountAll:self.fTorrents.count active:active.load() downloading:downloading.load()
                             seeding:seeding.load()
                              paused:paused.load()
                               error:error.load()];
    }

    //if either the previous or current lists are blank, set its value to the other
    BOOL const groupRows = allTorrents.count > 0 ?
        [self.fDefaults boolForKey:@"SortByGroup"] :
        (self.fDisplayedTorrents.count > 0 && [self.fDisplayedTorrents[0] isKindOfClass:[TorrentGroup class]]);
    BOOL const wasGroupRows = self.fDisplayedTorrents.count > 0 ? [self.fDisplayedTorrents[0] isKindOfClass:[TorrentGroup class]] : groupRows;

#warning could probably be merged with later code somehow
    //clear display cache for not-shown torrents
    if (self.fDisplayedTorrents.count > 0)
    {
        //for each torrent, removes the previous piece info if it's not in allTorrents, and keeps track of which torrents we already found in allTorrents
        void (^removePreviousFinishedPieces)(id, NSUInteger, BOOL*) = ^(Torrent* torrent, NSUInteger idx, BOOL* stop) {
            //we used to keep track of which torrents we already found in allTorrents, but it wasn't safe fo concurrent enumeration
            if (![allTorrents containsObject:torrent])
            {
                torrent.previousFinishedPieces = nil;
            }
        };

        if (wasGroupRows)
        {
            [self.fDisplayedTorrents enumerateObjectsWithOptions:NSEnumerationConcurrent usingBlock:^(id obj, NSUInteger idx, BOOL* stop) {
                [((TorrentGroup*)obj).torrents enumerateObjectsWithOptions:NSEnumerationConcurrent
                                                                usingBlock:removePreviousFinishedPieces];
            }];
        }
        else
        {
            [self.fDisplayedTorrents enumerateObjectsWithOptions:NSEnumerationConcurrent usingBlock:removePreviousFinishedPieces];
        }
    }

    BOOL beganUpdates = NO;

    //don't animate torrents when first launching
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSAnimationContext.currentContext.duration = 0;
    });
    [NSAnimationContext beginGrouping];

    //add/remove torrents (and rearrange for groups), one by one
    if (!groupRows && !wasGroupRows)
    {
        NSMutableIndexSet* addIndexes = [NSMutableIndexSet indexSet];
        NSMutableIndexSet* removePreviousIndexes = [NSMutableIndexSet
            indexSetWithIndexesInRange:NSMakeRange(0, self.fDisplayedTorrents.count)];

        //for each of the torrents to add, find if it already exists (and keep track of those we've already added & those we need to remove)
        [allTorrents enumerateObjectsWithOptions:0 usingBlock:^(id objAll, NSUInteger previousIndex, BOOL* stop) {
            NSUInteger const currentIndex = [self.fDisplayedTorrents indexOfObjectAtIndexes:removePreviousIndexes
                                                                                    options:NSEnumerationConcurrent
                                                                                passingTest:^(id objDisplay, NSUInteger idx, BOOL* stop) {
                                                                                    return (BOOL)(objAll == objDisplay);
                                                                                }];
            if (currentIndex == NSNotFound)
            {
                [addIndexes addIndex:previousIndex];
            }
            else
            {
                [removePreviousIndexes removeIndex:currentIndex];
            }
        }];

        if (addIndexes.count > 0 || removePreviousIndexes.count > 0)
        {
            beganUpdates = YES;
            [self.fTableView beginUpdates];

            //remove torrents we didn't find
            if (removePreviousIndexes.count > 0)
            {
                [self.fDisplayedTorrents removeObjectsAtIndexes:removePreviousIndexes];
                [self.fTableView removeItemsAtIndexes:removePreviousIndexes inParent:nil withAnimation:NSTableViewAnimationSlideDown];
            }

            //add new torrents
            if (addIndexes.count > 0)
            {
                //slide new torrents in differently
                if (self.fAddingTransfers)
                {
                    NSIndexSet* newAddIndexes = [allTorrents indexesOfObjectsAtIndexes:addIndexes options:NSEnumerationConcurrent
                                                                           passingTest:^BOOL(Torrent* obj, NSUInteger idx, BOOL* stop) {
                                                                               return [self.fAddingTransfers containsObject:obj];
                                                                           }];

                    [addIndexes removeIndexes:newAddIndexes];

                    [self.fDisplayedTorrents addObjectsFromArray:[allTorrents objectsAtIndexes:newAddIndexes]];
                    [self.fTableView insertItemsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(
                                                                                                     self.fDisplayedTorrents.count -
                                                                                                         newAddIndexes.count,
                                                                                                     newAddIndexes.count)]
                                                 inParent:nil
                                            withAnimation:NSTableViewAnimationSlideLeft];
                }

                [self.fDisplayedTorrents addObjectsFromArray:[allTorrents objectsAtIndexes:addIndexes]];
                [self.fTableView
                    insertItemsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(
                                                                                    self.fDisplayedTorrents.count - addIndexes.count,
                                                                                    addIndexes.count)]
                                inParent:nil
                           withAnimation:NSTableViewAnimationSlideDown];
            }
        }
    }
    else if (groupRows && wasGroupRows)
    {
        NSAssert(groupRows && wasGroupRows, @"Should have had group rows and should remain with group rows");

#warning don't always do?
        beganUpdates = YES;
        [self.fTableView beginUpdates];

        NSMutableIndexSet* unusedAllTorrentsIndexes = [NSMutableIndexSet indexSetWithIndexesInRange:NSMakeRange(0, allTorrents.count)];

        NSMutableDictionary* groupsByIndex = [NSMutableDictionary dictionaryWithCapacity:self.fDisplayedTorrents.count];
        for (TorrentGroup* group in self.fDisplayedTorrents)
        {
            groupsByIndex[@(group.groupIndex)] = group;
        }

        NSUInteger const originalGroupCount = self.fDisplayedTorrents.count;
        for (NSUInteger index = 0; index < originalGroupCount; ++index)
        {
            TorrentGroup* group = self.fDisplayedTorrents[index];

            NSMutableIndexSet* removeIndexes = [NSMutableIndexSet indexSet];

            //needs to be a signed integer
            for (NSUInteger indexInGroup = 0; indexInGroup < group.torrents.count; ++indexInGroup)
            {
                Torrent* torrent = group.torrents[indexInGroup];
                NSUInteger const allIndex = [allTorrents indexOfObjectAtIndexes:unusedAllTorrentsIndexes options:NSEnumerationConcurrent
                                                                    passingTest:^(id obj, NSUInteger idx, BOOL* stop) {
                                                                        return (BOOL)(obj == torrent);
                                                                    }];
                if (allIndex == NSNotFound)
                {
                    [removeIndexes addIndex:indexInGroup];
                }
                else
                {
                    BOOL markTorrentAsUsed = YES;

                    NSInteger const groupValue = torrent.groupValue;
                    if (groupValue != group.groupIndex)
                    {
                        TorrentGroup* newGroup = groupsByIndex[@(groupValue)];
                        if (!newGroup)
                        {
                            newGroup = [[TorrentGroup alloc] initWithGroup:groupValue];
                            groupsByIndex[@(groupValue)] = newGroup;
                            [self.fDisplayedTorrents addObject:newGroup];

                            [self.fTableView insertItemsAtIndexes:[NSIndexSet indexSetWithIndex:self.fDisplayedTorrents.count - 1]
                                                         inParent:nil
                                                    withAnimation:NSTableViewAnimationEffectFade];
                            [self.fTableView isGroupCollapsed:groupValue] ? [self.fTableView collapseItem:newGroup] :
                                                                            [self.fTableView expandItem:newGroup];
                        }
                        else //if we haven't processed the other group yet, we have to make sure we don't flag it for removal the next time
                        {
                            //ugggh, but shouldn't happen too often
                            if ([self.fDisplayedTorrents indexOfObject:newGroup
                                                               inRange:NSMakeRange(index + 1, originalGroupCount - (index + 1))] != NSNotFound)
                            {
                                markTorrentAsUsed = NO;
                            }
                        }

                        [group.torrents removeObjectAtIndex:indexInGroup];
                        [newGroup.torrents addObject:torrent];

                        [self.fTableView moveItemAtIndex:indexInGroup inParent:group toIndex:newGroup.torrents.count - 1
                                                inParent:newGroup];

                        --indexInGroup;
                    }

                    if (markTorrentAsUsed)
                    {
                        [unusedAllTorrentsIndexes removeIndex:allIndex];
                    }
                }
            }

            if (removeIndexes.count > 0)
            {
                [group.torrents removeObjectsAtIndexes:removeIndexes];
                [self.fTableView removeItemsAtIndexes:removeIndexes inParent:group withAnimation:NSTableViewAnimationEffectFade];
            }
        }

        //add remaining new torrents
        for (Torrent* torrent in [allTorrents objectsAtIndexes:unusedAllTorrentsIndexes])
        {
            NSInteger const groupValue = torrent.groupValue;
            TorrentGroup* group = groupsByIndex[@(groupValue)];
            if (!group)
            {
                group = [[TorrentGroup alloc] initWithGroup:groupValue];
                groupsByIndex[@(groupValue)] = group;
                [self.fDisplayedTorrents addObject:group];

                [self.fTableView insertItemsAtIndexes:[NSIndexSet indexSetWithIndex:self.fDisplayedTorrents.count - 1] inParent:nil
                                        withAnimation:NSTableViewAnimationEffectFade];
                [self.fTableView isGroupCollapsed:groupValue] ? [self.fTableView collapseItem:group] : [self.fTableView expandItem:group];
            }

            [group.torrents addObject:torrent];

            BOOL const newTorrent = self.fAddingTransfers && [self.fAddingTransfers containsObject:torrent];
            [self.fTableView insertItemsAtIndexes:[NSIndexSet indexSetWithIndex:group.torrents.count - 1] inParent:group
                                    withAnimation:newTorrent ? NSTableViewAnimationSlideLeft : NSTableViewAnimationSlideDown];
        }

        //remove empty groups
        NSIndexSet* removeGroupIndexes = [self.fDisplayedTorrents
            indexesOfObjectsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, originalGroupCount)]
                              options:NSEnumerationConcurrent passingTest:^BOOL(id obj, NSUInteger idx, BOOL* stop) {
                                  return ((TorrentGroup*)obj).torrents.count == 0;
                              }];

        if (removeGroupIndexes.count > 0)
        {
            [self.fDisplayedTorrents removeObjectsAtIndexes:removeGroupIndexes];
            [self.fTableView removeItemsAtIndexes:removeGroupIndexes inParent:nil withAnimation:NSTableViewAnimationEffectFade];
        }

        //now that all groups are there, sort them - don't insert on the fly in case groups were reordered in prefs
        NSSortDescriptor* groupDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"groupOrderValue" ascending:YES];
        [self rearrangeTorrentTableArray:self.fDisplayedTorrents forParent:nil withSortDescriptors:@[ groupDescriptor ]
                        beganTableUpdate:&beganUpdates];
    }
    else
    {
        NSAssert(groupRows != wasGroupRows, @"Trying toggling group-torrent reordering when we weren't expecting to.");

        //set all groups as expanded
        [self.fTableView removeAllCollapsedGroups];

//since we're not doing this the right way (boo buggy animation), we need to remember selected values
#warning when Lion-only and using views instead of cells, this likely won't be needed
        NSArray* selectedValues = self.fTableView.selectedValues;

        beganUpdates = YES;
        [self.fTableView beginUpdates];

        [self.fTableView removeItemsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fDisplayedTorrents.count)]
                                     inParent:nil
                                withAnimation:NSTableViewAnimationSlideDown];

        if (groupRows)
        {
            //a map for quickly finding groups
            NSMutableDictionary* groupsByIndex = [NSMutableDictionary dictionaryWithCapacity:GroupsController.groups.numberOfGroups];
            for (Torrent* torrent in allTorrents)
            {
                NSInteger const groupValue = torrent.groupValue;
                TorrentGroup* group = groupsByIndex[@(groupValue)];
                if (!group)
                {
                    group = [[TorrentGroup alloc] initWithGroup:groupValue];
                    groupsByIndex[@(groupValue)] = group;
                }

                [group.torrents addObject:torrent];
            }

            [self.fDisplayedTorrents setArray:groupsByIndex.allValues];

            //we need the groups to be sorted, and we can do it without moving items in the table, too!
            NSSortDescriptor* groupDescriptor = [NSSortDescriptor sortDescriptorWithKey:@"groupOrderValue" ascending:YES];
            [self.fDisplayedTorrents sortUsingDescriptors:@[ groupDescriptor ]];
        }
        else
            [self.fDisplayedTorrents setArray:allTorrents];

        [self.fTableView insertItemsAtIndexes:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fDisplayedTorrents.count)]
                                     inParent:nil
                                withAnimation:NSTableViewAnimationEffectFade];

        if (groupRows)
        {
            //actually expand group rows
            for (TorrentGroup* group in self.fDisplayedTorrents)
                [self.fTableView expandItem:group];
        }

        if (selectedValues)
        {
            [self.fTableView selectValues:selectedValues];
        }
    }

    //sort the torrents (won't sort the groups, though)
    [self sortTorrentsCallUpdates:!beganUpdates includeQueueOrder:YES];

    if (beganUpdates)
    {
        [self.fTableView endUpdates];
    }
    self.fTableView.needsDisplay = YES;

    [NSAnimationContext endGrouping];

    [self resetInfo]; //if group is already selected, but the torrents in it change

    [self setBottomCountText:groupRows || filterStatus || filterGroup || searchStrings];

    [self setWindowSizeToFit];

    if (self.fAddingTransfers)
    {
        self.fAddingTransfers = nil;
    }
}

- (void)switchFilter:(id)sender
{
    [self.fFilterBar switchFilter:sender == self.fNextFilterItem];
}

- (IBAction)showGlobalPopover:(id)sender
{
    if (self.fGlobalPopoverShown)
    {
        return;
    }

    NSPopover* popover = [[NSPopover alloc] init];
    popover.behavior = NSPopoverBehaviorTransient;
    GlobalOptionsPopoverViewController* viewController = [[GlobalOptionsPopoverViewController alloc] initWithHandle:self.fLib];
    popover.contentViewController = viewController;
    popover.delegate = self;

    NSView* senderView = sender;
    [popover showRelativeToRect:senderView.frame ofView:senderView preferredEdge:NSMaxYEdge];
}

//don't show multiple popovers when clicking the gear button repeatedly
- (void)popoverWillShow:(NSNotification*)notification
{
    self.fGlobalPopoverShown = YES;
}

- (void)popoverWillClose:(NSNotification*)notification
{
    self.fGlobalPopoverShown = NO;
}

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    if (menu == self.fGroupsSetMenu || menu == self.fGroupsSetContextMenu)
    {
        for (NSInteger i = menu.numberOfItems - 1; i >= 0; i--)
        {
            [menu removeItemAtIndex:i];
        }

        NSMenu* groupMenu = [GroupsController.groups groupMenuWithTarget:self action:@selector(setGroup:) isSmall:NO];

        NSInteger const groupMenuCount = groupMenu.numberOfItems;
        for (NSInteger i = 0; i < groupMenuCount; i++)
        {
            NSMenuItem* item = [groupMenu itemAtIndex:0];
            [groupMenu removeItemAtIndex:0];
            [menu addItem:item];
        }
    }
    else if (menu == self.fShareMenu || menu == self.fShareContextMenu)
    {
        [menu removeAllItems];

        for (NSMenuItem* item in ShareTorrentFileHelper.sharedHelper.menuItems)
        {
            [menu addItem:item];
        }
    }
}

- (void)setGroup:(id)sender
{
    for (Torrent* torrent in self.fTableView.selectedTorrents)
    {
        [self.fTableView removeCollapsedGroup:torrent.groupValue]; //remove old collapsed group

        [torrent setGroupValue:((NSMenuItem*)sender).tag determinationType:TorrentDeterminationUserSpecified];
    }

    [self applyFilter];
    [self updateUI];
    [self updateTorrentHistory];
}

- (void)toggleSpeedLimit:(id)sender
{
    [self.fDefaults setBool:![self.fDefaults boolForKey:@"SpeedLimit"] forKey:@"SpeedLimit"];
    [self speedLimitChanged:sender];
}

- (void)speedLimitChanged:(id)sender
{
    tr_sessionUseAltSpeed(self.fLib, [self.fDefaults boolForKey:@"SpeedLimit"]);
    [self.fStatusBar updateSpeedFieldsToolTips];
}

- (void)altSpeedToggledCallbackIsLimited:(NSDictionary*)dict
{
    BOOL const isLimited = [dict[@"Active"] boolValue];

    [self.fDefaults setBool:isLimited forKey:@"SpeedLimit"];
    [self.fStatusBar updateSpeedFieldsToolTips];

    if (![dict[@"ByUser"] boolValue])
    {
        NSUserNotification* notification = [[NSUserNotification alloc] init];
        notification.title = isLimited ? NSLocalizedString(@"Speed Limit Auto Enabled", "notification title") :
                                         NSLocalizedString(@"Speed Limit Auto Disabled", "notification title");
        notification.informativeText = NSLocalizedString(@"Bandwidth settings changed", "notification description");
        notification.hasActionButton = NO;

        [NSUserNotificationCenter.defaultUserNotificationCenter deliverNotification:notification];
    }
}

- (void)sound:(NSSound*)sound didFinishPlaying:(BOOL)finishedPlaying
{
    self.fSoundPlaying = NO;
}

- (void)VDKQueue:(VDKQueue*)queue receivedNotification:(NSString*)notification forPath:(NSString*)fpath
{
    //don't assume that just because we're watching for write notification, we'll only receive write notifications

    if (![self.fDefaults boolForKey:@"AutoImport"] || ![self.fDefaults stringForKey:@"AutoImportDirectory"])
    {
        return;
    }

    if (self.fAutoImportTimer.valid)
    {
        [self.fAutoImportTimer invalidate];
    }

    //check again in 10 seconds in case torrent file wasn't complete
    self.fAutoImportTimer = [NSTimer scheduledTimerWithTimeInterval:10.0 target:self
                                                           selector:@selector(checkAutoImportDirectory)
                                                           userInfo:nil
                                                            repeats:NO];

    [self checkAutoImportDirectory];
}

- (void)changeAutoImport
{
    if (self.fAutoImportTimer.valid)
    {
        [self.fAutoImportTimer invalidate];
    }
    self.fAutoImportTimer = nil;

    self.fAutoImportedNames = nil;

    [self checkAutoImportDirectory];
}

- (void)checkAutoImportDirectory
{
    NSString* path;
    if (![self.fDefaults boolForKey:@"AutoImport"] || !(path = [self.fDefaults stringForKey:@"AutoImportDirectory"]))
    {
        return;
    }

    path = path.stringByExpandingTildeInPath;

    NSArray<NSString*>* importedNames;
    if (!(importedNames = [NSFileManager.defaultManager contentsOfDirectoryAtPath:path error:NULL]))
    {
        return;
    }

    //only check files that have not been checked yet
    NSMutableArray* newNames = [importedNames mutableCopy];

    if (self.fAutoImportedNames)
    {
        [newNames removeObjectsInArray:self.fAutoImportedNames];
    }
    else
    {
        self.fAutoImportedNames = [[NSMutableArray alloc] init];
    }
    [self.fAutoImportedNames setArray:importedNames];

    for (NSString* file in newNames)
    {
        if ([file hasPrefix:@"."])
        {
            continue;
        }

        NSString* fullFile = [path stringByAppendingPathComponent:file];

        if (!([[NSWorkspace.sharedWorkspace typeOfFile:fullFile error:NULL] isEqualToString:@"org.bittorrent.torrent"] ||
              [fullFile.pathExtension caseInsensitiveCompare:@"torrent"] == NSOrderedSame))
        {
            continue;
        }

        auto metainfo = tr_torrent_metainfo{};
        if (!metainfo.parseTorrentFile(fullFile.UTF8String))
        {
            break;
        }

        [self openFiles:@[ fullFile ] addType:ADD_AUTO forcePath:nil];

        NSString* notificationTitle = NSLocalizedString(@"Torrent File Auto Added", "notification title");
        NSUserNotification* notification = [[NSUserNotification alloc] init];
        notification.title = notificationTitle;
        notification.informativeText = file;
        notification.hasActionButton = NO;

        [NSUserNotificationCenter.defaultUserNotificationCenter deliverNotification:notification];
    }
}

- (void)beginCreateFile:(NSNotification*)notification
{
    if (![self.fDefaults boolForKey:@"AutoImport"])
    {
        return;
    }

    NSString *location = ((NSURL*)notification.object).path, *path = [self.fDefaults stringForKey:@"AutoImportDirectory"];

    if (location && path && [location.stringByDeletingLastPathComponent.stringByExpandingTildeInPath isEqualToString:path.stringByExpandingTildeInPath])
    {
        [self.fAutoImportedNames addObject:location.lastPathComponent];
    }
}

- (NSInteger)outlineView:(NSOutlineView*)outlineView numberOfChildrenOfItem:(id)item
{
    if (item)
    {
        return ((TorrentGroup*)item).torrents.count;
    }
    else
    {
        return self.fDisplayedTorrents.count;
    }
}

- (id)outlineView:(NSOutlineView*)outlineView child:(NSInteger)index ofItem:(id)item
{
    if (item)
    {
        return ((TorrentGroup*)item).torrents[index];
    }
    else
    {
        return self.fDisplayedTorrents[index];
    }
}

- (BOOL)outlineView:(NSOutlineView*)outlineView isItemExpandable:(id)item
{
    return ![item isKindOfClass:[Torrent class]];
}

- (id)outlineView:(NSOutlineView*)outlineView objectValueForTableColumn:(NSTableColumn*)tableColumn byItem:(id)item
{
    if ([item isKindOfClass:[Torrent class]])
    {
        if (tableColumn)
        {
            return nil;
        }
        return ((Torrent*)item).hashString;
    }
    else
    {
        NSString* ident = tableColumn.identifier;
        TorrentGroup* group = (TorrentGroup*)item;
        if ([ident isEqualToString:@"Group"])
        {
            NSInteger groupIndex = group.groupIndex;
            return groupIndex != -1 ? [GroupsController.groups nameForIndex:groupIndex] : NSLocalizedString(@"No Group", "Group table row");
        }
        else if ([ident isEqualToString:@"Color"])
        {
            NSInteger groupIndex = group.groupIndex;
            return [GroupsController.groups imageForIndex:groupIndex];
        }
        else if ([ident isEqualToString:@"DL Image"])
        {
            return [NSImage imageNamed:@"DownArrowGroupTemplate"];
        }
        else if ([ident isEqualToString:@"UL Image"])
        {
            return [NSImage imageNamed:[self.fDefaults boolForKey:@"DisplayGroupRowRatio"] ? @"YingYangGroupTemplate" : @"UpArrowGroupTemplate"];
        }
        else
        {
            if ([self.fDefaults boolForKey:@"DisplayGroupRowRatio"])
            {
                return [NSString stringForRatio:group.ratio];
            }
            else
            {
                CGFloat rate = [ident isEqualToString:@"UL"] ? group.uploadRate : group.downloadRate;
                return [NSString stringForSpeed:rate];
            }
        }
    }
}

- (BOOL)outlineView:(NSOutlineView*)outlineView writeItems:(NSArray*)items toPasteboard:(NSPasteboard*)pasteboard
{
    //only allow reordering of rows if sorting by order
    if ([self.fDefaults boolForKey:@"SortByGroup"] || [[self.fDefaults stringForKey:@"Sort"] isEqualToString:SORT_ORDER])
    {
        NSMutableIndexSet* indexSet = [NSMutableIndexSet indexSet];
        for (id torrent in items)
        {
            if (![torrent isKindOfClass:[Torrent class]])
            {
                return NO;
            }

            [indexSet addIndex:[self.fTableView rowForItem:torrent]];
        }

        [pasteboard declareTypes:@[ TORRENT_TABLE_VIEW_DATA_TYPE ] owner:self];
        [pasteboard setData:[NSKeyedArchiver archivedDataWithRootObject:indexSet] forType:TORRENT_TABLE_VIEW_DATA_TYPE];
        return YES;
    }
    return NO;
}

- (NSDragOperation)outlineView:(NSOutlineView*)outlineView
                  validateDrop:(id<NSDraggingInfo>)info
                  proposedItem:(id)item
            proposedChildIndex:(NSInteger)index
{
    NSPasteboard* pasteboard = info.draggingPasteboard;
    if ([pasteboard.types containsObject:TORRENT_TABLE_VIEW_DATA_TYPE])
    {
        if ([self.fDefaults boolForKey:@"SortByGroup"])
        {
            if (!item)
            {
                return NSDragOperationNone;
            }

            if ([[self.fDefaults stringForKey:@"Sort"] isEqualToString:SORT_ORDER])
            {
                if ([item isKindOfClass:[Torrent class]])
                {
                    TorrentGroup* group = [self.fTableView parentForItem:item];
                    index = [group.torrents indexOfObject:item] + 1;
                    item = group;
                }
            }
            else
            {
                if ([item isKindOfClass:[Torrent class]])
                {
                    item = [self.fTableView parentForItem:item];
                }
                index = NSOutlineViewDropOnItemIndex;
            }
        }
        else
        {
            if (index == NSOutlineViewDropOnItemIndex)
            {
                return NSDragOperationNone;
            }

            if (item)
            {
                index = [self.fTableView rowForItem:item] + 1;
                item = nil;
            }
        }

        [self.fTableView setDropItem:item dropChildIndex:index];
        return NSDragOperationGeneric;
    }

    return NSDragOperationNone;
}

- (BOOL)outlineView:(NSOutlineView*)outlineView acceptDrop:(id<NSDraggingInfo>)info item:(id)item childIndex:(NSInteger)newRow
{
    NSPasteboard* pasteboard = info.draggingPasteboard;
    if ([pasteboard.types containsObject:TORRENT_TABLE_VIEW_DATA_TYPE])
    {
        NSIndexSet* indexes;
        if (@available(macOS 10.13, *))
        {
            indexes = [NSKeyedUnarchiver unarchivedObjectOfClass:NSIndexSet.class fromData:[pasteboard dataForType:TORRENT_TABLE_VIEW_DATA_TYPE]
                                                           error:nil];
        }
        else
        {
            indexes = [NSKeyedUnarchiver unarchiveObjectWithData:[pasteboard dataForType:TORRENT_TABLE_VIEW_DATA_TYPE]];
        }

        //get the torrents to move
        NSMutableArray* movingTorrents = [NSMutableArray arrayWithCapacity:indexes.count];
        for (NSUInteger i = indexes.firstIndex; i != NSNotFound; i = [indexes indexGreaterThanIndex:i])
        {
            Torrent* torrent = [self.fTableView itemAtRow:i];
            [movingTorrents addObject:torrent];
        }

        //change groups
        if (item)
        {
            TorrentGroup* group = (TorrentGroup*)item;
            NSInteger const groupIndex = group.groupIndex;

            for (Torrent* torrent in movingTorrents)
            {
                [torrent setGroupValue:groupIndex determinationType:TorrentDeterminationUserSpecified];
            }
        }

        //reorder queue order
        if (newRow != NSOutlineViewDropOnItemIndex)
        {
            TorrentGroup* group = (TorrentGroup*)item;
            //find torrent to place under
            NSArray* groupTorrents = group ? group.torrents : self.fDisplayedTorrents;
            Torrent* topTorrent = nil;
            for (NSInteger i = newRow - 1; i >= 0; i--)
            {
                Torrent* tempTorrent = groupTorrents[i];
                if (![movingTorrents containsObject:tempTorrent])
                {
                    topTorrent = tempTorrent;
                    break;
                }
            }

            //remove objects to reinsert
            [self.fTorrents removeObjectsInArray:movingTorrents];

            //insert objects at new location
            NSUInteger const insertIndex = topTorrent ? [self.fTorrents indexOfObject:topTorrent] + 1 : 0;
            NSIndexSet* insertIndexes = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(insertIndex, movingTorrents.count)];
            [self.fTorrents insertObjects:movingTorrents atIndexes:insertIndexes];

            //we need to make sure the queue order is updated in the Torrent object before we sort - safest to just reset all queue positions
            NSUInteger i = 0;
            for (Torrent* torrent in self.fTorrents)
            {
                torrent.queuePosition = i++;
                [torrent update];
            }

            //do the drag animation here so that the dragged torrents are the ones that are animated as moving, and not the torrents around them
            [self.fTableView beginUpdates];

            NSUInteger insertDisplayIndex = topTorrent ? [groupTorrents indexOfObject:topTorrent] + 1 : 0;

            for (Torrent* torrent in movingTorrents)
            {
                TorrentGroup* oldParent = item ? [self.fTableView parentForItem:torrent] : nil;
                NSMutableArray* oldTorrents = oldParent ? oldParent.torrents : self.fDisplayedTorrents;
                NSUInteger const oldIndex = [oldTorrents indexOfObject:torrent];

                if (item == oldParent)
                {
                    if (oldIndex < insertDisplayIndex)
                    {
                        --insertDisplayIndex;
                    }
                    [oldTorrents moveObjectAtIndex:oldIndex toIndex:insertDisplayIndex];
                }
                else
                {
                    NSAssert(item && oldParent, @"Expected to be dragging between group rows");

                    NSMutableArray* newTorrents = ((TorrentGroup*)item).torrents;
                    [newTorrents insertObject:torrent atIndex:insertDisplayIndex];
                    [oldTorrents removeObjectAtIndex:oldIndex];
                }

                [self.fTableView moveItemAtIndex:oldIndex inParent:oldParent toIndex:insertDisplayIndex inParent:item];

                ++insertDisplayIndex;
            }

            [self.fTableView endUpdates];
        }

        [self applyFilter];
    }

    return YES;
}

- (void)torrentTableViewSelectionDidChange:(NSNotification*)notification
{
    [self resetInfo];
    [self.fWindow.toolbar validateVisibleItems];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info
{
    NSPasteboard* pasteboard = info.draggingPasteboard;
    if ([pasteboard.types containsObject:NSFilenamesPboardType])
    {
        //check if any torrent files can be added
        BOOL torrent = NO;
        NSArray* files = [pasteboard propertyListForType:NSFilenamesPboardType];
        for (NSString* file in files)
        {
            if ([[NSWorkspace.sharedWorkspace typeOfFile:file error:NULL] isEqualToString:@"org.bittorrent.torrent"] ||
                [file.pathExtension caseInsensitiveCompare:@"torrent"] == NSOrderedSame)
            {
                torrent = YES;
                auto metainfo = tr_torrent_metainfo{};
                if (metainfo.parseTorrentFile(file.UTF8String))
                {
                    if (!self.fOverlayWindow)
                    {
                        self.fOverlayWindow = [[DragOverlayWindow alloc] initWithLib:self.fLib forWindow:self.fWindow];
                    }
                    [self.fOverlayWindow setTorrents:files];

                    return NSDragOperationCopy;
                }
            }
        }

        //create a torrent file if a single file
        if (!torrent && files.count == 1)
        {
            if (!self.fOverlayWindow)
            {
                self.fOverlayWindow = [[DragOverlayWindow alloc] initWithLib:self.fLib forWindow:self.fWindow];
            }
            [self.fOverlayWindow setFile:[files[0] lastPathComponent]];

            return NSDragOperationCopy;
        }
    }
    else if ([pasteboard.types containsObject:NSURLPboardType])
    {
        if (!self.fOverlayWindow)
        {
            self.fOverlayWindow = [[DragOverlayWindow alloc] initWithLib:self.fLib forWindow:self.fWindow];
        }
        [self.fOverlayWindow setURL:[NSURL URLFromPasteboard:pasteboard].relativeString];

        return NSDragOperationCopy;
    }

    return NSDragOperationNone;
}

- (void)draggingExited:(id<NSDraggingInfo>)info
{
    if (self.fOverlayWindow)
    {
        [self.fOverlayWindow fadeOut];
    }
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)info
{
    if (self.fOverlayWindow)
    {
        [self.fOverlayWindow fadeOut];
    }

    NSPasteboard* pasteboard = info.draggingPasteboard;
    if ([pasteboard.types containsObject:NSFilenamesPboardType])
    {
        BOOL torrent = NO, accept = YES;

        //create an array of files that can be opened
        NSArray* files = [pasteboard propertyListForType:NSFilenamesPboardType];
        NSMutableArray* filesToOpen = [NSMutableArray arrayWithCapacity:files.count];
        for (NSString* file in files)
        {
            if ([[NSWorkspace.sharedWorkspace typeOfFile:file error:NULL] isEqualToString:@"org.bittorrent.torrent"] ||
                [file.pathExtension caseInsensitiveCompare:@"torrent"] == NSOrderedSame)
            {
                torrent = YES;
                auto metainfo = tr_torrent_metainfo{};
                if (metainfo.parseTorrentFile(file.UTF8String))
                {
                    [filesToOpen addObject:file];
                }
            }
        }

        if (filesToOpen.count > 0)
        {
            [self application:NSApp openFiles:filesToOpen];
        }
        else
        {
            if (!torrent && files.count == 1)
            {
                [CreatorWindowController createTorrentFile:self.fLib forFile:[NSURL fileURLWithPath:files[0]]];
            }
            else
            {
                accept = NO;
            }
        }

        return accept;
    }
    else if ([pasteboard.types containsObject:NSURLPboardType])
    {
        NSURL* url;
        if ((url = [NSURL URLFromPasteboard:pasteboard]))
        {
            [self openURL:url.absoluteString];
            return YES;
        }
    }

    return NO;
}

- (void)toggleSmallView:(id)sender
{
    BOOL makeSmall = ![self.fDefaults boolForKey:@"SmallView"];
    [self.fDefaults setBool:makeSmall forKey:@"SmallView"];

    self.fTableView.usesAlternatingRowBackgroundColors = !makeSmall;

    self.fTableView.rowHeight = makeSmall ? ROW_HEIGHT_SMALL : ROW_HEIGHT_REGULAR;

    [self.fTableView beginUpdates];
    [self.fTableView
        noteHeightOfRowsWithIndexesChanged:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fTableView.numberOfRows)]];
    [self.fTableView endUpdates];

    //resize for larger min height if not set to auto size
    if (![self.fDefaults boolForKey:@"AutoSize"])
    {
        NSSize const contentSize = self.fWindow.contentView.frame.size;

        NSSize contentMinSize = self.fWindow.contentMinSize;
        contentMinSize.height = self.minWindowContentSizeAllowed;
        self.fWindow.contentMinSize = contentMinSize;

        //make sure the window already isn't too small
        if (!makeSmall && contentSize.height < contentMinSize.height)
        {
            NSRect frame = self.fWindow.frame;
            CGFloat heightChange = contentMinSize.height - contentSize.height;
            frame.size.height += heightChange;
            frame.origin.y -= heightChange;

            [self.fWindow setFrame:frame display:YES];
        }
    }
    else
    {
        [self setWindowSizeToFit];
    }
}

- (void)togglePiecesBar:(id)sender
{
    [self.fDefaults setBool:![self.fDefaults boolForKey:@"PiecesBar"] forKey:@"PiecesBar"];
    [self.fTableView togglePiecesBar];
}

- (void)toggleAvailabilityBar:(id)sender
{
    [self.fDefaults setBool:![self.fDefaults boolForKey:@"DisplayProgressBarAvailable"] forKey:@"DisplayProgressBarAvailable"];
    [self.fTableView display];
}

- (NSRect)windowFrameByAddingHeight:(CGFloat)height checkLimits:(BOOL)check
{
    NSScrollView* scrollView = self.fTableView.enclosingScrollView;

    //convert pixels to points
    NSRect windowFrame = self.fWindow.frame;
    NSSize windowSize = [scrollView convertSize:windowFrame.size fromView:nil];
    windowSize.height += height;

    if (check)
    {
        //we can't call minSize, since it might be set to the current size (auto size)
        CGFloat const minHeight = self.minWindowContentSizeAllowed +
            (NSHeight(self.fWindow.frame) - NSHeight(self.fWindow.contentView.frame)); //contentView to window

        if (windowSize.height <= minHeight)
        {
            windowSize.height = minHeight;
        }
        else
        {
            NSScreen* screen = self.fWindow.screen;
            if (screen)
            {
                NSSize maxSize = [scrollView convertSize:screen.visibleFrame.size fromView:nil];
                if (!self.fStatusBar)
                {
                    maxSize.height -= STATUS_BAR_HEIGHT;
                }
                if (!self.fFilterBar)
                {
                    maxSize.height -= FILTER_BAR_HEIGHT;
                }
                if (windowSize.height > maxSize.height)
                {
                    windowSize.height = maxSize.height;
                }
            }
        }
    }

    //convert points to pixels
    windowSize = [scrollView convertSize:windowSize toView:nil];

    windowFrame.origin.y -= (windowSize.height - windowFrame.size.height);
    windowFrame.size.height = windowSize.height;
    return windowFrame;
}

- (void)toggleStatusBar:(id)sender
{
    BOOL const show = self.fStatusBar == nil;
    [self showStatusBar:show animate:YES];
    [self.fDefaults setBool:show forKey:@"StatusBar"];
}

//doesn't save shown state
- (void)showStatusBar:(BOOL)show animate:(BOOL)animate
{
    BOOL const prevShown = self.fStatusBar != nil;
    if (show == prevShown)
    {
        return;
    }

    if (show)
    {
        self.fStatusBar = [[StatusBarController alloc] initWithLib:self.fLib];

        NSView* contentView = self.fWindow.contentView;
        NSSize const windowSize = [contentView convertSize:self.fWindow.frame.size fromView:nil];

        NSRect statusBarFrame = self.fStatusBar.view.frame;
        statusBarFrame.size.width = windowSize.width;
        self.fStatusBar.view.frame = statusBarFrame;

        [contentView addSubview:self.fStatusBar.view];
        [self.fStatusBar.view setFrameOrigin:NSMakePoint(0.0, NSMaxY(contentView.frame))];
    }

    CGFloat heightChange = self.fStatusBar.view.frame.size.height;
    if (!show)
    {
        heightChange *= -1;
    }

    //allow bar to show even if not enough room
    if (show && ![self.fDefaults boolForKey:@"AutoSize"])
    {
        NSRect frame = [self windowFrameByAddingHeight:heightChange checkLimits:NO];

        NSScreen* screen = self.fWindow.screen;
        if (screen)
        {
            CGFloat change = screen.visibleFrame.size.height - frame.size.height;
            if (change < 0.0)
            {
                frame = self.fWindow.frame;
                frame.size.height += change;
                frame.origin.y -= change;
                [self.fWindow setFrame:frame display:NO animate:NO];
            }
        }
    }

    [self updateUI];

    NSScrollView* scrollView = self.fTableView.enclosingScrollView;

    //set views to not autoresize
    NSUInteger const statsMask = self.fStatusBar.view.autoresizingMask;
    self.fStatusBar.view.autoresizingMask = NSViewNotSizable;
    NSUInteger filterMask;
    if (self.fFilterBar)
    {
        filterMask = self.fFilterBar.view.autoresizingMask;
        self.fFilterBar.view.autoresizingMask = NSViewNotSizable;
    }
    NSUInteger const scrollMask = scrollView.autoresizingMask;
    scrollView.autoresizingMask = NSViewNotSizable;

    NSRect frame = [self windowFrameByAddingHeight:heightChange checkLimits:NO];
    [self.fWindow setFrame:frame display:YES animate:animate];

    //re-enable autoresize
    self.fStatusBar.view.autoresizingMask = statsMask;
    if (self.fFilterBar)
    {
        self.fFilterBar.view.autoresizingMask = filterMask;
    }
    scrollView.autoresizingMask = scrollMask;

    if (!show)
    {
        [self.fStatusBar.view removeFromSuperviewWithoutNeedingDisplay];
        self.fStatusBar = nil;
    }

    if ([self.fDefaults boolForKey:@"AutoSize"])
    {
        [self setWindowMinMaxToCurrent];
    }
    else
    {
        //change min size
        NSSize minSize = self.fWindow.contentMinSize;
        minSize.height += heightChange;
        self.fWindow.contentMinSize = minSize;
    }
}

- (void)toggleFilterBar:(id)sender
{
    BOOL const show = self.fFilterBar == nil;

    //disable filtering when hiding (have to do before showFilterBar:animate:)
    if (!show)
    {
        [self.fFilterBar reset:NO];
    }

    [self showFilterBar:show animate:YES];
    [self.fDefaults setBool:show forKey:@"FilterBar"];
    [self.fWindow.toolbar validateVisibleItems];

    [self applyFilter]; //do even if showing to ensure tooltips are updated
}

//doesn't save shown state
- (void)showFilterBar:(BOOL)show animate:(BOOL)animate
{
    BOOL const prevShown = self.fFilterBar != nil;
    if (show == prevShown)
    {
        return;
    }

    if (show)
    {
        self.fFilterBar = [[FilterBarController alloc] init];

        NSView* contentView = self.fWindow.contentView;
        NSSize const windowSize = [contentView convertSize:self.fWindow.frame.size fromView:nil];

        NSRect filterBarFrame = self.fFilterBar.view.frame;
        filterBarFrame.size.width = windowSize.width;
        self.fFilterBar.view.frame = filterBarFrame;

        if (self.fStatusBar)
        {
            [contentView addSubview:self.fFilterBar.view positioned:NSWindowBelow relativeTo:self.fStatusBar.view];
        }
        else
        {
            [contentView addSubview:self.fFilterBar.view];
        }
        CGFloat const originY = self.fStatusBar ? NSMinY(self.fStatusBar.view.frame) : NSMaxY(contentView.frame);
        [self.fFilterBar.view setFrameOrigin:NSMakePoint(0.0, originY)];
    }
    else
    {
        [self.fWindow makeFirstResponder:self.fTableView];
    }

    CGFloat heightChange = NSHeight(self.fFilterBar.view.frame);
    if (!show)
    {
        heightChange *= -1;
    }

    //allow bar to show even if not enough room
    if (show && ![self.fDefaults boolForKey:@"AutoSize"])
    {
        NSRect frame = [self windowFrameByAddingHeight:heightChange checkLimits:NO];

        NSScreen* screen = self.fWindow.screen;
        if (screen)
        {
            CGFloat change = screen.visibleFrame.size.height - frame.size.height;
            if (change < 0.0)
            {
                frame = self.fWindow.frame;
                frame.size.height += change;
                frame.origin.y -= change;
                [self.fWindow setFrame:frame display:NO animate:NO];
            }
        }
    }

    NSScrollView* scrollView = self.fTableView.enclosingScrollView;

    //set views to not autoresize
    NSUInteger const filterMask = self.fFilterBar.view.autoresizingMask;
    NSUInteger const scrollMask = scrollView.autoresizingMask;
    self.fFilterBar.view.autoresizingMask = NSViewNotSizable;
    scrollView.autoresizingMask = NSViewNotSizable;

    NSRect const frame = [self windowFrameByAddingHeight:heightChange checkLimits:NO];
    [self.fWindow setFrame:frame display:YES animate:animate];

    //re-enable autoresize
    self.fFilterBar.view.autoresizingMask = filterMask;
    scrollView.autoresizingMask = scrollMask;

    if (!show)
    {
        [self.fFilterBar.view removeFromSuperviewWithoutNeedingDisplay];
        self.fFilterBar = nil;
    }

    if ([self.fDefaults boolForKey:@"AutoSize"])
    {
        [self setWindowMinMaxToCurrent];
    }
    else
    {
        //change min size
        NSSize minSize = self.fWindow.contentMinSize;
        minSize.height += heightChange;
        self.fWindow.contentMinSize = minSize;
    }
}

- (void)focusFilterField
{
    if (!self.fFilterBar)
    {
        [self toggleFilterBar:self];
    }
    [self.fFilterBar focusSearchField];
}

- (BOOL)acceptsPreviewPanelControl:(QLPreviewPanel*)panel
{
    return !self.fQuitting;
}

- (void)beginPreviewPanelControl:(QLPreviewPanel*)panel
{
    self.fPreviewPanel = panel;
    self.fPreviewPanel.delegate = self;
    self.fPreviewPanel.dataSource = self;
}

- (void)endPreviewPanelControl:(QLPreviewPanel*)panel
{
    self.fPreviewPanel = nil;
}

- (NSArray*)quickLookableTorrents
{
    NSArray* selectedTorrents = self.fTableView.selectedTorrents;
    NSMutableArray* qlArray = [NSMutableArray arrayWithCapacity:selectedTorrents.count];

    for (Torrent* torrent in selectedTorrents)
    {
        if ((torrent.folder || torrent.complete) && torrent.dataLocation)
        {
            [qlArray addObject:torrent];
        }
    }

    return qlArray;
}

- (NSInteger)numberOfPreviewItemsInPreviewPanel:(QLPreviewPanel*)panel
{
    if (self.fInfoController.canQuickLook)
    {
        return self.fInfoController.quickLookURLs.count;
    }
    else
    {
        return [self quickLookableTorrents].count;
    }
}

- (id<QLPreviewItem>)previewPanel:(QLPreviewPanel*)panel previewItemAtIndex:(NSInteger)index
{
    if (self.fInfoController.canQuickLook)
    {
        return self.fInfoController.quickLookURLs[index];
    }
    else
    {
        return [self quickLookableTorrents][index];
    }
}

- (BOOL)previewPanel:(QLPreviewPanel*)panel handleEvent:(NSEvent*)event
{
    /*if ([event type] == NSKeyDown)
    {
        [super keyDown: event];
        return YES;
    }*/

    return NO;
}

- (NSRect)previewPanel:(QLPreviewPanel*)panel sourceFrameOnScreenForPreviewItem:(id<QLPreviewItem>)item
{
    if (self.fInfoController.canQuickLook)
    {
        return [self.fInfoController quickLookSourceFrameForPreviewItem:item];
    }
    else
    {
        if (!self.fWindow.visible)
        {
            return NSZeroRect;
        }

        NSInteger const row = [self.fTableView rowForItem:item];
        if (row == -1)
        {
            return NSZeroRect;
        }

        NSRect frame = [self.fTableView iconRectForRow:row];

        if (!NSIntersectsRect(self.fTableView.visibleRect, frame))
        {
            return NSZeroRect;
        }

        frame.origin = [self.fTableView convertPoint:frame.origin toView:nil];
        frame = [self.fWindow convertRectToScreen:frame];
        frame.origin.y -= frame.size.height;
        return frame;
    }
}

- (void)showToolbarShare:(id)sender
{
    NSParameterAssert([sender isKindOfClass:[NSButton class]]);
    NSButton* senderButton = sender;

    NSSharingServicePicker* picker = [[NSSharingServicePicker alloc] initWithItems:ShareTorrentFileHelper.sharedHelper.shareTorrentURLs];
    picker.delegate = self;

    [picker showRelativeToRect:senderButton.bounds ofView:senderButton preferredEdge:NSMinYEdge];
}

- (id<NSSharingServiceDelegate>)sharingServicePicker:(NSSharingServicePicker*)sharingServicePicker
                           delegateForSharingService:(NSSharingService*)sharingService
{
    return self;
}

- (NSWindow*)sharingService:(NSSharingService*)sharingService
    sourceWindowForShareItems:(NSArray*)items
          sharingContentScope:(NSSharingContentScope*)sharingContentScope
{
    return self.fWindow;
}

- (ButtonToolbarItem*)standardToolbarButtonWithIdentifier:(NSString*)ident
{
    return [self toolbarButtonWithIdentifier:ident forToolbarButtonClass:[ButtonToolbarItem class]];
}

- (id)toolbarButtonWithIdentifier:(NSString*)ident forToolbarButtonClass:(Class)klass
{
    ButtonToolbarItem* item = [[klass alloc] initWithItemIdentifier:ident];

    NSButton* button = [[NSButton alloc] init];
    button.bezelStyle = NSBezelStyleTexturedRounded;
    button.stringValue = @"";

    item.view = button;

    if (@available(macOS 11.0, *))
    {
        // not needed
    }
    else
    {
        NSSize const buttonSize = NSMakeSize(36.0, 25.0);
        item.minSize = buttonSize;
        item.maxSize = buttonSize;
    }

    return item;
}

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar itemForItemIdentifier:(NSString*)ident willBeInsertedIntoToolbar:(BOOL)flag
{
    if ([ident isEqualToString:TOOLBAR_CREATE])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];

        item.label = NSLocalizedString(@"Create", "Create toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Create Torrent File", "Create toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Create torrent file", "Create toolbar item -> tooltip");
        item.image = [NSImage systemSymbol:@"doc.badge.plus" withFallback:@"ToolbarCreateTemplate"];
        item.target = self;
        item.action = @selector(createFile:);
        item.autovalidates = NO;

        return item;
    }
    else if ([ident isEqualToString:TOOLBAR_OPEN_FILE])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];

        item.label = NSLocalizedString(@"Open", "Open toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Open Torrent Files", "Open toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Open torrent files", "Open toolbar item -> tooltip");
        item.image = [NSImage systemSymbol:@"folder" withFallback:@"ToolbarOpenTemplate"];
        item.target = self;
        item.action = @selector(openShowSheet:);
        item.autovalidates = NO;

        return item;
    }
    else if ([ident isEqualToString:TOOLBAR_OPEN_WEB])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];

        item.label = NSLocalizedString(@"Open Address", "Open address toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Open Torrent Address", "Open address toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Open torrent web address", "Open address toolbar item -> tooltip");
        item.image = [NSImage systemSymbol:@"globe" withFallback:@"ToolbarOpenWebTemplate"];
        item.target = self;
        item.action = @selector(openURLShowSheet:);
        item.autovalidates = NO;

        return item;
    }
    else if ([ident isEqualToString:TOOLBAR_REMOVE])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];

        item.label = NSLocalizedString(@"Remove", "Remove toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Remove Selected", "Remove toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Remove selected transfers", "Remove toolbar item -> tooltip");
        item.image = [NSImage systemSymbol:@"nosign" withFallback:@"ToolbarRemoveTemplate"];
        item.target = self;
        item.action = @selector(removeNoDelete:);
        item.visibilityPriority = NSToolbarItemVisibilityPriorityHigh;

        return item;
    }
    else if ([ident isEqualToString:TOOLBAR_INFO])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];
        ((NSButtonCell*)((NSButton*)item.view).cell).showsStateBy = NSContentsCellMask; //blue when enabled

        item.label = NSLocalizedString(@"Inspector", "Inspector toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Toggle Inspector", "Inspector toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Toggle the torrent inspector", "Inspector toolbar item -> tooltip");
        item.image = [NSImage systemSymbol:@"info.circle" withFallback:@"ToolbarInfoTemplate"];
        item.target = self;
        item.action = @selector(showInfo:);

        return item;
    }
    else if ([ident isEqualToString:TOOLBAR_PAUSE_RESUME_ALL])
    {
        GroupToolbarItem* groupItem = [[GroupToolbarItem alloc] initWithItemIdentifier:ident];

        NSSegmentedControl* segmentedControl = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
        segmentedControl.cell = [[ToolbarSegmentedCell alloc] init];
        groupItem.view = segmentedControl;
        NSSegmentedCell* segmentedCell = (NSSegmentedCell*)segmentedControl.cell;
        segmentedControl.segmentStyle = NSSegmentStyleSeparated;

        segmentedControl.segmentCount = 2;
        segmentedCell.trackingMode = NSSegmentSwitchTrackingMomentary;

        if (@available(macOS 11.0, *))
        {
            // not needed
        }
        else
        {
            NSSize const groupSize = NSMakeSize(72.0, 25.0);
            groupItem.minSize = groupSize;
            groupItem.maxSize = groupSize;
        }

        groupItem.label = NSLocalizedString(@"Apply All", "All toolbar item -> label");
        groupItem.paletteLabel = NSLocalizedString(@"Pause / Resume All", "All toolbar item -> palette label");
        groupItem.target = self;
        groupItem.action = @selector(allToolbarClicked:);

        groupItem.identifiers = @[ TOOLBAR_PAUSE_ALL, TOOLBAR_RESUME_ALL ];

        [segmentedCell setTag:TOOLBAR_PAUSE_TAG forSegment:TOOLBAR_PAUSE_TAG];
        [segmentedControl setImage:[NSImage largeSystemSymbol:@"pause.circle.fill" withFallback:@"ToolbarPauseAllTemplate"]
                        forSegment:TOOLBAR_PAUSE_TAG];
        [segmentedCell setToolTip:NSLocalizedString(@"Pause all transfers", "All toolbar item -> tooltip") forSegment:TOOLBAR_PAUSE_TAG];

        [segmentedCell setTag:TOOLBAR_RESUME_TAG forSegment:TOOLBAR_RESUME_TAG];
        [segmentedControl setImage:[NSImage imageNamed:@"ToolbarResumeAllTemplate"] forSegment:TOOLBAR_RESUME_TAG];
        [segmentedControl setImage:[NSImage largeSystemSymbol:@"arrow.clockwise.circle.fill" withFallback:@"ToolbarResumeAllTemplate"]
                        forSegment:TOOLBAR_RESUME_TAG];
        [segmentedCell setToolTip:NSLocalizedString(@"Resume all transfers", "All toolbar item -> tooltip")
                       forSegment:TOOLBAR_RESUME_TAG];

        [groupItem createMenu:@[
            NSLocalizedString(@"Pause All", "All toolbar item -> label"),
            NSLocalizedString(@"Resume All", "All toolbar item -> label")
        ]];

        groupItem.visibilityPriority = NSToolbarItemVisibilityPriorityHigh;

        return groupItem;
    }
    else if ([ident isEqualToString:TOOLBAR_PAUSE_RESUME_SELECTED])
    {
        GroupToolbarItem* groupItem = [[GroupToolbarItem alloc] initWithItemIdentifier:ident];

        NSSegmentedControl* segmentedControl = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
        segmentedControl.cell = [[ToolbarSegmentedCell alloc] init];
        groupItem.view = segmentedControl;
        NSSegmentedCell* segmentedCell = (NSSegmentedCell*)segmentedControl.cell;

        segmentedControl.segmentCount = 2;
        segmentedCell.trackingMode = NSSegmentSwitchTrackingMomentary;

        if (@available(macOS 11.0, *))
        {
            // not needed
        }
        else
        {
            NSSize const groupSize = NSMakeSize(72.0, 25.0);
            groupItem.minSize = groupSize;
            groupItem.maxSize = groupSize;
        }

        groupItem.label = NSLocalizedString(@"Apply Selected", "Selected toolbar item -> label");
        groupItem.paletteLabel = NSLocalizedString(@"Pause / Resume Selected", "Selected toolbar item -> palette label");
        groupItem.target = self;
        groupItem.action = @selector(selectedToolbarClicked:);

        groupItem.identifiers = @[ TOOLBAR_PAUSE_SELECTED, TOOLBAR_RESUME_SELECTED ];

        [segmentedCell setTag:TOOLBAR_PAUSE_TAG forSegment:TOOLBAR_PAUSE_TAG];
        [segmentedControl setImage:[NSImage largeSystemSymbol:@"pause" withFallback:@"ToolbarPauseSelectedTemplate"]
                        forSegment:TOOLBAR_PAUSE_TAG];
        [segmentedCell setToolTip:NSLocalizedString(@"Pause selected transfers", "Selected toolbar item -> tooltip")
                       forSegment:TOOLBAR_PAUSE_TAG];

        [segmentedCell setTag:TOOLBAR_RESUME_TAG forSegment:TOOLBAR_RESUME_TAG];
        [segmentedControl setImage:[NSImage systemSymbol:@"arrow.clockwise" withFallback:@"ToolbarResumeSelectedTemplate"]
                        forSegment:TOOLBAR_RESUME_TAG];
        [segmentedCell setToolTip:NSLocalizedString(@"Resume selected transfers", "Selected toolbar item -> tooltip")
                       forSegment:TOOLBAR_RESUME_TAG];

        [groupItem createMenu:@[
            NSLocalizedString(@"Pause Selected", "Selected toolbar item -> label"),
            NSLocalizedString(@"Resume Selected", "Selected toolbar item -> label")
        ]];

        groupItem.visibilityPriority = NSToolbarItemVisibilityPriorityHigh;

        return groupItem;
    }
    else if ([ident isEqualToString:TOOLBAR_FILTER])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];
        ((NSButtonCell*)((NSButton*)item.view).cell).showsStateBy = NSContentsCellMask; //blue when enabled

        item.label = NSLocalizedString(@"Filter", "Filter toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Toggle Filter", "Filter toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Toggle the filter bar", "Filter toolbar item -> tooltip");
        item.image = [NSImage systemSymbol:@"magnifyingglass" withFallback:@"ToolbarFilterTemplate"];
        item.target = self;
        item.action = @selector(toggleFilterBar:);

        return item;
    }
    else if ([ident isEqualToString:TOOLBAR_QUICKLOOK])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];
        ((NSButtonCell*)((NSButton*)item.view).cell).showsStateBy = NSContentsCellMask; //blue when enabled

        item.label = NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> tooltip");
        item.image = [NSImage imageNamed:NSImageNameQuickLookTemplate];
        item.target = self;
        item.action = @selector(toggleQuickLook:);
        item.visibilityPriority = NSToolbarItemVisibilityPriorityLow;

        return item;
    }
    else if ([ident isEqualToString:TOOLBAR_SHARE])
    {
        ShareToolbarItem* item = [self toolbarButtonWithIdentifier:ident forToolbarButtonClass:[ShareToolbarItem class]];

        item.label = NSLocalizedString(@"Share", "Share toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Share", "Share toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Share torrent file", "Share toolbar item -> tooltip");
        item.image = [NSImage imageNamed:NSImageNameShareTemplate];
        item.visibilityPriority = NSToolbarItemVisibilityPriorityLow;

        NSButton* itemButton = (NSButton*)item.view;
        itemButton.target = self;
        itemButton.action = @selector(showToolbarShare:);
        [itemButton sendActionOn:NSEventMaskLeftMouseDown];

        return item;
    }
    else
    {
        return nil;
    }
}

- (void)allToolbarClicked:(id)sender
{
    NSInteger tagValue = [sender isKindOfClass:[NSSegmentedControl class]] ?
        [(NSSegmentedCell*)[sender cell] tagForSegment:[sender selectedSegment]] :
        ((NSControl*)sender).tag;
    switch (tagValue)
    {
    case TOOLBAR_PAUSE_TAG:
        [self stopAllTorrents:sender];
        break;
    case TOOLBAR_RESUME_TAG:
        [self resumeAllTorrents:sender];
        break;
    }
}

- (void)selectedToolbarClicked:(id)sender
{
    NSInteger tagValue = [sender isKindOfClass:[NSSegmentedControl class]] ?
        [(NSSegmentedCell*)[sender cell] tagForSegment:[sender selectedSegment]] :
        ((NSControl*)sender).tag;
    switch (tagValue)
    {
    case TOOLBAR_PAUSE_TAG:
        [self stopSelectedTorrents:sender];
        break;
    case TOOLBAR_RESUME_TAG:
        [self resumeSelectedTorrents:sender];
        break;
    }
}

- (NSArray*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar
{
    return @[
        TOOLBAR_CREATE,
        TOOLBAR_OPEN_FILE,
        TOOLBAR_OPEN_WEB,
        TOOLBAR_REMOVE,
        TOOLBAR_PAUSE_RESUME_SELECTED,
        TOOLBAR_PAUSE_RESUME_ALL,
        TOOLBAR_SHARE,
        TOOLBAR_QUICKLOOK,
        TOOLBAR_FILTER,
        TOOLBAR_INFO,
        NSToolbarSpaceItemIdentifier,
        NSToolbarFlexibleSpaceItemIdentifier
    ];
}

- (NSArray*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar
{
    return @[
        TOOLBAR_CREATE,
        TOOLBAR_OPEN_FILE,
        TOOLBAR_REMOVE,
        NSToolbarSpaceItemIdentifier,
        TOOLBAR_PAUSE_RESUME_ALL,
        NSToolbarFlexibleSpaceItemIdentifier,
        TOOLBAR_SHARE,
        TOOLBAR_QUICKLOOK,
        TOOLBAR_FILTER,
        TOOLBAR_INFO
    ];
}

- (BOOL)validateToolbarItem:(NSToolbarItem*)toolbarItem
{
    NSString* ident = toolbarItem.itemIdentifier;

    //enable remove item
    if ([ident isEqualToString:TOOLBAR_REMOVE])
    {
        return self.fTableView.numberOfSelectedRows > 0;
    }

    //enable pause all item
    if ([ident isEqualToString:TOOLBAR_PAUSE_ALL])
    {
        for (Torrent* torrent in self.fTorrents)
        {
            if (torrent.active || torrent.waitingToStart)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable resume all item
    if ([ident isEqualToString:TOOLBAR_RESUME_ALL])
    {
        for (Torrent* torrent in self.fTorrents)
        {
            if (!torrent.active && !torrent.waitingToStart && !torrent.finishedSeeding)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable pause item
    if ([ident isEqualToString:TOOLBAR_PAUSE_SELECTED])
    {
        for (Torrent* torrent in self.fTableView.selectedTorrents)
        {
            if (torrent.active || torrent.waitingToStart)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable resume item
    if ([ident isEqualToString:TOOLBAR_RESUME_SELECTED])
    {
        for (Torrent* torrent in self.fTableView.selectedTorrents)
        {
            if (!torrent.active && !torrent.waitingToStart)
            {
                return YES;
            }
        }
        return NO;
    }

    //set info item
    if ([ident isEqualToString:TOOLBAR_INFO])
    {
        ((NSButton*)toolbarItem.view).state = self.fInfoController.window.visible;
        return YES;
    }

    //set filter item
    if ([ident isEqualToString:TOOLBAR_FILTER])
    {
        ((NSButton*)toolbarItem.view).state = self.fFilterBar != nil;
        return YES;
    }

    //set quick look item
    if ([ident isEqualToString:TOOLBAR_QUICKLOOK])
    {
        ((NSButton*)toolbarItem.view).state = [QLPreviewPanel sharedPreviewPanelExists] && [QLPreviewPanel sharedPreviewPanel].visible;
        return YES;
    }

    //enable share item
    if ([ident isEqualToString:TOOLBAR_SHARE])
    {
        return self.fTableView.numberOfSelectedRows > 0;
    }

    return YES;
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL action = menuItem.action;

    if (action == @selector(toggleSpeedLimit:))
    {
        menuItem.state = [self.fDefaults boolForKey:@"SpeedLimit"] ? NSControlStateValueOn : NSControlStateValueOff;
        return YES;
    }

    //only enable some items if it is in a context menu or the window is useable
    BOOL canUseTable = self.fWindow.keyWindow || menuItem.menu.supermenu != NSApp.mainMenu;

    //enable open items
    if (action == @selector(openShowSheet:) || action == @selector(openURLShowSheet:))
    {
        return self.fWindow.attachedSheet == nil;
    }

    //enable sort options
    if (action == @selector(setSort:))
    {
        NSString* sortType;
        switch (menuItem.tag)
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
            sortType = SORT_ORDER;
        }

        menuItem.state = [sortType isEqualToString:[self.fDefaults stringForKey:@"Sort"]] ? NSControlStateValueOn : NSControlStateValueOff;
        return self.fWindow.visible;
    }

    if (action == @selector(setGroup:))
    {
        BOOL checked = NO;

        NSInteger index = menuItem.tag;
        for (Torrent* torrent in self.fTableView.selectedTorrents)
        {
            if (index == torrent.groupValue)
            {
                checked = YES;
                break;
            }
        }

        menuItem.state = checked ? NSControlStateValueOn : NSControlStateValueOff;
        return canUseTable && self.fTableView.numberOfSelectedRows > 0;
    }

    if (action == @selector(toggleSmallView:))
    {
        menuItem.state = [self.fDefaults boolForKey:@"SmallView"] ? NSControlStateValueOn : NSControlStateValueOff;
        return self.fWindow.visible;
    }

    if (action == @selector(togglePiecesBar:))
    {
        menuItem.state = [self.fDefaults boolForKey:@"PiecesBar"] ? NSControlStateValueOn : NSControlStateValueOff;
        return self.fWindow.visible;
    }

    if (action == @selector(toggleAvailabilityBar:))
    {
        menuItem.state = [self.fDefaults boolForKey:@"DisplayProgressBarAvailable"] ? NSControlStateValueOn : NSControlStateValueOff;
        return self.fWindow.visible;
    }

    //enable show info
    if (action == @selector(showInfo:))
    {
        NSString* title = self.fInfoController.window.visible ? NSLocalizedString(@"Hide Inspector", "View menu -> Inspector") :
                                                                NSLocalizedString(@"Show Inspector", "View menu -> Inspector");
        menuItem.title = title;

        return YES;
    }

    //enable prev/next inspector tab
    if (action == @selector(setInfoTab:))
    {
        return self.fInfoController.window.visible;
    }

    //enable toggle status bar
    if (action == @selector(toggleStatusBar:))
    {
        NSString* title = !self.fStatusBar ? NSLocalizedString(@"Show Status Bar", "View menu -> Status Bar") :
                                             NSLocalizedString(@"Hide Status Bar", "View menu -> Status Bar");
        menuItem.title = title;

        return self.fWindow.visible;
    }

    //enable toggle filter bar
    if (action == @selector(toggleFilterBar:))
    {
        NSString* title = !self.fFilterBar ? NSLocalizedString(@"Show Filter Bar", "View menu -> Filter Bar") :
                                             NSLocalizedString(@"Hide Filter Bar", "View menu -> Filter Bar");
        menuItem.title = title;

        return self.fWindow.visible;
    }

    //enable prev/next filter button
    if (action == @selector(switchFilter:))
    {
        return self.fWindow.visible && self.fFilterBar;
    }

    //enable reveal in finder
    if (action == @selector(revealFile:))
    {
        return canUseTable && self.fTableView.numberOfSelectedRows > 0;
    }

    //enable renaming file/folder
    if (action == @selector(renameSelected:))
    {
        return canUseTable && self.fTableView.numberOfSelectedRows == 1;
    }

    //enable remove items
    if (action == @selector(removeNoDelete:) || action == @selector(removeDeleteData:))
    {
        BOOL warning = NO;

        for (Torrent* torrent in self.fTableView.selectedTorrents)
        {
            if (torrent.active)
            {
                if ([self.fDefaults boolForKey:@"CheckRemoveDownloading"] ? !torrent.seeding : YES)
                {
                    warning = YES;
                    break;
                }
            }
        }

        //append or remove ellipsis when needed
        NSString *title = menuItem.title, *ellipsis = NSString.ellipsis;
        if (warning && [self.fDefaults boolForKey:@"CheckRemove"])
        {
            if (![title hasSuffix:ellipsis])
            {
                menuItem.title = [title stringByAppendingEllipsis];
            }
        }
        else
        {
            if ([title hasSuffix:ellipsis])
            {
                menuItem.title = [title substringToIndex:[title rangeOfString:ellipsis].location];
            }
        }

        return canUseTable && self.fTableView.numberOfSelectedRows > 0;
    }

    //remove all completed transfers item
    if (action == @selector(clearCompleted:))
    {
        //append or remove ellipsis when needed
        NSString *title = menuItem.title, *ellipsis = NSString.ellipsis;
        if ([self.fDefaults boolForKey:@"WarningRemoveCompleted"])
        {
            if (![title hasSuffix:ellipsis])
            {
                menuItem.title = [title stringByAppendingEllipsis];
            }
        }
        else
        {
            if ([title hasSuffix:ellipsis])
            {
                menuItem.title = [title substringToIndex:[title rangeOfString:ellipsis].location];
            }
        }

        for (Torrent* torrent in self.fTorrents)
        {
            if (torrent.finishedSeeding)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable pause all item
    if (action == @selector(stopAllTorrents:))
    {
        for (Torrent* torrent in self.fTorrents)
        {
            if (torrent.active || torrent.waitingToStart)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable resume all item
    if (action == @selector(resumeAllTorrents:))
    {
        for (Torrent* torrent in self.fTorrents)
        {
            if (!torrent.active && !torrent.waitingToStart && !torrent.finishedSeeding)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable resume all waiting item
    if (action == @selector(resumeWaitingTorrents:))
    {
        if (![self.fDefaults boolForKey:@"Queue"] && ![self.fDefaults boolForKey:@"QueueSeed"])
        {
            return NO;
        }

        for (Torrent* torrent in self.fTorrents)
        {
            if (torrent.waitingToStart)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable resume selected waiting item
    if (action == @selector(resumeSelectedTorrentsNoWait:))
    {
        if (!canUseTable)
        {
            return NO;
        }

        for (Torrent* torrent in self.fTableView.selectedTorrents)
        {
            if (!torrent.active)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable pause item
    if (action == @selector(stopSelectedTorrents:))
    {
        if (!canUseTable)
        {
            return NO;
        }

        for (Torrent* torrent in self.fTableView.selectedTorrents)
        {
            if (torrent.active || torrent.waitingToStart)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable resume item
    if (action == @selector(resumeSelectedTorrents:))
    {
        if (!canUseTable)
        {
            return NO;
        }

        for (Torrent* torrent in self.fTableView.selectedTorrents)
        {
            if (!torrent.active && !torrent.waitingToStart)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable manual announce item
    if (action == @selector(announceSelectedTorrents:))
    {
        if (!canUseTable)
        {
            return NO;
        }

        for (Torrent* torrent in self.fTableView.selectedTorrents)
        {
            if (torrent.canManualAnnounce)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable reset cache item
    if (action == @selector(verifySelectedTorrents:))
    {
        if (!canUseTable)
        {
            return NO;
        }

        for (Torrent* torrent in self.fTableView.selectedTorrents)
        {
            if (!torrent.magnet)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable move torrent file item
    if (action == @selector(moveDataFilesSelected:))
    {
        return canUseTable && self.fTableView.numberOfSelectedRows > 0;
    }

    //enable copy torrent file item
    if (action == @selector(copyTorrentFiles:))
    {
        if (!canUseTable)
        {
            return NO;
        }

        for (Torrent* torrent in self.fTableView.selectedTorrents)
        {
            if (!torrent.magnet)
            {
                return YES;
            }
        }
        return NO;
    }

    //enable copy torrent file item
    if (action == @selector(copyMagnetLinks:))
    {
        return canUseTable && self.fTableView.numberOfSelectedRows > 0;
    }

    //enable reverse sort item
    if (action == @selector(setSortReverse:))
    {
        BOOL const isReverse = menuItem.tag == SORT_DESC_TAG;
        menuItem.state = (isReverse == [self.fDefaults boolForKey:@"SortReverse"]) ? NSControlStateValueOn : NSControlStateValueOff;
        return ![[self.fDefaults stringForKey:@"Sort"] isEqualToString:SORT_ORDER];
    }

    //enable group sort item
    if (action == @selector(setSortByGroup:))
    {
        menuItem.state = [self.fDefaults boolForKey:@"SortByGroup"] ? NSControlStateValueOn : NSControlStateValueOff;
        return YES;
    }

    if (action == @selector(toggleQuickLook:))
    {
        BOOL const visible = [QLPreviewPanel sharedPreviewPanelExists] && [QLPreviewPanel sharedPreviewPanel].visible;
        //text consistent with Finder
        NSString* title = !visible ? NSLocalizedString(@"Quick Look", "View menu -> Quick Look") :
                                     NSLocalizedString(@"Close Quick Look", "View menu -> Quick Look");
        menuItem.title = title;

        return YES;
    }

    return YES;
}

- (void)sleepCallback:(natural_t)messageType argument:(void*)messageArgument
{
    switch (messageType)
    {
    case kIOMessageSystemWillSleep:
        {
            //stop all transfers (since some are active) before going to sleep and remember to resume when we wake up
            BOOL anyActive = NO;
            for (Torrent* torrent in self.fTorrents)
            {
                if (torrent.active)
                {
                    anyActive = YES;
                }
                [torrent sleep]; //have to call on all, regardless if they are active
            }

            //if there are any running transfers, wait 15 seconds for them to stop
            if (anyActive)
            {
                sleep(15);
            }

            IOAllowPowerChange(self.fRootPort, (long)messageArgument);
            break;
        }

    case kIOMessageCanSystemSleep:
        if ([self.fDefaults boolForKey:@"SleepPrevent"])
        {
            //prevent idle sleep unless no torrents are active
            for (Torrent* torrent in self.fTorrents)
            {
                if (torrent.active && !torrent.stalled && !torrent.error)
                {
                    IOCancelPowerChange(self.fRootPort, (long)messageArgument);
                    return;
                }
            }
        }

        IOAllowPowerChange(self.fRootPort, (long)messageArgument);
        break;

    case kIOMessageSystemHasPoweredOn:
        //resume sleeping transfers after we wake up
        for (Torrent* torrent in self.fTorrents)
        {
            [torrent wakeUp];
        }
        break;
    }
}

- (NSMenu*)applicationDockMenu:(NSApplication*)sender
{
    if (self.fQuitting)
    {
        return nil;
    }

    NSUInteger seeding = 0, downloading = 0;
    for (Torrent* torrent in self.fTorrents)
    {
        if (torrent.seeding)
        {
            seeding++;
        }
        else if (torrent.active)
        {
            downloading++;
        }
    }

    NSMenu* menu = [[NSMenu alloc] init];

    if (seeding > 0)
    {
        NSString* title = [NSString stringWithFormat:NSLocalizedString(@"%lu Seeding", "Dock item - Seeding"), seeding];
        [menu addItemWithTitle:title action:nil keyEquivalent:@""];
    }

    if (downloading > 0)
    {
        NSString* title = [NSString stringWithFormat:NSLocalizedString(@"%lu Downloading", "Dock item - Downloading"), downloading];
        [menu addItemWithTitle:title action:nil keyEquivalent:@""];
    }

    if (seeding > 0 || downloading > 0)
    {
        [menu addItem:[NSMenuItem separatorItem]];
    }

    [menu addItemWithTitle:NSLocalizedString(@"Pause All", "Dock item") action:@selector(stopAllTorrents:) keyEquivalent:@""];
    [menu addItemWithTitle:NSLocalizedString(@"Resume All", "Dock item") action:@selector(resumeAllTorrents:) keyEquivalent:@""];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:NSLocalizedString(@"Speed Limit", "Dock item") action:@selector(toggleSpeedLimit:) keyEquivalent:@""];

    return menu;
}

- (NSRect)windowWillUseStandardFrame:(NSWindow*)window defaultFrame:(NSRect)defaultFrame
{
    //if auto size is enabled, the current frame shouldn't need to change
    NSRect frame = [self.fDefaults boolForKey:@"AutoSize"] ? window.frame : self.sizedWindowFrame;

    frame.size.width = [self.fDefaults boolForKey:@"SmallView"] ? self.fWindow.minSize.width : WINDOW_REGULAR_WIDTH;
    return frame;
}

- (void)setWindowSizeToFit
{
    if ([self.fDefaults boolForKey:@"AutoSize"])
    {
        NSScrollView* scrollView = self.fTableView.enclosingScrollView;

        scrollView.hasVerticalScroller = NO;
        [self.fWindow setFrame:self.sizedWindowFrame display:YES animate:YES];
        scrollView.hasVerticalScroller = YES;

        [self setWindowMinMaxToCurrent];
    }
}

- (NSRect)sizedWindowFrame
{
    NSUInteger groups = (self.fDisplayedTorrents.count > 0 && ![self.fDisplayedTorrents[0] isKindOfClass:[Torrent class]]) ?
        self.fDisplayedTorrents.count :
        0;

    CGFloat heightChange = (GROUP_SEPARATOR_HEIGHT + self.fTableView.intercellSpacing.height) * groups +
        (self.fTableView.rowHeight + self.fTableView.intercellSpacing.height) * (self.fTableView.numberOfRows - groups) -
        NSHeight(self.fTableView.enclosingScrollView.frame);

    return [self windowFrameByAddingHeight:heightChange checkLimits:YES];
}

- (void)updateForAutoSize
{
    if ([self.fDefaults boolForKey:@"AutoSize"])
    {
        [self setWindowSizeToFit];
    }
    else
    {
        NSSize contentMinSize = self.fWindow.contentMinSize;
        contentMinSize.height = self.minWindowContentSizeAllowed;

        self.fWindow.contentMinSize = contentMinSize;

        NSSize contentMaxSize = self.fWindow.contentMaxSize;
        contentMaxSize.height = FLT_MAX;
        self.fWindow.contentMaxSize = contentMaxSize;
    }
}

- (void)setWindowMinMaxToCurrent
{
    CGFloat const height = NSHeight(self.fWindow.contentView.frame);

    NSSize minSize = self.fWindow.contentMinSize, maxSize = self.fWindow.contentMaxSize;
    minSize.height = height;
    maxSize.height = height;

    self.fWindow.contentMinSize = minSize;
    self.fWindow.contentMaxSize = maxSize;
}

- (CGFloat)minWindowContentSizeAllowed
{
    CGFloat contentMinHeight = NSHeight(self.fWindow.contentView.frame) - NSHeight(self.fTableView.enclosingScrollView.frame) +
        self.fTableView.rowHeight + self.fTableView.intercellSpacing.height;
    return contentMinHeight;
}

- (void)updateForExpandCollapse
{
    [self setWindowSizeToFit];
    [self setBottomCountText:YES];
}

- (void)showMainWindow:(id)sender
{
    [self.fWindow makeKeyAndOrderFront:nil];
}

- (void)windowDidBecomeMain:(NSNotification*)notification
{
    [self.fBadger clearCompleted];
    [self updateUI];
}

- (void)applicationWillUnhide:(NSNotification*)notification
{
    [self updateUI];
}

- (void)toggleQuickLook:(id)sender
{
    if ([QLPreviewPanel sharedPreviewPanel].visible)
    {
        [[QLPreviewPanel sharedPreviewPanel] orderOut:nil];
    }
    else
    {
        [[QLPreviewPanel sharedPreviewPanel] makeKeyAndOrderFront:nil];
    }
}

- (void)linkHomepage:(id)sender
{
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:WEBSITE_URL]];
}

- (void)linkForums:(id)sender
{
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:FORUM_URL]];
}

- (void)linkGitHub:(id)sender
{
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:GITHUB_URL]];
}

- (void)linkDonate:(id)sender
{
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:DONATE_URL]];
}

- (void)updaterWillRelaunchApplication:(SUUpdater*)updater
{
    self.fQuitRequested = YES;
}

- (void)rpcCallback:(tr_rpc_callback_type)type forTorrentStruct:(struct tr_torrent*)torrentStruct
{
    @autoreleasepool
    {
        //get the torrent
        __block Torrent* torrent = nil;
        if (torrentStruct != NULL && (type != TR_RPC_TORRENT_ADDED && type != TR_RPC_SESSION_CHANGED && type != TR_RPC_SESSION_CLOSE))
        {
            [self.fTorrents enumerateObjectsWithOptions:NSEnumerationConcurrent
                                             usingBlock:^(Torrent* checkTorrent, NSUInteger idx, BOOL* stop) {
                                                 if (torrentStruct == checkTorrent.torrentStruct)
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
                [self rpcAddTorrentStruct:torrentStruct];
                break;

            case TR_RPC_TORRENT_STARTED:
            case TR_RPC_TORRENT_STOPPED:
                [self rpcStartedStoppedTorrent:torrent];
                break;

            case TR_RPC_TORRENT_REMOVING:
                [self rpcRemoveTorrent:torrent deleteData:NO];
                break;

            case TR_RPC_TORRENT_TRASHING:
                [self rpcRemoveTorrent:torrent deleteData:YES];
                break;

            case TR_RPC_TORRENT_CHANGED:
                [self rpcChangedTorrent:torrent];
                break;

            case TR_RPC_TORRENT_MOVED:
                [self rpcMovedTorrent:torrent];
                break;

            case TR_RPC_SESSION_QUEUE_POSITIONS_CHANGED:
                [self rpcUpdateQueue];
                break;

            case TR_RPC_SESSION_CHANGED:
                [_prefsController rpcUpdatePrefs];
                break;

            case TR_RPC_SESSION_CLOSE:
                self.fQuitRequested = YES;
                [NSApp terminate:self];
                break;

            default:
                NSAssert1(NO, @"Unknown RPC command received: %d", type);
            }
        });
    }
}

- (void)rpcAddTorrentStruct:(struct tr_torrent*)torrentStruct
{
    NSString* location = nil;
    if (tr_torrentGetDownloadDir(torrentStruct) != NULL)
    {
        location = @(tr_torrentGetDownloadDir(torrentStruct));
    }

    Torrent* torrent = [[Torrent alloc] initWithTorrentStruct:torrentStruct location:location lib:self.fLib];

    //change the location if the group calls for it (this has to wait until after the torrent is created)
    if ([GroupsController.groups usesCustomDownloadLocationForIndex:torrent.groupValue])
    {
        location = [GroupsController.groups customDownloadLocationForIndex:torrent.groupValue];
        [torrent changeDownloadFolderBeforeUsing:location determinationType:TorrentDeterminationAutomatic];
    }

    [torrent update];
    [self.fTorrents addObject:torrent];

    if (!self.fAddingTransfers)
    {
        self.fAddingTransfers = [[NSMutableSet alloc] init];
    }
    [self.fAddingTransfers addObject:torrent];

    [self fullUpdateUI];
}

- (void)rpcRemoveTorrent:(Torrent*)torrent deleteData:(BOOL)deleteData
{
    [self confirmRemoveTorrents:@[ torrent ] deleteData:deleteData];
}

- (void)rpcStartedStoppedTorrent:(Torrent*)torrent
{
    [torrent update];

    [self updateUI];
    [self applyFilter];
    [self updateTorrentHistory];
}

- (void)rpcChangedTorrent:(Torrent*)torrent
{
    [torrent update];

    if ([self.fTableView.selectedTorrents containsObject:torrent])
    {
        [self.fInfoController updateInfoStats]; //this will reload the file table
        [self.fInfoController updateOptions];
    }
}

- (void)rpcMovedTorrent:(Torrent*)torrent
{
    [torrent update];
    [torrent updateTimeMachineExclude];

    if ([self.fTableView.selectedTorrents containsObject:torrent])
    {
        [self.fInfoController updateInfoStats];
    }
}

- (void)rpcUpdateQueue
{
    for (Torrent* torrent in self.fTorrents)
    {
        [torrent update];
    }

    NSSortDescriptor* descriptor = [NSSortDescriptor sortDescriptorWithKey:@"queuePosition" ascending:YES];
    NSArray* descriptors = @[ descriptor ];
    [self.fTorrents sortUsingDescriptors:descriptors];

    [self sortTorrentsAndIncludeQueueOrder:YES];
}

@end
