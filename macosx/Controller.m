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

#import <IOKit/IOMessage.h>

#import "Controller.h"
#import "Torrent.h"
#import "TorrentCell.h"
#import "TorrentTableView.h"
#import "CreatorWindowController.h"
#import "StatsWindowController.h"
#import "GroupsWindowController.h"
#import "AboutWindowController.h"
#import "ButtonToolbarItem.h"
#import "GroupToolbarItem.h"
#import "ToolbarSegmentedCell.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "NSMenuAdditions.h"
#import "UKKQueue.h"
#import "ExpandedPathToPathTransformer.h"
#import "ExpandedPathToIconTransformer.h"
#import "SpeedLimitToTurtleIconTransformer.h"

#import <Sparkle/Sparkle.h>

#define TOOLBAR_CREATE                  @"Toolbar Create"
#define TOOLBAR_OPEN                    @"Toolbar Open"
#define TOOLBAR_REMOVE                  @"Toolbar Remove"
#define TOOLBAR_INFO                    @"Toolbar Info"
#define TOOLBAR_PAUSE_ALL               @"Toolbar Pause All"
#define TOOLBAR_RESUME_ALL              @"Toolbar Resume All"
#define TOOLBAR_PAUSE_RESUME_ALL        @"Toolbar Pause / Resume All"
#define TOOLBAR_PAUSE_SELECTED          @"Toolbar Pause Selected"
#define TOOLBAR_RESUME_SELECTED         @"Toolbar Resume Selected"
#define TOOLBAR_PAUSE_RESUME_SELECTED   @"Toolbar Pause / Resume Selected"
#define TOOLBAR_FILTER                  @"Toolbar Toggle Filter"

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

typedef enum
{
    SORT_ORDER_TAG = 0,
    SORT_DATE_TAG = 1,
    SORT_NAME_TAG = 2,
    SORT_PROGRESS_TAG = 3,
    SORT_STATE_TAG = 4,
    SORT_TRACKER_TAG = 5,
    SORT_ACTIVITY_TAG = 6
} sortTag;

#define FILTER_NONE     @"None"
#define FILTER_ACTIVE   @"Active"
#define FILTER_DOWNLOAD @"Download"
#define FILTER_SEED     @"Seed"
#define FILTER_PAUSE    @"Pause"

#define FILTER_TYPE_NAME    @"Name"
#define FILTER_TYPE_TRACKER @"Tracker"

#define FILTER_TYPE_TAG_NAME    401
#define FILTER_TYPE_TAG_TRACKER 402

#define GROUP_FILTER_ALL_TAG    -2

#define STATUS_RATIO_TOTAL      @"RatioTotal"
#define STATUS_RATIO_SESSION    @"RatioSession"
#define STATUS_TRANSFER_TOTAL   @"TransferTotal"
#define STATUS_TRANSFER_SESSION @"TransferSession"

typedef enum
{
    STATUS_RATIO_TOTAL_TAG = 0,
    STATUS_RATIO_SESSION_TAG = 1,
    STATUS_TRANSFER_TOTAL_TAG = 2,
    STATUS_TRANSFER_SESSION_TAG = 3
} statusTag;

#define GROWL_DOWNLOAD_COMPLETE @"Download Complete"
#define GROWL_SEEDING_COMPLETE  @"Seeding Complete"
#define GROWL_AUTO_ADD          @"Torrent Auto Added"
#define GROWL_AUTO_SPEED_LIMIT  @"Speed Limit Auto Changed"

#define TORRENT_TABLE_VIEW_DATA_TYPE    @"TorrentTableViewDataType"

#define ROW_HEIGHT_REGULAR      62.0
#define ROW_HEIGHT_SMALL        38.0
#define WINDOW_REGULAR_WIDTH    468.0

#define SEARCH_FILTER_MIN_WIDTH 48.0
#define SEARCH_FILTER_MAX_WIDTH 95.0

#define UPDATE_UI_SECONDS           1.0
#define AUTO_SPEED_LIMIT_SECONDS    5.0

#define DOCK_SEEDING_TAG        101
#define DOCK_DOWNLOADING_TAG    102

#define SUPPORT_FOLDER  @"/Library/Application Support/Transmission/Transfers.plist"

#define WEBSITE_URL @"http://www.transmissionbt.com/"
#define FORUM_URL   @"http://forum.transmissionbt.com/"
#define DONATE_URL  @"http://www.transmissionbt.com/donate.php"

void sleepCallBack(void * controller, io_service_t y, natural_t messageType, void * messageArgument)
{
    [(Controller *)controller sleepCallBack: messageType argument: messageArgument];
}

@implementation Controller

+ (void) initialize
{
    //make sure another Transmission.app isn't running already
    NSString * bundleIdentifier = [[NSBundle mainBundle] bundleIdentifier];
    int processIdentifier = [[NSProcessInfo processInfo] processIdentifier];

    NSDictionary * dic;
    NSEnumerator * enumerator = [[[NSWorkspace sharedWorkspace] launchedApplications] objectEnumerator];
    while ((dic = [enumerator nextObject]))
    {
        if ([[dic objectForKey: @"NSApplicationBundleIdentifier"] isEqualToString: bundleIdentifier]
            && [[dic objectForKey: @"NSApplicationProcessIdentifier"] intValue] != processIdentifier)
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle: NSLocalizedString(@"Quit", "Transmission already running alert -> button")];
            [alert setMessageText: NSLocalizedString(@"Transmission is already running.",
                                                    "Transmission already running alert -> title")];
            [alert setInformativeText: NSLocalizedString(@"There is already a copy of Transmission running. "
                "This copy cannot be opened until that instance is quit.", "Transmission already running alert -> message")];
            [alert setAlertStyle: NSWarningAlertStyle];
            
            [alert runModal];
            [alert release];
            
            //kill ourselves right away
            exit(0);
        }
    }
    
    [[NSUserDefaults standardUserDefaults] registerDefaults: [NSDictionary dictionaryWithContentsOfFile:
        [[NSBundle mainBundle] pathForResource: @"Defaults" ofType: @"plist"]]];
    
    //set custom value transformers
    ExpandedPathToPathTransformer * pathTransformer =
                        [[[ExpandedPathToPathTransformer alloc] init] autorelease];
    [NSValueTransformer setValueTransformer: pathTransformer forName: @"ExpandedPathToPathTransformer"];
    
    ExpandedPathToIconTransformer * iconTransformer =
                        [[[ExpandedPathToIconTransformer alloc] init] autorelease];
    [NSValueTransformer setValueTransformer: iconTransformer forName: @"ExpandedPathToIconTransformer"];
    
    SpeedLimitToTurtleIconTransformer * speedLimitIconTransformer =
                        [[[SpeedLimitToTurtleIconTransformer alloc] init] autorelease];
    [NSValueTransformer setValueTransformer: speedLimitIconTransformer forName: @"SpeedLimitToTurtleIconTransformer"];
}

- (id) init
{
    if ((self = [super init]))
    {
        fDefaults = [NSUserDefaults standardUserDefaults];
        
        fLib = tr_initFull("macosx",
                        [fDefaults boolForKey: @"PEXGlobal"],
                        [fDefaults boolForKey: @"NatTraversal"],
                        [fDefaults integerForKey: @"BindPort"],
                        TR_ENCRYPTION_PREFERRED, /* reset in prefs */
                        FALSE, /* reset in prefs */
                        -1, /* reset in prefs */
                        FALSE, /* reset in prefs */
                        -1, /* reset in prefs */
                        [fDefaults integerForKey: @"PeersTotal"],
                        [fDefaults integerForKey: @"MessageLevel"],
                        YES);
        
        [NSApp setDelegate: self];
        
        fTorrents = [[NSMutableArray alloc] init];
        fDisplayedTorrents = [[NSMutableArray alloc] init];
        fDisplayedGroupIndexes = [[NSMutableIndexSet alloc] init];
        
        fMessageController = [[MessageWindowController alloc] init];
        fInfoController = [[InfoWindowController alloc] init];
        fPrefsController = [[PrefsController alloc] initWithHandle: fLib];
        
        fBadger = [[Badger alloc] initWithLib: fLib];
        
        fIPCController = [[IPCController alloc] init];
        [fIPCController setDelegate: self];
        [fIPCController setPrefsController: fPrefsController];
        fRemoteQuit = NO;
        
        [GrowlApplicationBridge setGrowlDelegate: self];
        [[UKKQueue sharedFileWatcher] setDelegate: self];
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
    if ([fDefaults boolForKey: @"SmallView"])
        [fTableView setRowHeight: ROW_HEIGHT_SMALL];
    
    //window min height
    NSSize contentMinSize = [fWindow contentMinSize];
    contentMinSize.height = [[fWindow contentView] frame].size.height - [fScrollView frame].size.height
                                + [fTableView rowHeight] + [fTableView intercellSpacing].height;
    [fWindow setContentMinSize: contentMinSize];
    
    if ([NSApp isOnLeopardOrBetter])
    {
        [fWindow setContentBorderThickness: [[fTableView enclosingScrollView] frame].origin.y forEdge: NSMinYEdge];
        [[fTotalTorrentsField cell] setBackgroundStyle: NSBackgroundStyleRaised];
        
        [[[fActionButton menu] itemAtIndex: 0] setImage: [NSImage imageNamed: NSImageNameActionTemplate]]; //set in nib if Leopard-only
    }
    else
    {
        //bottom bar
        [fBottomTigerBar setShowOnTiger: YES];
        [fBottomTigerBar setHidden: NO];
        [fBottomTigerLine setHidden: NO];
        
        [fActionButton setBezelStyle: NSSmallSquareBezelStyle];
        [fSpeedLimitButton setBezelStyle: NSSmallSquareBezelStyle];
        
        //status bar
        [fStatusBar setShowOnTiger: YES];
        [fStatusButton setHidden: YES];
        [fStatusTigerField setHidden: NO];
        [fStatusTigerImageView setHidden: NO];
        
        //filter bar
        [fNoFilterButton sizeToFit];
        
        NSRect activeRect = [fActiveFilterButton frame];
        activeRect.origin.x = NSMaxX([fNoFilterButton frame]) + 1.0;
        [fActiveFilterButton setFrame: activeRect];
    }
    
    [self updateGroupsFilterButton];
    
    //set up filter bar
    NSView * contentView = [fWindow contentView];
    NSSize windowSize = [contentView convertSize: [fWindow frame].size fromView: nil];
    [fFilterBar setHidden: YES];
    
    NSRect filterBarFrame = [fFilterBar frame];
    filterBarFrame.size.width = windowSize.width;
    [fFilterBar setFrame: filterBarFrame];
    
    [contentView addSubview: fFilterBar];
    [fFilterBar setFrameOrigin: NSMakePoint(0, NSMaxY([contentView frame]))];

    [self showFilterBar: [fDefaults boolForKey: @"FilterBar"] animate: NO];
    
    //set up status bar
    [fStatusBar setHidden: YES];
    
    [fTotalDLField setToolTip: NSLocalizedString(@"Total download speed", "Status Bar -> speed tooltip")];
    [fTotalULField setToolTip: NSLocalizedString(@"Total upload speed", "Status Bar -> speed tooltip")];
    
    NSRect statusBarFrame = [fStatusBar frame];
    statusBarFrame.size.width = windowSize.width;
    [fStatusBar setFrame: statusBarFrame];
    
    [contentView addSubview: fStatusBar];
    [fStatusBar setFrameOrigin: NSMakePoint(0, NSMaxY([contentView frame]))];
    [self showStatusBar: [fDefaults boolForKey: @"StatusBar"] animate: NO];
    
    [fActionButton setToolTip: NSLocalizedString(@"Shortcuts for changing global settings.",
                                "Main window -> 1st bottom left button (action) tooltip")];
    [fSpeedLimitButton setToolTip: NSLocalizedString(@"Speed Limit overrides the total bandwidth limits with its own limits.",
                                "Main window -> 2nd bottom left button (turtle) tooltip")];
    
    [fPrefsController setUpdater: fUpdater];
    
    [fTableView setTorrents: fDisplayedTorrents];
    [fTableView setGroupIndexes: fDisplayedGroupIndexes];
    
    [fTableView registerForDraggedTypes: [NSArray arrayWithObject: TORRENT_TABLE_VIEW_DATA_TYPE]];
    [fWindow registerForDraggedTypes: [NSArray arrayWithObjects: NSFilenamesPboardType, NSURLPboardType, nil]];

    //register for sleep notifications
    IONotificationPortRef notify;
    io_object_t iterator;
    if ((fRootPort = IORegisterForSystemPower(self, & notify, sleepCallBack, & iterator)) != 0)
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(notify), kCFRunLoopCommonModes);
    else
        NSLog(@"Could not IORegisterForSystemPower");
    
    //load previous transfers
    NSArray * history = [[NSArray alloc] initWithContentsOfFile: [NSHomeDirectory() stringByAppendingPathComponent: SUPPORT_FOLDER]];
    
    //old version saved transfer info in prefs file
    if (!history)
    {
        if ((history = [fDefaults arrayForKey: @"History"]))
            [history retain];
        [fDefaults removeObjectForKey: @"History"];
    }
    
    if (history)
    {
        Torrent * torrent;
        NSDictionary * historyItem;
        NSEnumerator * enumerator = [history objectEnumerator];
        while ((historyItem = [enumerator nextObject]))
            if ((torrent = [[Torrent alloc] initWithHistory: historyItem lib: fLib]))
            {
                [fTorrents addObject: torrent];
                [torrent release];
            }
        
        [history release];
    }
    
    //set filter
    NSString * filterType = [fDefaults stringForKey: @"Filter"];
    
    NSButton * currentFilterButton;
    if ([filterType isEqualToString: FILTER_ACTIVE])
        currentFilterButton = fActiveFilterButton;
    else if ([filterType isEqualToString: FILTER_PAUSE])
        currentFilterButton = fPauseFilterButton;
    else if ([filterType isEqualToString: FILTER_SEED])
        currentFilterButton = fSeedFilterButton;
    else if ([filterType isEqualToString: FILTER_DOWNLOAD])
        currentFilterButton = fDownloadFilterButton;
    else
    {
        //safety
        if (![filterType isEqualToString: FILTER_NONE])
            [fDefaults setObject: FILTER_NONE forKey: @"Filter"];
        currentFilterButton = fNoFilterButton;
    }
    [currentFilterButton setState: NSOnState];
    
    //set filter search type
    NSString * filterSearchType = [fDefaults stringForKey: @"FilterSearchType"];
    
    NSMenu * filterSearchMenu = [[fSearchFilterField cell] searchMenuTemplate];
    NSString * filterSearchTypeTitle;
    if ([filterSearchType isEqualToString: FILTER_TYPE_TRACKER])
        filterSearchTypeTitle = [[filterSearchMenu itemWithTag: FILTER_TYPE_TAG_TRACKER] title];
    else
    {
        //safety
        if (![filterType isEqualToString: FILTER_TYPE_NAME])
            [fDefaults setObject: FILTER_TYPE_NAME forKey: @"FilterSearchType"];
        filterSearchTypeTitle = [[filterSearchMenu itemWithTag: FILTER_TYPE_TAG_NAME] title];
    }
    [[fSearchFilterField cell] setPlaceholderString: filterSearchTypeTitle];
    
    //observe notifications
    NSNotificationCenter * nc = [NSNotificationCenter defaultCenter];
    
    [nc addObserver: self selector: @selector(updateUI)
                    name: @"UpdateUI" object: nil];
    
    [nc addObserver: self selector: @selector(torrentFinishedDownloading:)
                    name: @"TorrentFinishedDownloading" object: nil];
    
    [nc addObserver: self selector: @selector(torrentRestartedDownloading:)
                    name: @"TorrentRestartedDownloading" object: nil];
    
    //avoids need of setting delegate
    [nc addObserver: self selector: @selector(torrentTableViewSelectionDidChange:)
                    name: NSTableViewSelectionDidChangeNotification object: fTableView];
    
    [nc addObserver: self selector: @selector(prepareForUpdate:)
                    name: SUUpdaterWillRestartNotification object: nil];
    fUpdateInProgress = NO;
    
    [nc addObserver: self selector: @selector(autoSpeedLimitChange:)
                    name: @"AutoSpeedLimitChange" object: nil];
    
    [nc addObserver: self selector: @selector(changeAutoImport)
                    name: @"AutoImportSettingChange" object: nil];
    
    [nc addObserver: self selector: @selector(setWindowSizeToFit)
                    name: @"AutoSizeSettingChange" object: nil];
    
    [nc addObserver: fWindow selector: @selector(makeKeyWindow)
                    name: @"MakeWindowKey" object: nil];
    
    //check if torrent should now start
    [nc addObserver: self selector: @selector(torrentStoppedForRatio:)
                    name: @"TorrentStoppedForRatio" object: nil];
    
    [nc addObserver: self selector: @selector(updateTorrentsInQueue)
                    name: @"UpdateQueue" object: nil];
    
    //open newly created torrent file
    [nc addObserver: self selector: @selector(beginCreateFile:)
                    name: @"BeginCreateTorrentFile" object: nil];
    
    //open newly created torrent file
    [nc addObserver: self selector: @selector(openCreatedFile:)
                    name: @"OpenCreatedTorrentFile" object: nil];
    
    //update when groups change
    [nc addObserver: self selector: @selector(updateGroupsFilters:)
                    name: @"UpdateGroups" object: nil];

    //timer to update the interface every second
    [self updateUI];
    fTimer = [NSTimer scheduledTimerWithTimeInterval: UPDATE_UI_SECONDS target: self
        selector: @selector(updateUI) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSEventTrackingRunLoopMode];
    
    [self updateDisplay: nil];
    
    [fWindow makeKeyAndOrderFront: nil]; 
    
    if ([fDefaults boolForKey: @"InfoVisible"])
        [self showInfo: nil];
    
    //timer to auto toggle speed limit
    [self autoSpeedLimitChange: nil];
    fSpeedLimitTimer = [NSTimer scheduledTimerWithTimeInterval: AUTO_SPEED_LIMIT_SECONDS target: self
        selector: @selector(autoSpeedLimit) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fSpeedLimitTimer forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fSpeedLimitTimer forMode: NSEventTrackingRunLoopMode];
}

- (void) applicationDidFinishLaunching: (NSNotification *) notification
{
    [NSApp setServicesProvider: self];
    
    //register for dock icon drags
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler: self
        andSelector: @selector(handleOpenContentsEvent:replyEvent:)
        forEventClass: kCoreEventClass andEventID: kAEOpenContents];
    
    //auto importing
    [self checkAutoImportDirectory];
}

- (BOOL) applicationShouldHandleReopen: (NSApplication *) app hasVisibleWindows: (BOOL) visibleWindows
{
    if (![fWindow isVisible] && ![[fPrefsController window] isVisible])
        [fWindow makeKeyAndOrderFront: nil];
    return NO;
}

- (NSApplicationTerminateReply) applicationShouldTerminate: (NSApplication *) sender
{
    if (!fUpdateInProgress && !fRemoteQuit && [fDefaults boolForKey: @"CheckQuit"])
    {
        int active = 0, downloading = 0;
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive] && ![torrent isStalled])
            {
                active++;
                if (![torrent allDownloaded])
                    downloading++;
            }
        
        if ([fDefaults boolForKey: @"CheckQuitDownloading"] ? downloading > 0 : active > 0)
        {
            NSString * message = active == 1
                ? NSLocalizedString(@"There is an active transfer. Do you really want to quit?",
                    "Confirm Quit panel -> message")
                : [NSString stringWithFormat: NSLocalizedString(@"There are %d active transfers. Do you really want to quit?",
                    "Confirm Quit panel -> message"), active];

            NSBeginAlertSheet(NSLocalizedString(@"Confirm Quit", "Confirm Quit panel -> title"),
                                NSLocalizedString(@"Quit", "Confirm Quit panel -> button"),
                                NSLocalizedString(@"Cancel", "Confirm Quit panel -> button"), nil, fWindow, self,
                                @selector(quitSheetDidEnd:returnCode:contextInfo:), nil, nil, message);
            return NSTerminateLater;
        }
    }
    
    return NSTerminateNow;
}

- (void) quitSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode contextInfo: (void *) contextInfo
{
    [NSApp replyToApplicationShouldTerminate: returnCode == NSAlertDefaultReturn];
}

- (void) applicationWillTerminate: (NSNotification *) notification
{
    //stop timers and notification checking
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fTimer invalidate];
    [fSpeedLimitTimer invalidate];
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
        NSEnumerator * downloadEnumerator = [[fPendingTorrentDownloads allValues] objectEnumerator];
        NSDictionary * downloadDict;
        NSURLDownload * download;
        while ((downloadDict = [downloadEnumerator nextObject]))
        {
            download = [downloadDict objectForKey: @"Download"];
            [download cancel];
            [download release];
        }
        [fPendingTorrentDownloads removeAllObjects];
    }
    
    //remove all remaining torrent files in the temporary directory
    if (fTempTorrentFiles)
    {
        NSEnumerator * torrentEnumerator = [fTempTorrentFiles objectEnumerator];
        NSString * path;
        while ((path = [torrentEnumerator nextObject]))
            [[NSFileManager defaultManager] removeFileAtPath: path handler: nil];
    }
    
    //remember window states and close all windows
    [fDefaults setBool: [[fInfoController window] isVisible] forKey: @"InfoVisible"];
    [[NSApp windows] makeObjectsPerformSelector: @selector(close)];
    [self showStatusBar: NO animate: NO];
    [self showFilterBar: NO animate: NO];
    
    //save history
    [self updateTorrentHistory];
    
    //remaining calls the same as dealloc 
    [fInfoController release];
    [fMessageController release];
    [fPrefsController release];
    
    [fTorrents release];
    [fDisplayedTorrents release];
    [fDisplayedGroupIndexes release];
    
    [fOverlayWindow release];
    [fIPCController release];
    
    [fAutoImportedNames release];
    [fPendingTorrentDownloads release];
    [fTempTorrentFiles release];
    
    //complete cleanup
    tr_close(fLib);
    
    [fBadger release]; //clears dock icon on 10.4
}

- (void) handleOpenContentsEvent: (NSAppleEventDescriptor *) event replyEvent: (NSAppleEventDescriptor *) replyEvent
{
    NSString * urlString = nil;

    NSAppleEventDescriptor * directObject = [event paramDescriptorForKeyword: keyDirectObject];
    if ([directObject descriptorType] == typeAEList)
    {
        unsigned i;
        for (i = 1; i <= [directObject numberOfItems]; i++)
            if ((urlString = [[directObject descriptorAtIndex: i] stringValue]))
                break;
    }
    else
        urlString = [directObject stringValue];
    
    if (urlString)
        [self openURL: [NSURL URLWithString: urlString]];
}

- (void) download: (NSURLDownload *) download decideDestinationWithSuggestedFilename: (NSString *) suggestedName
{
    if ([[suggestedName pathExtension] caseInsensitiveCompare: @"torrent"] != NSOrderedSame)
    {
        [download cancel];
        
        NSRunAlertPanel(NSLocalizedString(@"Torrent download failed",
            @"Download not a torrent -> title"), [NSString stringWithFormat:
            NSLocalizedString(@"It appears that the file \"%@\" from %@ is not a torrent file.",
            @"Download not a torrent -> message"), suggestedName,
            [[[[download request] URL] absoluteString] stringByReplacingPercentEscapesUsingEncoding: NSUTF8StringEncoding]],
            NSLocalizedString(@"OK", @"Download not a torrent -> button"), nil, nil);
        
        [download release];
    }
    else
        [download setDestination: [NSTemporaryDirectory() stringByAppendingPathComponent: [suggestedName lastPathComponent]]
                    allowOverwrite: NO];
}

-(void) download: (NSURLDownload *) download didCreateDestination: (NSString *) path
{
    if (!fPendingTorrentDownloads)
        fPendingTorrentDownloads = [[NSMutableDictionary alloc] init];
    
    [fPendingTorrentDownloads setObject: [NSDictionary dictionaryWithObjectsAndKeys:
                    path, @"Path", download, @"Download", nil] forKey: [[download request] URL]];
}

- (void) download: (NSURLDownload *) download didFailWithError: (NSError *) error
{
    NSRunAlertPanel(NSLocalizedString(@"Torrent download failed", @"Torrent download error -> title"),
        [NSString stringWithFormat: NSLocalizedString(@"The torrent could not be downloaded from %@ because an error occurred (%@).",
        @"Torrent download failed -> message"),
        [[[[download request] URL] absoluteString] stringByReplacingPercentEscapesUsingEncoding: NSUTF8StringEncoding],
        [error localizedDescription]], NSLocalizedString(@"OK", @"Torrent download failed -> button"), nil, nil);
    
    [fPendingTorrentDownloads removeObjectForKey: [[download request] URL]];
    [download release];
}

- (void) downloadDidFinish: (NSURLDownload *) download
{
    NSString * path = [[fPendingTorrentDownloads objectForKey: [[download request] URL]] objectForKey: @"Path"];
    
    [self openFiles: [NSArray arrayWithObject: path] addType: ADD_URL forcePath: nil];
    
    [fPendingTorrentDownloads removeObjectForKey: [[download request] URL]];
    [download release];
    
    //delete temp torrent file on quit
    if (!fTempTorrentFiles)
        fTempTorrentFiles = [[NSMutableArray alloc] init];
    [fTempTorrentFiles addObject: path];
}

- (void) application: (NSApplication *) app openFiles: (NSArray *) filenames
{
    [self openFiles: filenames addType: ADD_NORMAL forcePath: nil];
}

- (void) openFiles: (NSArray *) filenames addType: (addType) type forcePath: (NSString *) path
{
    #warning checks could probably be removed, since location is checked when starting
    if (!path && [fDefaults boolForKey: @"UseIncompleteDownloadFolder"]
        && access([[[fDefaults stringForKey: @"IncompleteDownloadFolder"] stringByExpandingTildeInPath] UTF8String], 0))
    {
        NSOpenPanel * panel = [NSOpenPanel openPanel];
        
        [panel setPrompt: NSLocalizedString(@"Select", "Default incomplete folder cannot be used alert -> prompt")];
        [panel setAllowsMultipleSelection: NO];
        [panel setCanChooseFiles: NO];
        [panel setCanChooseDirectories: YES];
        [panel setCanCreateDirectories: YES];

        [panel setMessage: NSLocalizedString(@"The incomplete folder cannot be used. Choose a new location or cancel for none.",
                                        "Default incomplete folder cannot be used alert -> message")];
        
        NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys: filenames, @"Filenames",
                                                        [NSNumber numberWithInt: type], @"AddType", nil];
        
        [panel beginSheetForDirectory: nil file: nil types: nil modalForWindow: fWindow modalDelegate: self
                didEndSelector: @selector(incompleteChoiceClosed:returnCode:contextInfo:) contextInfo: dict];
        return;
    }
    
    if (!path && [fDefaults boolForKey: @"DownloadLocationConstant"]
        && access([[[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath] UTF8String], 0))
    {
        NSOpenPanel * panel = [NSOpenPanel openPanel];
        
        [panel setPrompt: NSLocalizedString(@"Select", "Default folder cannot be used alert -> prompt")];
        [panel setAllowsMultipleSelection: NO];
        [panel setCanChooseFiles: NO];
        [panel setCanChooseDirectories: YES];
        [panel setCanCreateDirectories: YES];

        [panel setMessage: NSLocalizedString(@"The download folder cannot be used. Choose a new location.",
                                        "Default folder cannot be used alert -> message")];
        
        NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys: filenames, @"Filenames",
                                                        [NSNumber numberWithInt: type], @"AddType", nil];
        
        [panel beginSheetForDirectory: nil file: nil types: nil modalForWindow: fWindow modalDelegate: self
                didEndSelector: @selector(downloadChoiceClosed:returnCode:contextInfo:) contextInfo: dict];
        return;
    }
    
    torrentFileState deleteTorrentFile;
    switch (type)
    {
        case ADD_CREATED:
            deleteTorrentFile = TORRENT_FILE_SAVE;
            break;
        case ADD_URL:
            deleteTorrentFile = TORRENT_FILE_DELETE;
            break;
        default:
            deleteTorrentFile = TORRENT_FILE_DEFAULT;
    }
    
    Torrent * torrent;
    NSString * torrentPath;
    tr_info info;
    NSEnumerator * enumerator = [filenames objectEnumerator];
    while ((torrentPath = [enumerator nextObject]))
    {
        //ensure torrent doesn't already exist
        tr_ctor * ctor = tr_ctorNew(fLib);
        tr_ctorSetMetainfoFromFile(ctor, [torrentPath UTF8String]);
        if (tr_torrentParse(fLib, ctor, &info) == TR_EDUPLICATE)
        {
            [self duplicateOpenAlert: [NSString stringWithUTF8String: info.name]];
            tr_ctorFree(ctor);
            tr_metainfoFree(&info);
            continue;
        }
        tr_ctorFree(ctor);
        
        //determine download location
        NSString * location;
        if (path)
            location = [path stringByExpandingTildeInPath];
        else if ([fDefaults boolForKey: @"DownloadLocationConstant"])
            location = [[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath];
        else if (type != ADD_URL)
            location = [torrentPath stringByDeletingLastPathComponent];
        else
            location = nil;
        
        //determine to show the options window
        BOOL showWindow = type == ADD_SHOW_OPTIONS || ([fDefaults boolForKey: @"DownloadAsk"]
                            && (info.isMultifile || ![fDefaults boolForKey: @"DownloadAskMulti"]));
        tr_metainfoFree(&info);
        
        if (!(torrent = [[Torrent alloc] initWithPath: torrentPath location: location
                            deleteTorrentFile: showWindow ? TORRENT_FILE_SAVE : deleteTorrentFile lib: fLib]))
            continue;
        
        //verify the data right away if it was newly created
        if (type == ADD_CREATED)
            [torrent resetCache];
        
        //add it to the "File -> Open Recent" menu
        [[NSDocumentController sharedDocumentController] noteNewRecentDocumentURL: [NSURL fileURLWithPath: torrentPath]];
        
        //show the add window or add directly
        if (showWindow || !location)
        {
            AddWindowController * addController = [[AddWindowController alloc] initWithTorrent: torrent destination: location
                                                    controller: self deleteTorrent: deleteTorrentFile];
            [addController showWindow: self];
        }
        else
        {
            [torrent setWaitToStart: [fDefaults boolForKey: @"AutoStartDownload"]];
            
            [torrent update];
            [fTorrents addObject: torrent];
            [torrent release];
        }
    }

    [self updateTorrentsInQueue];
}

- (void) askOpenConfirmed: (AddWindowController *) addController add: (BOOL) add
{
    Torrent * torrent = [addController torrent];
    [addController release];
    
    if (add)
    {
        [torrent setOrderValue: [fTorrents count]-1]; //ensure that queue order is always sequential
        
        [torrent update];
        [fTorrents addObject: torrent];
        [torrent release];
        
        [self updateTorrentsInQueue];
    }
    else
    {
        [torrent closeRemoveTorrent];
        [torrent release];
    }
}

- (void) openCreatedFile: (NSNotification *) notification
{
    NSDictionary * dict = [notification userInfo];
    [self openFiles: [NSArray arrayWithObject: [dict objectForKey: @"File"]] addType: ADD_CREATED
                        forcePath: [dict objectForKey: @"Path"]];
    [dict release];
}

- (void) incompleteChoiceClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (NSDictionary *) dictionary
{
    if (code == NSOKButton)
        [fDefaults setObject: [[openPanel filenames] objectAtIndex: 0] forKey: @"IncompleteDownloadFolder"];
    else
        [fDefaults setBool: NO forKey: @"UseIncompleteDownloadFolder"];
    
    [self performSelectorOnMainThread: @selector(openFilesWithDict:) withObject: dictionary waitUntilDone: NO];
}

- (void) downloadChoiceClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (NSDictionary *) dictionary
{
    if (code == NSOKButton)
    {
        [fDefaults setObject: [[openPanel filenames] objectAtIndex: 0] forKey: @"DownloadFolder"];
        [self performSelectorOnMainThread: @selector(openFilesWithDict:) withObject: dictionary waitUntilDone: NO];
    }
    else
        [dictionary release];
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
                                [NSNumber numberWithInt: ADD_NORMAL], @"AddType", nil];
    [self performSelectorOnMainThread: @selector(openFilesWithDict:) withObject: dict waitUntilDone: NO];
}

- (void) openShowSheet: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setAllowsMultipleSelection: YES];
    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: NO];

    [panel beginSheetForDirectory: nil file: nil types: [NSArray arrayWithObject: @"torrent"]
        modalForWindow: fWindow modalDelegate: self didEndSelector: @selector(openSheetClosed:returnCode:contextInfo:)
        contextInfo: [NSNumber numberWithBool: sender == fOpenIgnoreDownloadFolder]];
}

- (void) openSheetClosed: (NSOpenPanel *) panel returnCode: (int) code contextInfo: (NSNumber *) useOptions
{
    if (code == NSOKButton)
    {
        NSDictionary * dictionary = [[NSDictionary alloc] initWithObjectsAndKeys: [panel filenames], @"Filenames",
            [NSNumber numberWithInt: [useOptions boolValue] ? ADD_SHOW_OPTIONS : ADD_NORMAL], @"AddType", nil];
        [self performSelectorOnMainThread: @selector(openFilesWithDict:) withObject: dictionary waitUntilDone: NO];
    }
}

- (void) duplicateOpenAlert: (NSString *) name
{
    if (![fDefaults boolForKey: @"WarningDuplicate"])
        return;
    
    NSAlert * alert = [[NSAlert alloc] init];
    [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"A transfer of \"%@\" already exists.",
                            "Open duplicate alert -> title"), name]];
    [alert setInformativeText:
            NSLocalizedString(@"The torrent file cannot be opened because it is a duplicate of an already added transfer.",
                            "Open duplicate alert -> message")];
    [alert setAlertStyle: NSWarningAlertStyle];
    [alert addButtonWithTitle: NSLocalizedString(@"OK", "Open duplicate alert -> button")];
    
    BOOL onLeopard = [NSApp isOnLeopardOrBetter];
    if (onLeopard)
        [alert setShowsSuppressionButton: YES];
    else
        [alert addButtonWithTitle: NSLocalizedString(@"Don't Alert Again", "Open duplicate alert -> button")];
    
    NSInteger result = [alert runModal];
    if ((onLeopard ? [[alert suppressionButton] state] == NSOnState : result == NSAlertSecondButtonReturn))
        [fDefaults setBool: NO forKey: @"WarningDuplicate"];
    [alert release];
}

- (void) openURL: (NSURL *) url
{
    [[NSURLDownload alloc] initWithRequest: [NSURLRequest requestWithURL: url] delegate: self];
}

- (void) openURLShowSheet: (id) sender
{
    [NSApp beginSheet: fURLSheetWindow modalForWindow: fWindow modalDelegate: self
            didEndSelector: @selector(urlSheetDidEnd:returnCode:contextInfo:) contextInfo: nil];
}

- (void) openURLEndSheet: (id) sender
{
    [fURLSheetWindow orderOut: sender];
    [NSApp endSheet: fURLSheetWindow returnCode: 1];
}

- (void) openURLCancelEndSheet: (id) sender
{
    [fURLSheetWindow orderOut: sender];
    [NSApp endSheet: fURLSheetWindow returnCode: 0];
}

- (void) urlSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode contextInfo: (void *) contextInfo
{
    [fURLSheetTextField selectText: self];
    if (returnCode != 1)
        return;
    
    NSString * urlString = [fURLSheetTextField stringValue];
    if (![urlString isEqualToString: @""])
    {
        if ([urlString rangeOfString: @"://"].location == NSNotFound)
        {
            if ([urlString rangeOfString: @"."].location == NSNotFound)
            {
                int beforeCom;
                if ((beforeCom = [urlString rangeOfString: @"/"].location) != NSNotFound)
                    urlString = [NSString stringWithFormat: @"http://www.%@.com/%@",
                                    [urlString substringToIndex: beforeCom],
                                    [urlString substringFromIndex: beforeCom + 1]];
                else
                    urlString = [NSString stringWithFormat: @"http://www.%@.com", urlString];
            }
            else
                urlString = [@"http://" stringByAppendingString: urlString];
        }
        
        NSURL * url = [NSURL URLWithString: urlString];
        [self performSelectorOnMainThread: @selector(openURL:) withObject: url waitUntilDone: NO];
    }
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
    [self resumeTorrents: fTorrents];
}

- (void) resumeTorrents: (NSArray *) torrents
{
    NSEnumerator * enumerator = [torrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent setWaitToStart: YES];
    
    [self updateTorrentsInQueue];
}

- (void) resumeSelectedTorrentsNoWait:  (id) sender
{
    [self resumeTorrentsNoWait: [fTableView selectedTorrents]];
}

- (void) resumeWaitingTorrents: (id) sender
{
    NSMutableArray * torrents = [NSMutableArray arrayWithCapacity: [fTorrents count]];
    
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        if (![torrent isActive] && [torrent waitingToStart])
            [torrents addObject: torrent];
    
    [self resumeTorrentsNoWait: torrents];
}

- (void) resumeTorrentsNoWait: (NSArray *) torrents
{
    //iterate through instead of all at once to ensure no conflicts
    NSEnumerator * enumerator = [torrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent startTransfer];
    
    [self updateUI];
    [self updateDisplay: nil];
    [self updateTorrentHistory];
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
    NSEnumerator * enumerator = [torrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent setWaitToStart: NO];

    [torrents makeObjectsPerformSelector: @selector(stopTransfer)];
    
    [self updateUI];
    [self updateTorrentHistory];
}

- (void) removeTorrents: (NSArray *) torrents deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent
{
    [torrents retain];
    int active = 0, downloading = 0;

    if ([fDefaults boolForKey: @"CheckRemove"])
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [torrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive])
            {
                active++;
                if (![torrent isSeeding])
                    downloading++;
            }

        if ([fDefaults boolForKey: @"CheckRemoveDownloading"] ? downloading > 0 : active > 0)
        {
            NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys:
                                    torrents, @"Torrents",
                                    [NSNumber numberWithBool: deleteData], @"DeleteData",
                                    [NSNumber numberWithBool: deleteTorrent], @"DeleteTorrent", nil];
            
            NSString * title, * message;
            
            int selected = [torrents count];
            if (selected == 1)
            {
                NSString * torrentName = [[torrents objectAtIndex: 0] name];
                
                if (!deleteData && !deleteTorrent)
                    title = [NSString stringWithFormat: NSLocalizedString(@"Confirm removal of \"%@\" from the transfer list.",
                                "Removal confirm panel -> title"), torrentName];
                else if (deleteData && !deleteTorrent)
                    title = [NSString stringWithFormat: NSLocalizedString(@"Confirm removal of \"%@\" from the transfer list"
                                " and trash data file.", "Removal confirm panel -> title"), torrentName];
                else if (!deleteData && deleteTorrent)
                    title = [NSString stringWithFormat: NSLocalizedString(@"Confirm removal of \"%@\" from the transfer list"
                                " and trash torrent file.", "Removal confirm panel -> title"), torrentName];
                else
                    title = [NSString stringWithFormat: NSLocalizedString(@"Confirm removal of \"%@\" from the transfer list"
                            " and trash both data and torrent files.", "Removal confirm panel -> title"), torrentName];
                
                message = NSLocalizedString(@"This transfer is active."
                            " Once removed, continuing the transfer will require the torrent file.",
                            "Removal confirm panel -> message");
            }
            else
            {
                if (!deleteData && !deleteTorrent)
                    title = [NSString stringWithFormat: NSLocalizedString(@"Confirm removal of %d transfers"
                                " from the transfer list.", "Removal confirm panel -> title"), selected];
                else if (deleteData && !deleteTorrent)
                    title = [NSString stringWithFormat: NSLocalizedString(@"Confirm removal of %d transfers"
                                " from the transfer list and trash data file.", "Removal confirm panel -> title"), selected];
                else if (!deleteData && deleteTorrent)
                    title = [NSString stringWithFormat: NSLocalizedString(@"Confirm removal of %d transfers"
                                " from the transfer list and trash torrent file.", "Removal confirm panel -> title"), selected];
                else
                    title = [NSString stringWithFormat: NSLocalizedString(@"Confirm removal of %d transfers"
                                " from the transfer list and trash both data and torrent files.",
                                "Removal confirm panel -> title"), selected];
                
                if (selected == active)
                    message = [NSString stringWithFormat: NSLocalizedString(@"There are %d active transfers.",
                                "Removal confirm panel -> message part 1"), active];
                else
                    message = [NSString stringWithFormat: NSLocalizedString(@"There are %d transfers (%d active).",
                                "Removal confirm panel -> message part 1"), selected, active];
                message = [message stringByAppendingString:
                    NSLocalizedString(@" Once removed, continuing the transfers will require the torrent files.",
                                "Removal confirm panel -> message part 2")];
            }
            
            NSBeginAlertSheet(title, NSLocalizedString(@"Remove", "Removal confirm panel -> button"),
                NSLocalizedString(@"Cancel", "Removal confirm panel -> button"), nil, fWindow, self,
                nil, @selector(removeSheetDidEnd:returnCode:contextInfo:), dict, message);
            return;
        }
    }
    
    [self confirmRemoveTorrents: torrents deleteData: deleteData deleteTorrent: deleteTorrent];
}

- (void) removeSheetDidEnd: (NSWindow *) sheet returnCode: (int) returnCode contextInfo: (NSDictionary *) dict
{
    NSArray * torrents = [dict objectForKey: @"Torrents"];
    if (returnCode == NSAlertDefaultReturn)
        [self confirmRemoveTorrents: torrents deleteData: [[dict objectForKey: @"DeleteData"] boolValue]
                                                deleteTorrent: [[dict objectForKey: @"DeleteTorrent"] boolValue]];
    else
        [torrents release];
    
    [dict release];
}

- (void) confirmRemoveTorrents: (NSArray *) torrents deleteData: (BOOL) deleteData deleteTorrent: (BOOL) deleteTorrent
{
    //don't want any of these starting then stopping
    NSEnumerator * enumerator = [torrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent setWaitToStart: NO];
    
    [fTorrents removeObjectsInArray: torrents];
    [fDisplayedTorrents removeObjectsInArray: torrents];
    
    int lowestOrderValue = INT_MAX;
    enumerator = [torrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        if (deleteData)
            [torrent trashData];
        if (deleteTorrent)
            [torrent trashTorrent];
        
        lowestOrderValue = MIN(lowestOrderValue, [torrent orderValue]);
        
        [torrent closeRemoveTorrent];
    }
    
    [torrents release];

    //reset the order values if necessary
    if (lowestOrderValue < [fTorrents count])
    {
        NSSortDescriptor * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"orderValue" ascending: YES] autorelease];
        NSArray * tempTorrents = [fTorrents sortedArrayUsingDescriptors: [NSArray arrayWithObject: orderDescriptor]];

        int i;
        for (i = lowestOrderValue; i < [tempTorrents count]; i++)
            [[tempTorrents objectAtIndex: i] setOrderValue: i];
    }
    
    [fTableView deselectAll: nil];
    
    [self updateTorrentsInQueue];
}

- (void) removeNoDelete: (id) sender
{
    [self removeTorrents: [fTableView selectedTorrents]
                deleteData: NO deleteTorrent: NO];
}

- (void) removeDeleteData: (id) sender
{
    [self removeTorrents: [fTableView selectedTorrents]
                deleteData: YES deleteTorrent: NO];
}

- (void) removeDeleteTorrent: (id) sender
{
    [self removeTorrents: [fTableView selectedTorrents]
                deleteData: NO deleteTorrent: YES];
}

- (void) removeDeleteDataAndTorrent: (id) sender
{
    [self removeTorrents: [fTableView selectedTorrents]
                deleteData: YES deleteTorrent: YES];
}

- (void) moveDataFiles: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];
    [panel setPrompt: NSLocalizedString(@"Select", "Move torrent -> prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];
    
    NSArray * torrents = [[fTableView selectedTorrents] retain];
    int count = [torrents count];
    if (count == 1)
        [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the new folder for \"%@\".",
                            "Move torrent -> select destination folder"), [[torrents objectAtIndex: 0] name]]];
    else
        [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the new folder for %d data files.",
                            "Move torrent -> select destination folder"), count]];
        
    [panel beginSheetForDirectory: nil file: nil modalForWindow: fWindow modalDelegate: self
        didEndSelector: @selector(moveDataFileChoiceClosed:returnCode:contextInfo:) contextInfo: torrents];
}

- (void) moveDataFileChoiceClosed: (NSOpenPanel *) panel returnCode: (int) code contextInfo: (NSArray *) torrents
{
    if (code == NSOKButton)
    {
        NSEnumerator * enumerator = [torrents objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            [torrent moveTorrentDataFileTo: [[panel filenames] objectAtIndex: 0]];
    }
    
    [torrents release];
}

- (void) copyTorrentFiles: (id) sender
{
    [self copyTorrentFileForTorrents: [[NSMutableArray alloc] initWithArray:
            [fTableView selectedTorrents]]];
}

- (void) copyTorrentFileForTorrents: (NSMutableArray *) torrents
{
    if ([torrents count] <= 0)
    {
        [torrents release];
        return;
    }

    Torrent * torrent = [torrents objectAtIndex: 0];

    //warn user if torrent file can't be found
    if (![[NSFileManager defaultManager] fileExistsAtPath: [torrent torrentLocation]])
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
        
        [torrents removeObjectAtIndex: 0];
        [self copyTorrentFileForTorrents: torrents];
    }
    else
    {
        NSSavePanel * panel = [NSSavePanel savePanel];
        [panel setRequiredFileType: @"torrent"];
        [panel setCanSelectHiddenExtension: YES];
        
        [panel beginSheetForDirectory: nil file: [torrent name] modalForWindow: fWindow modalDelegate: self
            didEndSelector: @selector(saveTorrentCopySheetClosed:returnCode:contextInfo:) contextInfo: torrents];
    }
}

- (void) saveTorrentCopySheetClosed: (NSSavePanel *) panel returnCode: (int) code contextInfo: (NSMutableArray *) torrents
{
    //copy torrent to new location with name of data file
    if (code == NSOKButton)
        [[torrents objectAtIndex: 0] copyTorrentFileTo: [panel filename]];
    
    [torrents removeObjectAtIndex: 0];
    [self performSelectorOnMainThread: @selector(copyTorrentFileForTorrents:) withObject: torrents waitUntilDone: NO];
}

- (void) revealFile: (id) sender
{
    NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent revealData];
}

- (void) announceSelectedTorrents: (id) sender
{
    NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
    {
        if ([torrent canManualAnnounce])
            [torrent manualAnnounce];
    }
}

- (void) resetCacheForSelectedTorrents: (id) sender
{
    NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent resetCache];
    
    [self updateDisplay: nil];
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
    }
}

- (void) setInfoTab: (id) sender
{
    if (sender == fNextInfoTabItem)
        [fInfoController setNextTab];
    else
        [fInfoController setPreviousTab];
}

- (void) showMessageWindow: (id) sender
{
    [fMessageController showWindow: nil];
}

- (void) showStatsWindow: (id) sender
{
    [[StatsWindowController statsWindow: fLib] showWindow: nil];
}

- (void) updateUI
{
    [fTorrents makeObjectsPerformSelector: @selector(update)];
    
    if (![NSApp isHidden])
    {
        if ([fWindow isVisible])
        {
            [self updateDisplay: nil];
            
            //update status bar
            if (![fStatusBar isHidden])
            {
                //set rates
                float downloadRate, uploadRate;
                tr_torrentRates(fLib, & downloadRate, & uploadRate);
                
                [fTotalDLField setStringValue: [NSString stringForSpeed: downloadRate]];
                [fTotalULField setStringValue: [NSString stringForSpeed: uploadRate]];
                
                //set status button text
                NSString * statusLabel = [fDefaults stringForKey: @"StatusLabel"], * statusString;
                BOOL total;
                if ((total = [statusLabel isEqualToString: STATUS_RATIO_TOTAL]) || [statusLabel isEqualToString: STATUS_RATIO_SESSION])
                {
                    tr_session_stats stats;
                    if (total)
                        tr_getCumulativeSessionStats(fLib, &stats);
                    else
                        tr_getSessionStats(fLib, &stats);
                    
                    statusString = [NSLocalizedString(@"Ratio: ", "status bar -> status label")
                                    stringByAppendingString: [NSString stringForRatio: stats.ratio]];
                }
                else if ((total = [statusLabel isEqualToString: STATUS_TRANSFER_TOTAL])
                            || [statusLabel isEqualToString: STATUS_TRANSFER_SESSION])
                {
                    tr_session_stats stats;
                    if (total)
                        tr_getCumulativeSessionStats(fLib, &stats);
                    else
                        tr_getSessionStats(fLib, &stats);
                    
                    statusString = [NSString stringWithFormat: NSLocalizedString(@"DL: %@  UL: %@",
                        "status bar -> status label (2 spaces between)"),
                        [NSString stringForFileSize: stats.downloadedBytes], [NSString stringForFileSize: stats.uploadedBytes]];
                }
                else
                    statusString = @"";
                
                if ([NSApp isOnLeopardOrBetter])
                {
                    [fStatusButton setTitle: statusString];
                    [fStatusButton sizeToFit];
                    
                    //width ends up being too long
                    NSRect statusFrame = [fStatusButton frame];
                    statusFrame.size.width -= 25.0;
                    [fStatusButton setFrame: statusFrame];
                }
                else
                    [fStatusTigerField setStringValue: statusString];
            }
        }

        //update non-constant parts of info window
        if ([[fInfoController window] isVisible])
            [fInfoController updateInfoStats];
    }
    
    //badge dock
    [fBadger updateBadge];
}

- (void) updateTorrentsInQueue
{
    BOOL download = [fDefaults boolForKey: @"Queue"],
        seed = [fDefaults boolForKey: @"QueueSeed"];
    
    int desiredDownloadActive = [self numToStartFromQueue: YES],
        desiredSeedActive = [self numToStartFromQueue: NO];
    
    //sort torrents by order value
    NSArray * sortedTorrents;
    if ([fTorrents count] > 1 && (desiredDownloadActive > 0 || desiredSeedActive > 0))
    {
        NSSortDescriptor * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"orderValue" ascending: YES] autorelease];
        sortedTorrents = [fTorrents sortedArrayUsingDescriptors: [NSArray arrayWithObject: orderDescriptor]];
    }
    else
        sortedTorrents = fTorrents;
    
    Torrent * torrent;
    NSEnumerator * enumerator = [sortedTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        if (![torrent isActive] && ![torrent isChecking] && [torrent waitingToStart])
        {
            if (![torrent allDownloaded])
            {
                if (!download || desiredDownloadActive > 0)
                {
                    [torrent startTransfer];
                    if ([torrent isActive])
                        desiredDownloadActive--;
                    [torrent update];
                }
            }
            else
            {
                if (!seed || desiredSeedActive > 0)
                {
                    [torrent startTransfer];
                    if ([torrent isActive])
                        desiredSeedActive--;
                    [torrent update];
                }
            }
        }
    }
    
    [self updateUI];
    [self updateTorrentHistory];
}

- (int) numToStartFromQueue: (BOOL) downloadQueue
{
    if (![fDefaults boolForKey: downloadQueue ? @"Queue" : @"QueueSeed"])
        return 0;
    
    int desired = [fDefaults integerForKey: downloadQueue ? @"QueueDownloadNumber" : @"QueueSeedNumber"];
        
    Torrent * torrent;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
    {
        if ([torrent isChecking])
        {
            desired--;
            if (desired <= 0)
                return 0;
        }
        else if ([torrent isActive] && ![torrent isStalled] && ![torrent isError])
        {
            if ([torrent allDownloaded] != downloadQueue)
            {
                desired--;
                if (desired <= 0)
                    return 0;
            }
        }
        else;
    }
    
    return desired;
}

- (void) torrentFinishedDownloading: (NSNotification *) notification
{
    Torrent * torrent = [notification object];
    if ([torrent isActive])
    {
        if ([fDefaults boolForKey: @"PlayDownloadSound"])
        {
            NSSound * sound;
            if ((sound = [NSSound soundNamed: [fDefaults stringForKey: @"DownloadSound"]]))
                [sound play];
        }
        
        NSDictionary * clickContext = [NSDictionary dictionaryWithObjectsAndKeys: GROWL_DOWNLOAD_COMPLETE, @"Type",
                                        [torrent dataLocation] , @"Location", nil];
        [GrowlApplicationBridge notifyWithTitle: NSLocalizedString(@"Download Complete", "Growl notification title")
                                    description: [torrent name] notificationName: GROWL_DOWNLOAD_COMPLETE
                                    iconData: nil priority: 0 isSticky: NO clickContext: clickContext];
        
        if (![fWindow isMainWindow])
            [fBadger incrementCompleted];
        
        if ([fDefaults boolForKey: @"QueueSeed"] && [self numToStartFromQueue: NO] <= 0)
        {
            [torrent stopTransfer];
            [torrent setWaitToStart: YES];
        }
    }
    
    [self updateTorrentsInQueue];
}

- (void) torrentRestartedDownloading: (NSNotification *) notification
{
    Torrent * torrent = [notification object];
    if ([torrent isActive])
    {
        if ([fDefaults boolForKey: @"Queue"] && [self numToStartFromQueue: YES] <= 0)
        {
            [torrent stopTransfer];
            [torrent setWaitToStart: YES];
        }
    }
    
    [self updateTorrentsInQueue];
}

- (void) updateTorrentHistory
{
    NSMutableArray * history = [NSMutableArray arrayWithCapacity: [fTorrents count]];

    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [history addObject: [torrent history]];

    [history writeToFile: [NSHomeDirectory() stringByAppendingPathComponent: SUPPORT_FOLDER] atomically: YES];
}

- (void) setSort: (id) sender
{
    NSString * sortType;
    switch ([sender tag])
    {
        case SORT_ORDER_TAG:
            sortType = SORT_ORDER;
            [fDefaults setBool: NO forKey: @"SortReverse"];
            [fDefaults setBool: NO forKey: @"SortByGroup"];
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
        default:
            return;
    }
    
    [fDefaults setObject: sortType forKey: @"Sort"];
    [self updateDisplay: nil];
}

- (void) setSortByGroup: (id) sender
{
    [fDefaults setBool: ![fDefaults boolForKey: @"SortByGroup"] forKey: @"SortByGroup"];
    [self updateDisplay: nil];
}

- (void) setSortReverse: (id) sender
{
    [fDefaults setBool: ![fDefaults boolForKey: @"SortReverse"] forKey: @"SortReverse"];
    [self updateDisplay: nil];
}

- (void) prepareForDisplay
{
    NSString * sortType = [fDefaults stringForKey: @"Sort"];
    BOOL asc = ![fDefaults boolForKey: @"SortReverse"];
    
    NSSortDescriptor * nameDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"name"
                                            ascending: asc selector: @selector(caseInsensitiveCompare:)] autorelease],
                    * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"orderValue"
                                            ascending: asc] autorelease];
    
    BOOL group = NO;
    
    NSArray * descriptors;
    if ([sortType isEqualToString: SORT_ORDER])
        descriptors = [[NSArray alloc] initWithObjects: orderDescriptor, nil];
    else
    {
        if ([sortType isEqualToString: SORT_NAME])
            descriptors = [[NSArray alloc] initWithObjects: nameDescriptor, orderDescriptor, nil];
        else if ([sortType isEqualToString: SORT_STATE])
        {
            NSSortDescriptor * stateDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                    @"stateSortKey" ascending: !asc] autorelease],
                            * progressDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"progress" ascending: !asc] autorelease],
                            * ratioDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"ratio" ascending: !asc] autorelease];
            
            descriptors = [[NSArray alloc] initWithObjects: stateDescriptor, progressDescriptor, ratioDescriptor,
                                                                nameDescriptor, orderDescriptor, nil];
        }
        else if ([sortType isEqualToString: SORT_PROGRESS])
        {
            NSSortDescriptor * progressDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"progress" ascending: asc] autorelease],
                            * ratioProgressDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"progressStopRatio" ascending: asc] autorelease],
                            * ratioDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"ratio" ascending: asc] autorelease];
            
            descriptors = [[NSArray alloc] initWithObjects: progressDescriptor, ratioProgressDescriptor, ratioDescriptor,
                                                                nameDescriptor, orderDescriptor, nil];
        }
        else if ([sortType isEqualToString: SORT_TRACKER])
        {
            NSSortDescriptor * trackerDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"trackerAddress"
                                                    ascending: asc selector: @selector(caseInsensitiveCompare:)] autorelease];
            
            descriptors = [[NSArray alloc] initWithObjects: trackerDescriptor, nameDescriptor, orderDescriptor, nil];
        }
        else if ([sortType isEqualToString: SORT_ACTIVITY])
        {
            NSSortDescriptor * rateDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"totalRate" ascending: !asc]
                                                        autorelease];
            NSSortDescriptor * activityDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"dateActivityOrAdd" ascending: !asc]
                                                        autorelease];
            
            descriptors = [[NSArray alloc] initWithObjects: rateDescriptor, activityDescriptor, orderDescriptor, nil];
        }
        else
        {
            NSSortDescriptor * dateDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"dateAdded" ascending: asc] autorelease];
        
            descriptors = [[NSArray alloc] initWithObjects: dateDescriptor, orderDescriptor, nil];
        }
        
        group = [fDefaults boolForKey: @"SortByGroup"];
        if (group)
        {
            NSSortDescriptor * groupDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"groupOrderValue"
                                                    ascending: asc] autorelease];
            
            NSMutableArray * temp = [[NSMutableArray alloc] initWithCapacity: [descriptors count]+1];
            [temp addObject: groupDescriptor];
            [temp addObjectsFromArray: descriptors];
            
            [descriptors release];
            descriptors = temp;
        }
    }
    
    [fDisplayedTorrents sortUsingDescriptors: descriptors];
    [descriptors release];
    
    //add group divider if necessary
    int total = [fDisplayedTorrents count];
    
    [fDisplayedGroupIndexes removeAllIndexes];
    if (group && total > 0 && [NSApp isOnLeopardOrBetter])
    {
        int i, groupValue = [[fDisplayedTorrents objectAtIndex: 0] groupValue], newGroupValue, count = 1, start = 0;
        for (i = 0; i < [fDisplayedTorrents count]; i++)
        {
            BOOL last = i == [fDisplayedTorrents count]-1;
            if (!last)
                newGroupValue = [[fDisplayedTorrents objectAtIndex: i+1] groupValue];
            if (groupValue != newGroupValue || last)
            {
                [fDisplayedTorrents insertObject: [NSNumber numberWithInt: groupValue] atIndex: start];
                [fDisplayedGroupIndexes addIndex: start];
                
                groupValue = newGroupValue;
                count = 1;
                
                start = i+2;
                i++;
            }
            else
                count++;
        }
    }
    
    [fTableView reloadData];
}

- (void) updateDisplay: (id) sender
{
    NSMutableArray * previousTorrents = [fDisplayedTorrents mutableCopy];
    [previousTorrents removeObjectsAtIndexes: fDisplayedGroupIndexes];
    
    NSArray * selectedValues = [fTableView selectedValues];
    
    int active = 0, downloading = 0, seeding = 0, paused = 0;
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
    
    int groupFilterValue = [fDefaults integerForKey: @"FilterGroup"];
    BOOL filterGroup = groupFilterValue != GROUP_FILTER_ALL_TAG;
    
    NSString * searchString = [fSearchFilterField stringValue];
    BOOL filterText = [searchString length] > 0,
        filterTracker = filterText && [[fDefaults stringForKey: @"FilterSearchType"] isEqualToString: FILTER_TYPE_TRACKER];
    
    NSMutableIndexSet * indexes = [NSMutableIndexSet indexSet];
    
    //get count of each type
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    int i = -1;
    BOOL isActive;
    while ((torrent = [enumerator nextObject]))
    {
        i++;
        
        //check status
        if ([torrent isActive])
        {
            if ([torrent isSeeding])
            {
                seeding++;
                isActive = ![torrent isStalled];
                if (isActive)
                    active++;
                
                if (filterStatus && (!(filterActive && isActive) && !filterSeed))
                    continue;
            }
            else
            {
                downloading++;
                isActive = ![torrent isStalled];
                if (isActive)
                    active++;
                
                if (filterStatus && (!(filterActive && isActive) && !filterDownload))
                    continue;
            }
        }
        else
        {
            paused++;
            if (filterStatus && !filterPause)
                continue;
        }
        
        //checkGroup
        if (filterGroup)
            if ([torrent groupValue] != groupFilterValue)
                continue;
        
        //check text field
        if (filterText)
        {
            if (filterTracker)
            {
                BOOL removeTextField = YES;
                NSEnumerator * trackerEnumerator = [[torrent allTrackers] objectEnumerator], * subTrackerEnumerator;
                NSArray * subTrackers;
                NSString * tracker;
                while (removeTextField && (subTrackers = [trackerEnumerator nextObject]))
                {
                    subTrackerEnumerator = [subTrackers objectEnumerator];
                    while ((tracker = [subTrackerEnumerator nextObject]))
                        if ([tracker rangeOfString: searchString options: NSCaseInsensitiveSearch].location != NSNotFound)
                        {
                            removeTextField = NO;
                            break;
                        }
                }
                
                if (removeTextField)
                    continue;
            }
            else
            {
                if ([[torrent name] rangeOfString: searchString options: NSCaseInsensitiveSearch].location == NSNotFound)
                    continue;
            }
        }
        
        [indexes addIndex: i];
    }
    
    [fDisplayedTorrents setArray: [fTorrents objectsAtIndexes: indexes]];
    
    //set button tooltips
    [fNoFilterButton setCount: [fTorrents count]];
    [fActiveFilterButton setCount: active];
    [fDownloadFilterButton setCount: downloading];
    [fSeedFilterButton setCount: seeding];
    [fPauseFilterButton setCount: paused];
    
    //clear display cache for not-shown torrents
    [previousTorrents removeObjectsInArray: fDisplayedTorrents]; //neither array should currently have group items
    
    enumerator = [previousTorrents objectEnumerator];
    while ((torrent = [enumerator nextObject]))
        [torrent setPreviousAmountFinished: NULL];
    
    [previousTorrents release];
    
    //sort, add groups, and reset selected
    [self prepareForDisplay];
    [fTableView selectValues: selectedValues];
    
    //set status bar torrent count text
    NSString * totalTorrentsString;
    int totalCount = [fTorrents count];
    if (totalCount != 1)
        totalTorrentsString = [NSString stringWithFormat: NSLocalizedString(@"%d transfers", "Status bar transfer count"), totalCount];
    else
        totalTorrentsString = NSLocalizedString(@"1 transfer", "Status bar transfer count");
    
    if (filterStatus || filterGroup || filterText)
        totalTorrentsString = [NSString stringWithFormat: NSLocalizedString(@"%d of %@", "Status bar transfer count"),
                                [fDisplayedTorrents count], totalTorrentsString];
    
    [fTotalTorrentsField setStringValue: totalTorrentsString];

    [self setWindowSizeToFit];
}

//resets filter and sorts torrents
- (void) setFilter: (id) sender
{
    NSString * oldFilterType = [fDefaults stringForKey: @"Filter"];
    
    NSButton * prevFilterButton;
    if ([oldFilterType isEqualToString: FILTER_PAUSE])
        prevFilterButton = fPauseFilterButton;
    else if ([oldFilterType isEqualToString: FILTER_ACTIVE])
        prevFilterButton = fActiveFilterButton;
    else if ([oldFilterType isEqualToString: FILTER_SEED])
        prevFilterButton = fSeedFilterButton;
    else if ([oldFilterType isEqualToString: FILTER_DOWNLOAD])
        prevFilterButton = fDownloadFilterButton;
    else
        prevFilterButton = fNoFilterButton;
    
    if (sender != prevFilterButton)
    {
        [prevFilterButton setState: NSOffState];
        [sender setState: NSOnState];

        NSString * filterType;
        if (sender == fActiveFilterButton)
            filterType = FILTER_ACTIVE;
        else if (sender == fDownloadFilterButton)
            filterType = FILTER_DOWNLOAD;
        else if (sender == fPauseFilterButton)
            filterType = FILTER_PAUSE;
        else if (sender == fSeedFilterButton)
            filterType = FILTER_SEED;
        else
            filterType = FILTER_NONE;

        [fDefaults setObject: filterType forKey: @"Filter"];
    }
    else
        [sender setState: NSOnState];

    [self updateDisplay: nil];
}

- (void) setFilterSearchType: (id) sender
{
    NSString * oldFilterType = [fDefaults stringForKey: @"FilterSearchType"];
    
    int prevTag, currentTag = [sender tag];
    if ([oldFilterType isEqualToString: FILTER_TYPE_TRACKER])
        prevTag = FILTER_TYPE_TAG_TRACKER;
    else
        prevTag = FILTER_TYPE_TAG_NAME;
    
    if (currentTag != prevTag)
    {
        NSString * filterType;
        if (currentTag == FILTER_TYPE_TAG_TRACKER)
            filterType = FILTER_TYPE_TRACKER;
        else
            filterType = FILTER_TYPE_NAME;
        
        [fDefaults setObject: filterType forKey: @"FilterSearchType"];
        
        [[fSearchFilterField cell] setPlaceholderString: [sender title]];
    }
    
    [self updateDisplay: nil];
}

- (void) switchFilter: (id) sender
{
    NSString * filterType = [fDefaults stringForKey: @"Filter"];
    
    NSButton * button;
    if ([filterType isEqualToString: FILTER_NONE])
        button = sender == fNextFilterItem ? fActiveFilterButton : fPauseFilterButton;
    else if ([filterType isEqualToString: FILTER_ACTIVE])
        button = sender == fNextFilterItem ? fDownloadFilterButton : fNoFilterButton;
    else if ([filterType isEqualToString: FILTER_DOWNLOAD])
        button = sender == fNextFilterItem ? fSeedFilterButton : fActiveFilterButton;
    else if ([filterType isEqualToString: FILTER_SEED])
        button = sender == fNextFilterItem ? fPauseFilterButton : fDownloadFilterButton;
    else if ([filterType isEqualToString: FILTER_PAUSE])
        button = sender == fNextFilterItem ? fNoFilterButton : fSeedFilterButton;
    else
        button = fNoFilterButton;
    
    [self setFilter: button];
}

- (void) setStatusLabel: (id) sender
{
    NSString * statusLabel;
    switch ([sender tag])
    {
        case STATUS_RATIO_TOTAL_TAG:
            statusLabel = STATUS_RATIO_TOTAL;
            break;
        case STATUS_RATIO_SESSION_TAG:
            statusLabel = STATUS_RATIO_SESSION;
            break;
        case STATUS_TRANSFER_TOTAL_TAG:
            statusLabel = STATUS_TRANSFER_TOTAL;
            break;
        case STATUS_TRANSFER_SESSION_TAG:
            statusLabel = STATUS_TRANSFER_SESSION;
            break;
        default:
            return;
    }
    
    [fDefaults setObject: statusLabel forKey: @"StatusLabel"];
    [self updateUI];
}

- (void) showGroups: (id) sender
{
    [[GroupsWindowController groups] showWindow: self];
}

- (void) menuNeedsUpdate: (NSMenu *) menu
{
    if (menu == fGroupsSetMenu || menu == fGroupsSetContextMenu)
    {
        int i;
        for (i = [menu numberOfItems]-1 - 2; i >= 0; i--)
            [menu removeItemAtIndex: i];
        
        NSMenu * groupMenu = [[GroupsWindowController groups] groupMenuWithTarget: self action: @selector(setGroup:) isSmall: NO];
        [menu appendItemsFromMenu: groupMenu atIndexes: [NSIndexSet indexSetWithIndexesInRange:
                NSMakeRange(0, [groupMenu numberOfItems])] atBottom: NO];
    }
    else if (menu == fGroupFilterMenu)
    {
        int i;
        for (i = [menu numberOfItems]-1; i >= 3; i--)
            [menu removeItemAtIndex: i];
        
        NSMenu * groupMenu = [[GroupsWindowController groups] groupMenuWithTarget: self action: @selector(setGroupFilter:)
                                isSmall: YES];
        [menu appendItemsFromMenu: groupMenu atIndexes: [NSIndexSet indexSetWithIndexesInRange:
                NSMakeRange(0, [groupMenu numberOfItems])] atBottom: YES];
    }
    else if (menu == fUploadMenu || menu == fDownloadMenu)
    {
        if ([menu numberOfItems] > 3)
            return;
        
        const int speedLimitActionValue[] = { 5, 10, 20, 30, 40, 50, 75, 100, 150, 200, 250, 500, 750, -1 };
        
        NSMenuItem * item;
        int i;
        for (i = 0; speedLimitActionValue[i] != -1; i++)
        {
            item = [[NSMenuItem alloc] initWithTitle: [NSString stringWithFormat: NSLocalizedString(@"%d KB/s",
                    "Action menu -> upload/download limit"), speedLimitActionValue[i]] action: @selector(setQuickLimitGlobal:)
                    keyEquivalent: @""];
            [item setTarget: self];
            [item setRepresentedObject: [NSNumber numberWithInt: speedLimitActionValue[i]]];
            [menu addItem: item];
            [item release];
        }
    }
    else if (menu == fRatioStopMenu)
    {
        if ([menu numberOfItems] > 3)
            return;
        
        const float ratioLimitActionValue[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, -1 };
        
        NSMenuItem * item;
        int i;
        for (i = 0; ratioLimitActionValue[i] != -1; i++)
        {
            item = [[NSMenuItem alloc] initWithTitle: [NSString stringWithFormat: @"%.2f", ratioLimitActionValue[i]]
                    action: @selector(setQuickRatioGlobal:) keyEquivalent: @""];
            [item setTarget: self];
            [item setRepresentedObject: [NSNumber numberWithFloat: ratioLimitActionValue[i]]];
            [menu addItem: item];
            [item release];
        }
    }
    else;
}

- (void) setGroup: (id) sender
{
    NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        [torrent setGroupValue: [sender tag]];
    
    [self updateUI];
    [self updateTorrentHistory];
}

- (void) setGroupFilter: (id) sender
{
    [fDefaults setInteger: [sender tag] forKey: @"FilterGroup"];
    [self updateGroupsFilterButton];
    [self updateDisplay: nil];
}

- (void) updateGroupsFilterButton
{
    int index = [fDefaults integerForKey: @"FilterGroup"];
    
    NSImage * icon = nil;
    NSString * toolTip;
    switch (index)
    {
        case GROUP_FILTER_ALL_TAG:
            icon = [NSImage imageNamed: @"PinTemplate.png"];
            toolTip = NSLocalizedString(@"All Groups", "Groups -> Button");
            break;
        case -1:
            if ([NSApp isOnLeopardOrBetter])
                icon = [NSImage imageNamed: NSImageNameStopProgressTemplate];
            toolTip = NSLocalizedString(@"Group: No Label", "Groups -> Button");
            break;
        default:
            icon = [[GroupsWindowController groups] imageForIndex: index isSmall: YES];
            toolTip = [NSLocalizedString(@"Group: ", "Groups -> Button") stringByAppendingString:
                        [[GroupsWindowController groups] nameForIndex: index]];
    }
    
    [[fGroupFilterMenu itemAtIndex: 0] setImage: icon];
    [fGroupsButton setToolTip: toolTip];
}

- (void) updateGroupsFilters: (NSNotification *) notification
{
    [self updateGroupsFilterButton];
    [self updateDisplay: nil];
}

- (void) toggleSpeedLimit: (id) sender
{
    [fDefaults setBool: ![fDefaults boolForKey: @"SpeedLimit"] forKey: @"SpeedLimit"];
    [fPrefsController applySpeedSettings: nil];
}

- (void) autoSpeedLimitChange: (NSNotification *) notification
{
    if (![fDefaults boolForKey: @"SpeedLimitAuto"])
        return;
 
    NSCalendarDate * onDate = [NSCalendarDate dateWithTimeIntervalSinceReferenceDate:
                        [[fDefaults objectForKey: @"SpeedLimitAutoOnDate"] timeIntervalSinceReferenceDate]],
        * offDate = [NSCalendarDate dateWithTimeIntervalSinceReferenceDate:
                        [[fDefaults objectForKey: @"SpeedLimitAutoOffDate"] timeIntervalSinceReferenceDate]],
        * nowDate = [NSCalendarDate calendarDate];
    
    //check if should be on if within range
    int onTime = [onDate hourOfDay] * 60 + [onDate minuteOfHour],
        offTime = [offDate hourOfDay] * 60 + [offDate minuteOfHour],
        nowTime = [nowDate hourOfDay] * 60 + [nowDate minuteOfHour];
    
    BOOL shouldBeOn = NO;
    if (onTime < offTime)
        shouldBeOn = onTime <= nowTime && nowTime < offTime;
    else if (onTime > offTime)
        shouldBeOn = onTime <= nowTime || nowTime < offTime;
    else;
    
    if ([fDefaults boolForKey: @"SpeedLimit"] != shouldBeOn)
        [self toggleSpeedLimit: nil];
}

- (void) autoSpeedLimit
{
    if (![fDefaults boolForKey: @"SpeedLimitAuto"])
        return;
    
    //only toggle if within first few seconds of minutes
    NSCalendarDate * nowDate = [NSCalendarDate calendarDate];
    if ([nowDate secondOfMinute] > AUTO_SPEED_LIMIT_SECONDS)
        return;
    
    NSCalendarDate * offDate = [NSCalendarDate dateWithTimeIntervalSinceReferenceDate:
                        [[fDefaults objectForKey: @"SpeedLimitAutoOffDate"] timeIntervalSinceReferenceDate]];
    
    BOOL toggle;
    if ([fDefaults boolForKey: @"SpeedLimit"])
        toggle = [nowDate hourOfDay] == [offDate hourOfDay] && [nowDate minuteOfHour] == [offDate minuteOfHour];
    else
    {
        NSCalendarDate * onDate = [NSCalendarDate dateWithTimeIntervalSinceReferenceDate:
                        [[fDefaults objectForKey: @"SpeedLimitAutoOnDate"] timeIntervalSinceReferenceDate]];
        toggle = ([nowDate hourOfDay] == [onDate hourOfDay] && [nowDate minuteOfHour] == [onDate minuteOfHour])
                    && !([onDate hourOfDay] == [offDate hourOfDay] && [onDate minuteOfHour] == [offDate minuteOfHour]);
    }
    
    if (toggle)
    {
        [self toggleSpeedLimit: nil];
        
        [GrowlApplicationBridge notifyWithTitle: [fDefaults boolForKey: @"SpeedLimit"]
                ? NSLocalizedString(@"Speed Limit Auto Enabled", "Growl notification title")
                : NSLocalizedString(@"Speed Limit Auto Disabled", "Growl notification title")
            description: NSLocalizedString(@"Bandwidth settings changed", "Growl notification description")
            notificationName: GROWL_AUTO_SPEED_LIMIT iconData: nil priority: 0 isSticky: NO clickContext: nil];
    }
}

- (void) setLimitGlobalEnabled: (id) sender
{
    [fDefaults setBool: sender == ([sender menu] == fUploadMenu ? fUploadLimitItem : fDownloadLimitItem)
        forKey: [sender menu] == fUploadMenu ? @"CheckUpload" : @"CheckDownload"];
    
    [fPrefsController applySpeedSettings: nil];
}

- (void) setQuickLimitGlobal: (id) sender
{
    [fDefaults setInteger: [[sender representedObject] intValue] forKey: [sender menu] == fUploadMenu
                ? @"UploadLimit" : @"DownloadLimit"];
    [fDefaults setBool: YES forKey: [sender menu] == fUploadMenu ? @"CheckUpload" : @"CheckDownload"];
    
    [fPrefsController updateLimitFields];
    [fPrefsController applySpeedSettings: nil];
}

- (void) setRatioGlobalEnabled: (id) sender
{
    [fDefaults setBool: sender == fCheckRatioItem forKey: @"RatioCheck"];
}

- (void) setQuickRatioGlobal: (id) sender
{
    [fDefaults setBool: YES forKey: @"RatioCheck"];
    [fDefaults setFloat: [[sender representedObject] floatValue] forKey: @"RatioLimit"];
    
    [fPrefsController updateRatioStopField];
}

- (void) torrentStoppedForRatio: (NSNotification *) notification
{
    Torrent * torrent = [notification object];
    
    [self updateTorrentsInQueue];
    [fInfoController updateInfoStats];
    [fInfoController updateOptions];
    
    if ([fDefaults boolForKey: @"PlaySeedingSound"])
    {
        NSSound * sound;
        if ((sound = [NSSound soundNamed: [fDefaults stringForKey: @"SeedingSound"]]))
            [sound play];
    }
    
    NSDictionary * clickContext = [NSDictionary dictionaryWithObjectsAndKeys: GROWL_SEEDING_COMPLETE, @"Type",
                                    [torrent dataLocation], @"Location", nil];
    [GrowlApplicationBridge notifyWithTitle: NSLocalizedString(@"Seeding Complete", "Growl notification title")
                        description: [torrent name] notificationName: GROWL_SEEDING_COMPLETE
                        iconData: nil priority: 0 isSticky: NO clickContext: clickContext];
}

-(void) watcher: (id<UKFileWatcher>) watcher receivedNotification: (NSString *) notification forPath: (NSString *) path
{
    if ([notification isEqualToString: UKFileWatcherWriteNotification])
    {
        if (![fDefaults boolForKey: @"AutoImport"] || ![fDefaults stringForKey: @"AutoImportDirectory"])
            return;
        
        if (fAutoImportTimer)
        {
            if ([fAutoImportTimer isValid])
                [fAutoImportTimer invalidate];
            [fAutoImportTimer release];
            fAutoImportTimer = nil;
        }
        
        //check again in 10 seconds in case torrent file wasn't complete
        fAutoImportTimer = [[NSTimer scheduledTimerWithTimeInterval: 10.0 target: self 
            selector: @selector(checkAutoImportDirectory) userInfo: nil repeats: NO] retain];
        
        [self checkAutoImportDirectory];
    }
}

- (void) changeAutoImport
{
    if (fAutoImportTimer)
    {
        if ([fAutoImportTimer isValid])
            [fAutoImportTimer invalidate];
        [fAutoImportTimer release];
        fAutoImportTimer = nil;
    }
    
    if (fAutoImportedNames)
    {
        [fAutoImportedNames release];
        fAutoImportedNames = nil;
    }
    
    [self checkAutoImportDirectory];
}

- (void) checkAutoImportDirectory
{
    NSString * path;
    if (![fDefaults boolForKey: @"AutoImport"] || !(path = [fDefaults stringForKey: @"AutoImportDirectory"]))
        return;
    
    path = [path stringByExpandingTildeInPath];
    
    NSArray * importedNames;
    if (!(importedNames = [[NSFileManager defaultManager] directoryContentsAtPath: path]))
        return;
    
    //only check files that have not been checked yet
    NSMutableArray * newNames = [importedNames mutableCopy];
    
    if (fAutoImportedNames)
        [newNames removeObjectsInArray: fAutoImportedNames];
    else
        fAutoImportedNames = [[NSMutableArray alloc] init];
    [fAutoImportedNames setArray: importedNames];
    
    NSString * file;
    int i;
    for (i = [newNames count] - 1; i >= 0; i--)
    {
        file = [newNames objectAtIndex: i];
        if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] != NSOrderedSame)
            [newNames removeObjectAtIndex: i];
        else
            [newNames replaceObjectAtIndex: i withObject: [path stringByAppendingPathComponent: file]];
    }
    
    NSEnumerator * enumerator = [newNames objectEnumerator];
    tr_ctor * ctor;
    while ((file = [enumerator nextObject]))
    {
        ctor = tr_ctorNew(fLib);
        tr_ctorSetMetainfoFromFile(ctor, [file UTF8String]);
        
        switch (tr_torrentParse(fLib, ctor, NULL))
        {
            case TR_OK:
                [self openFiles: [NSArray arrayWithObject: file] addType: ADD_NORMAL forcePath: nil];
                
                [GrowlApplicationBridge notifyWithTitle: NSLocalizedString(@"Torrent File Auto Added", "Growl notification title")
                    description: [file lastPathComponent] notificationName: GROWL_AUTO_ADD iconData: nil priority: 0 isSticky: NO
                    clickContext: nil];
                break;
            
            case TR_EINVALID:
                [fAutoImportedNames removeObject: [file lastPathComponent]];
        }
        
        tr_ctorFree(ctor);
    }
    
    [newNames release];
}

- (void) beginCreateFile: (NSNotification *) notification
{
    if (![fDefaults boolForKey: @"AutoImport"])
        return;
    
    NSString * location = [notification object],
            * path = [fDefaults stringForKey: @"AutoImportDirectory"];
    
    if (location && path && [[[location stringByDeletingLastPathComponent] stringByExpandingTildeInPath]
                                    isEqualToString: [path stringByExpandingTildeInPath]])
        [fAutoImportedNames addObject: [location lastPathComponent]];
}

- (int) numberOfRowsInTableView: (NSTableView *) tableview
{
    return [fDisplayedTorrents count];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) tableColumn row: (int) row
{
    if (![fDisplayedGroupIndexes containsIndex: row])
        return nil;
    
    int group = [[fDisplayedTorrents objectAtIndex: row] intValue];
    return group != -1 ? [[GroupsWindowController groups] nameForIndex: group] : NSLocalizedString(@"No Group", "Group table row");
}

- (BOOL) tableView: (NSTableView *) tableView writeRowsWithIndexes: (NSIndexSet *) indexes toPasteboard: (NSPasteboard *) pasteboard
{
    //only allow reordering of rows if sorting by order
    if ([[fDefaults stringForKey: @"Sort"] isEqualToString: SORT_ORDER])
    {
        [pasteboard declareTypes: [NSArray arrayWithObject: TORRENT_TABLE_VIEW_DATA_TYPE] owner: self];
        [pasteboard setData: [NSKeyedArchiver archivedDataWithRootObject: indexes] forType: TORRENT_TABLE_VIEW_DATA_TYPE];
        return YES;
    }
    return NO;
}

- (NSDragOperation) tableView: (NSTableView *) tableView validateDrop: (id <NSDraggingInfo>) info
    proposedRow: (int) row proposedDropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: TORRENT_TABLE_VIEW_DATA_TYPE])
    {
        [fTableView setDropRow: row dropOperation: NSTableViewDropAbove];
        return NSDragOperationGeneric;
    }
    
    return NSDragOperationNone;
}

- (BOOL) tableView: (NSTableView *) t acceptDrop: (id <NSDraggingInfo>) info
    row: (int) newRow dropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: TORRENT_TABLE_VIEW_DATA_TYPE])
    {
        //remember selected rows if needed
        NSArray * selectedValues = [fTableView selectedValues];
    
        NSIndexSet * indexes = [NSKeyedUnarchiver unarchiveObjectWithData:
                                [pasteboard dataForType: TORRENT_TABLE_VIEW_DATA_TYPE]];
        
        //determine where to move them
        int i, originalRow = newRow;
        for (i = [indexes firstIndex]; i < originalRow && i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
            newRow--;
        
        //reinsert into array
        int insertIndex = newRow > 0 ? [[fDisplayedTorrents objectAtIndex: newRow-1] orderValue] + 1 : 0;
        
        //get all torrents to reorder
        NSSortDescriptor * orderDescriptor = [[[NSSortDescriptor alloc] initWithKey:
                                                @"orderValue" ascending: YES] autorelease];
        
        NSMutableArray * sortedTorrents = [[fTorrents sortedArrayUsingDescriptors:
                                            [NSArray arrayWithObject: orderDescriptor]] mutableCopy];
        
        //remove objects to reinsert
        NSArray * movingTorrents = [[fDisplayedTorrents objectsAtIndexes: indexes] retain];
        [sortedTorrents removeObjectsInArray: movingTorrents];
        
        //insert objects at new location
        for (i = 0; i < [movingTorrents count]; i++)
            [sortedTorrents insertObject: [movingTorrents objectAtIndex: i] atIndex: insertIndex + i];
        
        [movingTorrents release];
        
        //redo order values
        i = 0;
        for (i = 0; i < [sortedTorrents count]; i++)
            [[sortedTorrents objectAtIndex: i] setOrderValue: i];
        
        [sortedTorrents release];
        
        [self updateDisplay: nil];
        
        //set selected rows
        [fTableView selectValues: selectedValues];
    }
    
    return YES;
}

- (void) torrentTableViewSelectionDidChange: (NSNotification *) notification
{
    [fInfoController setInfoForTorrents: [fTableView selectedTorrents]];
}

- (NSDragOperation) draggingEntered: (id <NSDraggingInfo>) info
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: NSFilenamesPboardType])
    {
        //check if any torrent files can be added
        NSArray * files = [pasteboard propertyListForType: NSFilenamesPboardType];
        NSEnumerator * enumerator = [files objectEnumerator];
        NSString * file;
        BOOL torrent = NO;
        tr_ctor * ctor;
        while ((file = [enumerator nextObject]))
        {
            if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
            {
                ctor = tr_ctorNew(fLib);
                tr_ctorSetMetainfoFromFile(ctor, [file UTF8String]);
                switch (tr_torrentParse(fLib, ctor, NULL))
                {
                    case TR_OK:
                        if (!fOverlayWindow)
                            fOverlayWindow = [[DragOverlayWindow alloc] initWithLib: fLib forWindow: fWindow];
                        [fOverlayWindow setTorrents: files];
                        
                        return NSDragOperationCopy;
                    
                    case TR_EDUPLICATE:
                        torrent = YES;
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
        NSMutableArray * filesToOpen = [[NSMutableArray alloc] init];
        NSArray * files = [pasteboard propertyListForType: NSFilenamesPboardType];
        NSEnumerator * enumerator = [files objectEnumerator];
        NSString * file;
        tr_ctor * ctor;
        while ((file = [enumerator nextObject]))
        {
            if ([[file pathExtension] caseInsensitiveCompare: @"torrent"] == NSOrderedSame)
            {
                ctor = tr_ctorNew(fLib);
                tr_ctorSetMetainfoFromFile(ctor, [file UTF8String]);
                switch (tr_torrentParse(fLib, ctor, NULL))
                {
                    case TR_OK:
                        [filesToOpen addObject: file];
                        torrent = YES;
                        break;
                        
                    case TR_EDUPLICATE:
                        torrent = YES;
                }
                tr_ctorFree(ctor);
            }
        }
        
        if ([filesToOpen count] > 0)
            [self application: NSApp openFiles: filesToOpen];
        else
        {
            if (!torrent && [files count] == 1)
                [CreatorWindowController createTorrentFile: fLib forFile: [files objectAtIndex: 0]];
            else
                accept = NO;
        }
        [filesToOpen release];
        
        return accept;
    }
    else if ([[pasteboard types] containsObject: NSURLPboardType])
    {
        NSURL * url;
        if ((url = [NSURL URLFromPasteboard: pasteboard]))
        {
            [self openURL: url];
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
    
    [fTableView setRowHeight: makeSmall ? ROW_HEIGHT_SMALL : ROW_HEIGHT_REGULAR];
    
    //window min height
    NSSize contentMinSize = [fWindow contentMinSize],
            contentSize = [[fWindow contentView] frame].size;
    contentMinSize.height = contentSize.height - [fScrollView frame].size.height
                            + [fTableView rowHeight] + [fTableView intercellSpacing].height;
    [fWindow setContentMinSize: contentMinSize];
    
    //resize for larger min height if not set to auto size
    if (![fDefaults boolForKey: @"AutoSize"])
    {
        if (!makeSmall && contentSize.height < contentMinSize.height)
        {
            NSRect frame = [fWindow frame];
            float heightChange = contentMinSize.height - contentSize.height;
            frame.size.height += heightChange;
            frame.origin.y -= heightChange;
            
            [fWindow setFrame: frame display: YES];
            [fTableView reloadData];
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

- (void) toggleStatusBar: (id) sender
{
    [self showStatusBar: [fStatusBar isHidden] animate: YES];
    [fDefaults setBool: ![fStatusBar isHidden] forKey: @"StatusBar"];
}

- (NSRect) windowFrameByAddingHeight: (float) height checkLimits: (BOOL) check
{
    //convert pixels to points
    NSRect windowFrame = [fWindow frame];
    NSSize windowSize = [fScrollView convertSize: windowFrame.size fromView: nil];
    windowSize.height += height;
    
    if (check)
    {
        NSSize minSize = [fScrollView convertSize: [fWindow minSize] fromView: nil];
        
        if (windowSize.height < minSize.height)
            windowSize.height = minSize.height;
        else
        {
            NSSize maxSize = [fScrollView convertSize: [[fWindow screen] visibleFrame].size fromView: nil];
            if ([fStatusBar isHidden])
                maxSize.height -= [fStatusBar frame].size.height;
            if ([fFilterBar isHidden]) 
                maxSize.height -= [fFilterBar frame].size.height;
            if (windowSize.height > maxSize.height)
                windowSize.height = maxSize.height;
        }
    }

    //convert points to pixels
    windowSize = [fScrollView convertSize: windowSize toView: nil];

    windowFrame.origin.y -= (windowSize.height - windowFrame.size.height);
    windowFrame.size.height = windowSize.height;
    return windowFrame;
}

- (void) showStatusBar: (BOOL) show animate: (BOOL) animate
{
    if (show != [fStatusBar isHidden])
        return;

    if (show)
        [fStatusBar setHidden: NO];

    NSRect frame;
    float heightChange = [fStatusBar frame].size.height;
    if (!show)
        heightChange *= -1;
    
    //allow bar to show even if not enough room
    if (show && ![fDefaults boolForKey: @"AutoSize"])
    {
        frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];
        float change = [[fWindow screen] visibleFrame].size.height - frame.size.height;
        if (change < 0.0)
        {
            frame = [fWindow frame];
            frame.size.height += change;
            frame.origin.y -= change;
            [fWindow setFrame: frame display: NO animate: NO];
        }
    }

    [self updateUI];
    
    //set views to not autoresize
    unsigned int statsMask = [fStatusBar autoresizingMask];
    unsigned int filterMask = [fFilterBar autoresizingMask];
    unsigned int scrollMask = [fScrollView autoresizingMask];
    [fStatusBar setAutoresizingMask: NSViewNotSizable];
    [fFilterBar setAutoresizingMask: NSViewNotSizable];
    [fScrollView setAutoresizingMask: NSViewNotSizable];
    
    frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];
    [fWindow setFrame: frame display: YES animate: animate]; 
    
    //re-enable autoresize
    [fStatusBar setAutoresizingMask: statsMask];
    [fFilterBar setAutoresizingMask: filterMask];
    [fScrollView setAutoresizingMask: scrollMask];
    
    //change min size
    NSSize minSize = [fWindow contentMinSize];
    minSize.height += heightChange;
    [fWindow setContentMinSize: minSize];
    
    if (!show)
        [fStatusBar setHidden: YES];
}

- (void) toggleFilterBar: (id) sender
{
    //disable filtering when hiding
    if (![fFilterBar isHidden])
    {
        [fSearchFilterField setStringValue: @""];
        [self setFilter: fNoFilterButton];
        [self setGroupFilter: [fGroupFilterMenu itemWithTag: GROUP_FILTER_ALL_TAG]];
    }

    [self showFilterBar: [fFilterBar isHidden] animate: YES];
    [fDefaults setBool: ![fFilterBar isHidden] forKey: @"FilterBar"];
}

- (void) showFilterBar: (BOOL) show animate: (BOOL) animate
{
    if (show != [fFilterBar isHidden])
        return;

    if (show)
        [fFilterBar setHidden: NO];

    NSRect frame;
    float heightChange = [fFilterBar frame].size.height;
    if (!show)
        heightChange *= -1;
    
    //allow bar to show even if not enough room
    if (show && ![fDefaults boolForKey: @"AutoSize"])
    {
        frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];
        float change = [[fWindow screen] visibleFrame].size.height - frame.size.height;
        if (change < 0.0)
        {
            frame = [fWindow frame];
            frame.size.height += change;
            frame.origin.y -= change;
            [fWindow setFrame: frame display: NO animate: NO];
        }
    }

    //set views to not autoresize
    unsigned int filterMask = [fFilterBar autoresizingMask];
    unsigned int scrollMask = [fScrollView autoresizingMask];
    [fFilterBar setAutoresizingMask: NSViewNotSizable];
    [fScrollView setAutoresizingMask: NSViewNotSizable];
    
    frame = [self windowFrameByAddingHeight: heightChange checkLimits: NO];
    [fWindow setFrame: frame display: YES animate: animate];
    
    //re-enable autoresize
    [fFilterBar setAutoresizingMask: filterMask];
    [fScrollView setAutoresizingMask: scrollMask];
    
    //change min size
    NSSize minSize = [fWindow contentMinSize];
    minSize.height += heightChange;
    [fWindow setContentMinSize: minSize];
    
    if (!show)
    {
        [fFilterBar setHidden: YES];
        [fWindow makeFirstResponder: fTableView];
    }
}

- (ButtonToolbarItem *) standardToolbarButtonWithIdentifier: (NSString *) ident
{
    ButtonToolbarItem * item = [[ButtonToolbarItem alloc] initWithItemIdentifier: ident];
    
    NSButton * button = [[NSButton alloc] initWithFrame: NSZeroRect];
    [button setBezelStyle: NSTexturedRoundedBezelStyle];
    [button setStringValue: @""];
    
    [item setView: button];
    [button release];
    
    NSSize buttonSize = NSMakeSize(36.0, 25.0);
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
        [item setImage: [NSImage imageNamed: @"Create.png"]];
        [item setTarget: self];
        [item setAction: @selector(createFile:)];
        [item setAutovalidates: NO];
        
        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_OPEN])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        
        [item setLabel: NSLocalizedString(@"Open", "Open toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Open Torrent Files", "Open toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Open torrent files", "Open toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"Open.png"]];
        [item setTarget: self];
        [item setAction: @selector(openShowSheet:)];
        [item setAutovalidates: NO];
        
        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_REMOVE])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        
        [item setLabel: NSLocalizedString(@"Remove", "Remove toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Remove Selected", "Remove toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Remove selected transfers", "Remove toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"Remove.png"]];
        [item setTarget: self];
        [item setAction: @selector(removeNoDelete:)];
        
        return item;
    }
    else if ([ident isEqualToString: TOOLBAR_INFO])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        
        [item setLabel: NSLocalizedString(@"Inspector", "Inspector toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Toggle Inspector", "Inspector toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Toggle the torrent inspector", "Inspector toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"Info.png"]];
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
        
        [segmentedControl setSegmentCount: 2];
        [segmentedCell setTrackingMode: NSSegmentSwitchTrackingMomentary];
        
        NSSize groupSize = NSMakeSize(72.0, 25.0);
        [groupItem setMinSize: groupSize];
        [groupItem setMaxSize: groupSize];
        
        [groupItem setLabel: NSLocalizedString(@"Apply All", "All toolbar item -> label")];
        [groupItem setPaletteLabel: NSLocalizedString(@"Pause / Resume All", "All toolbar item -> palette label")];
        [groupItem setTarget: self];
        [groupItem setAction: @selector(allToolbarClicked:)];
        
        [groupItem setIdentifiers: [NSArray arrayWithObjects: TOOLBAR_PAUSE_ALL, TOOLBAR_RESUME_ALL, nil]];
        
        [segmentedCell setTag: TOOLBAR_PAUSE_TAG forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"PauseAll.png"] forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Pause all transfers",
                                    "All toolbar item -> tooltip") forSegment: TOOLBAR_PAUSE_TAG];
        
        [segmentedCell setTag: TOOLBAR_RESUME_TAG forSegment: TOOLBAR_RESUME_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"ResumeAll.png"] forSegment: TOOLBAR_RESUME_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Resume all transfers",
                                    "All toolbar item -> tooltip") forSegment: TOOLBAR_RESUME_TAG];
        
        [groupItem createMenu: [NSArray arrayWithObjects: NSLocalizedString(@"Pause All", "All toolbar item -> label"),
                                        NSLocalizedString(@"Resume All", "All toolbar item -> label"), nil]];
        
        [segmentedControl release];
        return [groupItem autorelease];
    }
    else if ([ident isEqualToString: TOOLBAR_PAUSE_RESUME_SELECTED])
    {
        GroupToolbarItem * groupItem = [[GroupToolbarItem alloc] initWithItemIdentifier: ident];
        
        NSSegmentedControl * segmentedControl = [[NSSegmentedControl alloc] initWithFrame: NSZeroRect];
        [segmentedControl setCell: [[[ToolbarSegmentedCell alloc] init] autorelease]];
        [groupItem setView: segmentedControl];
        NSSegmentedCell * segmentedCell = (NSSegmentedCell *)[segmentedControl cell];
        
        [segmentedControl setSegmentCount: 2];
        [segmentedCell setTrackingMode: NSSegmentSwitchTrackingMomentary];
        
        NSSize groupSize = NSMakeSize(72.0, 25.0);
        [groupItem setMinSize: groupSize];
        [groupItem setMaxSize: groupSize];
        
        [groupItem setLabel: NSLocalizedString(@"Apply Selected", "Selected toolbar item -> label")];
        [groupItem setPaletteLabel: NSLocalizedString(@"Pause / Resume Selected", "Selected toolbar item -> palette label")];
        [groupItem setTarget: self];
        [groupItem setAction: @selector(selectedToolbarClicked:)];
        
        [groupItem setIdentifiers: [NSArray arrayWithObjects: TOOLBAR_PAUSE_SELECTED, TOOLBAR_RESUME_SELECTED, nil]];
        
        [segmentedCell setTag: TOOLBAR_PAUSE_TAG forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"PauseSelected.png"] forSegment: TOOLBAR_PAUSE_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Pause selected transfers",
                                    "Selected toolbar item -> tooltip") forSegment: TOOLBAR_PAUSE_TAG];
        
        [segmentedCell setTag: TOOLBAR_RESUME_TAG forSegment: TOOLBAR_RESUME_TAG];
        [segmentedControl setImage: [NSImage imageNamed: @"ResumeSelected.png"] forSegment: TOOLBAR_RESUME_TAG];
        [segmentedCell setToolTip: NSLocalizedString(@"Resume selected transfers",
                                    "Selected toolbar item -> tooltip") forSegment: TOOLBAR_RESUME_TAG];
        
        [groupItem createMenu: [NSArray arrayWithObjects: NSLocalizedString(@"Pause Selected", "Selected toolbar item -> label"),
                                        NSLocalizedString(@"Resume Selected", "Selected toolbar item -> label"), nil]];
        
        [segmentedControl release];
        return [groupItem autorelease];
    }
    else if ([ident isEqualToString: TOOLBAR_FILTER])
    {
        ButtonToolbarItem * item = [self standardToolbarButtonWithIdentifier: ident];
        
        [item setLabel: NSLocalizedString(@"Filter", "Filter toolbar item -> label")];
        [item setPaletteLabel: NSLocalizedString(@"Toggle Filter", "Filter toolbar item -> palette label")];
        [item setToolTip: NSLocalizedString(@"Toggle the filter bar", "Filter toolbar item -> tooltip")];
        [item setImage: [NSImage imageNamed: @"Filter.png"]];
        [item setTarget: self];
        [item setAction: @selector(toggleFilterBar:)];
        
        return item;
    }
    else
        return nil;
}

- (void) allToolbarClicked: (id) sender
{
    int tagValue = [sender isKindOfClass: [NSSegmentedControl class]]
                    ? [(NSSegmentedCell *)[sender cell] tagForSegment: [sender selectedSegment]] : [sender tag];
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
    int tagValue = [sender isKindOfClass: [NSSegmentedControl class]]
                    ? [(NSSegmentedCell *)[sender cell] tagForSegment: [sender selectedSegment]] : [sender tag];
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
    return [NSArray arrayWithObjects:
            TOOLBAR_CREATE, TOOLBAR_OPEN, TOOLBAR_REMOVE,
            TOOLBAR_PAUSE_RESUME_SELECTED, TOOLBAR_PAUSE_RESUME_ALL,
            TOOLBAR_FILTER, TOOLBAR_INFO,
            NSToolbarSeparatorItemIdentifier,
            NSToolbarSpaceItemIdentifier,
            NSToolbarFlexibleSpaceItemIdentifier,
            NSToolbarCustomizeToolbarItemIdentifier, nil];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) toolbar
{
    return [NSArray arrayWithObjects:
            TOOLBAR_CREATE, TOOLBAR_OPEN, TOOLBAR_REMOVE,
            NSToolbarSeparatorItemIdentifier,
            TOOLBAR_PAUSE_RESUME_ALL,
            NSToolbarFlexibleSpaceItemIdentifier,
            TOOLBAR_FILTER, TOOLBAR_INFO, nil];
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
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable resume all item
    if ([ident isEqualToString: TOOLBAR_RESUME_ALL])
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if (![torrent isActive] && ![torrent waitingToStart])
                return YES;
        return NO;
    }

    //enable pause item
    if ([ident isEqualToString: TOOLBAR_PAUSE_SELECTED])
    {
        NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable resume item
    if ([ident isEqualToString: TOOLBAR_RESUME_SELECTED])
    {
        NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if (![torrent isActive] && ![torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //set info image
    if ([ident isEqualToString: TOOLBAR_INFO])
    {
        [toolbarItem setImage: [[fInfoController window] isVisible] ? [NSImage imageNamed: @"InfoBlue.png"]
                                                                    : [NSImage imageNamed: @"Info.png"]];
        return YES;
    }
    
    //set filter image
    if ([ident isEqualToString: TOOLBAR_FILTER])
    {
        [toolbarItem setImage: ![fFilterBar isHidden] ? [NSImage imageNamed: @"FilterBlue.png"] : [NSImage imageNamed: @"Filter.png"]];
        return YES;
    }

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
            default:
                sortType = @"";
        }
        
        [menuItem setState: [sortType isEqualToString: [fDefaults stringForKey: @"Sort"]] ? NSOnState : NSOffState];
        return [fWindow isVisible];
    }
    
    //enable sort options
    if (action == @selector(setStatusLabel:))
    {
        NSString * statusLabel;
        switch ([menuItem tag])
        {
            case STATUS_RATIO_TOTAL_TAG:
                statusLabel = STATUS_RATIO_TOTAL;
                break;
            case STATUS_RATIO_SESSION_TAG:
                statusLabel = STATUS_RATIO_SESSION;
                break;
            case STATUS_TRANSFER_TOTAL_TAG:
                statusLabel = STATUS_TRANSFER_TOTAL;
                break;
            case STATUS_TRANSFER_SESSION_TAG:
                statusLabel = STATUS_TRANSFER_SESSION;
                break;
            default:
                statusLabel = @"";;
        }
        
        [menuItem setState: [statusLabel isEqualToString: [fDefaults stringForKey: @"StatusLabel"]] ? NSOnState : NSOffState];
        return YES;
    }
    
    if (action == @selector(setGroup:))
    {
        BOOL checked = NO;
        
        int index = [menuItem tag];
        NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if (index == [torrent groupValue])
            {
                checked = YES;
                break;
            }
        
        [menuItem setState: checked ? NSOnState : NSOffState];
        return canUseTable && [fTableView numberOfSelectedRows] > 0;
    }
    
    if (action == @selector(setGroupFilter:))
    {
        [menuItem setState: [menuItem tag] == [fDefaults integerForKey: @"FilterGroup"] ? NSOnState : NSOffState];
        return YES;
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
    
    if (action == @selector(setLimitGlobalEnabled:))
    {
        BOOL upload = [menuItem menu] == fUploadMenu;
        BOOL limit = menuItem == (upload ? fUploadLimitItem : fDownloadLimitItem);
        if (limit)
            [menuItem setTitle: [NSString stringWithFormat: NSLocalizedString(@"Limit (%d KB/s)",
                                    "Action menu -> upload/download limit"),
                                    [fDefaults integerForKey: upload ? @"UploadLimit" : @"DownloadLimit"]]];
        
        [menuItem setState: [fDefaults boolForKey: upload ? @"CheckUpload" : @"CheckDownload"] ? limit : !limit];
        return YES;
    }
    
    if (action == @selector(setRatioGlobalEnabled:))
    {
        BOOL check = menuItem == fCheckRatioItem;
        if (check)
            [menuItem setTitle: [NSString stringWithFormat: NSLocalizedString(@"Stop at Ratio (%.2f)",
                                    "Action menu -> ratio stop"), [fDefaults floatForKey: @"RatioLimit"]]];
        
        [menuItem setState: [fDefaults boolForKey: @"RatioCheck"] ? check : !check];
        return YES;
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
        NSString * title = [fStatusBar isHidden] ? NSLocalizedString(@"Show Status Bar", "View menu -> Status Bar")
                            : NSLocalizedString(@"Hide Status Bar", "View menu -> Status Bar");
        [menuItem setTitle: title];

        return [fWindow isVisible];
    }
    
    //enable toggle filter bar
    if (action == @selector(toggleFilterBar:))
    {
        NSString * title = [fFilterBar isHidden] ? NSLocalizedString(@"Show Filter Bar", "View menu -> Filter Bar")
                            : NSLocalizedString(@"Hide Filter Bar", "View menu -> Filter Bar");
        [menuItem setTitle: title];

        return [fWindow isVisible];
    }
    
    //enable prev/next filter button
    if (action == @selector(switchFilter:))
        return [fWindow isVisible] && ![fFilterBar isHidden];

    //enable reveal in finder
    if (action == @selector(revealFile:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;

    //enable remove items
    if (action == @selector(removeNoDelete:) || action == @selector(removeDeleteData:)
        || action == @selector(removeDeleteTorrent:) || action == @selector(removeDeleteDataAndTorrent:))
    {
        BOOL warning = NO,
            onlyDownloading = [fDefaults boolForKey: @"CheckRemoveDownloading"],
            canDelete = action != @selector(removeDeleteTorrent:) && action != @selector(removeDeleteDataAndTorrent:);
        
        NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
        {
            if (!warning && [torrent isActive])
            {
                warning = onlyDownloading ? ![torrent isSeeding] : YES;
                if (warning && canDelete)
                    break;
            }
            if (!canDelete && [torrent publicTorrent])
            {
                canDelete = YES;
                if (warning)
                    break;
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
        
        return canUseTable && canDelete && [fTableView numberOfSelectedRows] > 0;
    }

    //enable pause all item
    if (action == @selector(stopAllTorrents:))
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable resume all item
    if (action == @selector(resumeAllTorrents:))
    {
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if (![torrent isActive] && ![torrent waitingToStart])
                return YES;
        return NO;
    }
    
    #warning hide queue options if all queues are disabled?
    
    //enable resume all waiting item
    if (action == @selector(resumeWaitingTorrents:))
    {
        if (![fDefaults boolForKey: @"Queue"] && ![fDefaults boolForKey: @"QueueSeed"])
            return NO;
    
        Torrent * torrent;
        NSEnumerator * enumerator = [fTorrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
            if (![torrent isActive] && [torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable resume selected waiting item
    if (action == @selector(resumeSelectedTorrentsNoWait:))
    {
        if (!canUseTable)
            return NO;
        
        NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if (![torrent isActive])
                return YES;
        return NO;
    }

    //enable pause item
    if (action == @selector(stopSelectedTorrents:))
    {
        if (!canUseTable)
            return NO;
    
        NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if ([torrent isActive] || [torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable resume item
    if (action == @selector(resumeSelectedTorrents:))
    {
        if (!canUseTable)
            return NO;
    
        NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if (![torrent isActive] && ![torrent waitingToStart])
                return YES;
        return NO;
    }
    
    //enable manual announce item
    if (action == @selector(announceSelectedTorrents:))
    {
        if (!canUseTable)
            return NO;
        
        NSEnumerator * enumerator = [[fTableView selectedTorrents] objectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if ([torrent canManualAnnounce])
                return YES;
        return NO;
    }
    
    //enable reset cache item
    if (action == @selector(resetCacheForSelectedTorrents:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;
    
    //enable move torrent file item
    if (action == @selector(moveDataFiles:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;
    
    //enable copy torrent file item
    if (action == @selector(copyTorrentFiles:))
        return canUseTable && [fTableView numberOfSelectedRows] > 0;
    
    //enable reverse sort item
    if (action == @selector(setSortReverse:))
    {
        [menuItem setState: [fDefaults boolForKey: @"SortReverse"] ? NSOnState : NSOffState];
        return ![[fDefaults stringForKey: @"Sort"] isEqualToString: SORT_ORDER];
    }
    
    //enable group sort item
    if (action == @selector(setSortByGroup:))
    {
        [menuItem setState: [fDefaults boolForKey: @"SortByGroup"] ? NSOnState : NSOffState];
        return ![[fDefaults stringForKey: @"Sort"] isEqualToString: SORT_ORDER];
    }
    
    //check proper filter search item
    if (action == @selector(setFilterSearchType:))
    {
        NSString * filterType = [fDefaults stringForKey: @"FilterSearchType"];
        
        BOOL state;
        if ([menuItem tag] == FILTER_TYPE_TAG_TRACKER)
            state = [filterType isEqualToString: FILTER_TYPE_TRACKER];
        else
            state = [filterType isEqualToString: FILTER_TYPE_NAME];
        
        [menuItem setState: state ? NSOnState : NSOffState];
        return YES;
    }
    
    return YES;
}

- (void) sleepCallBack: (natural_t) messageType argument: (void *) messageArgument
{
    NSEnumerator * enumerator;
    Torrent * torrent;
    BOOL allowSleep;

    switch (messageType)
    {
        case kIOMessageSystemWillSleep:
            //close all connections before going to sleep and remember we should resume when we wake up
            [fTorrents makeObjectsPerformSelector: @selector(sleep)];

            //wait for running transfers to stop (5 second timeout)
            NSDate * start = [NSDate date];
            BOOL timeUp = NO;
            
            enumerator = [fTorrents objectEnumerator];
            while (!timeUp && (torrent = [enumerator nextObject]))
                while ([torrent isActive] && !(timeUp = [start timeIntervalSinceNow] < -5.0))
                {
                    usleep(100000);
                    [torrent update];
                }

            IOAllowPowerChange(fRootPort, (long) messageArgument);
            break;

        case kIOMessageCanSystemSleep:
            allowSleep = YES;
            if ([fDefaults boolForKey: @"SleepPrevent"])
            {
                //prevent idle sleep unless no torrents are active
                enumerator = [fTorrents objectEnumerator];
                while ((torrent = [enumerator nextObject]))
                    if ([torrent isActive] && ![torrent isStalled] && ![torrent isError])
                    {
                        allowSleep = NO;
                        break;
                    }
            }

            if (allowSleep)
                IOAllowPowerChange(fRootPort, (long) messageArgument);
            else
                IOCancelPowerChange(fRootPort, (long) messageArgument);
            break;

        case kIOMessageSystemHasPoweredOn:
            //resume sleeping transfers after we wake up
            [fTorrents makeObjectsPerformSelector: @selector(wakeUp)];
            [self autoSpeedLimitChange: nil];
            break;
    }
}

- (NSMenu *) applicationDockMenu: (NSApplication *) sender
{
    int seeding = 0, downloading = 0;
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
    {
        if ([torrent isSeeding])
            seeding++;
        else if ([torrent isActive])
            downloading++;
        else;
    }
    
    NSMenuItem * seedingItem = [fDockMenu itemWithTag: DOCK_SEEDING_TAG],
            * downloadingItem = [fDockMenu itemWithTag: DOCK_DOWNLOADING_TAG];
    
    BOOL hasSeparator = seedingItem || downloadingItem;
    
    if (seeding > 0)
    {
        NSString * title = [NSString stringWithFormat: NSLocalizedString(@"%d Seeding",
                                                        "Dock item - Seeding"), seeding];
        if (!seedingItem)
        {
            seedingItem = [[[NSMenuItem alloc] initWithTitle: title action: nil keyEquivalent: @""] autorelease];
            [seedingItem setTag: DOCK_SEEDING_TAG];
            [fDockMenu insertItem: seedingItem atIndex: 0];
        }
        else
            [seedingItem setTitle: title];
    }
    else
    {
        if (seedingItem)
            [fDockMenu removeItem: seedingItem];
    }
    
    if (downloading > 0)
    {
        NSString * title = [NSString stringWithFormat: NSLocalizedString(@"%d Downloading",
                                                        "Dock item - Downloading"), downloading];
        if (!downloadingItem)
        {
            downloadingItem = [[[NSMenuItem alloc] initWithTitle: title action: nil keyEquivalent: @""] autorelease];
            [downloadingItem setTag: DOCK_DOWNLOADING_TAG];
            [fDockMenu insertItem: downloadingItem atIndex: seeding > 0 ? 1 : 0];
        }
        else
            [downloadingItem setTitle: title];
    }
    else
    {
        if (downloadingItem)
            [fDockMenu removeItem: downloadingItem];
    }
    
    if (seeding > 0 || downloading > 0)
    {
        if (!hasSeparator)
            [fDockMenu insertItem: [NSMenuItem separatorItem] atIndex: seeding > 0 && downloading > 0 ? 2 : 1];
    }
    else
    {
        if (hasSeparator)
            [fDockMenu removeItemAtIndex: 0];
    }
    
    return fDockMenu;
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
        [fScrollView setHasVerticalScroller: NO];
        [fWindow setFrame: [self sizedWindowFrame] display: YES animate: YES];
        [fScrollView setHasVerticalScroller: YES];
    }
}

- (NSRect) sizedWindowFrame
{
    float heightChange = (GROUP_SEPARATOR_HEIGHT + [fTableView intercellSpacing].height) * [fDisplayedGroupIndexes count]
                        + ([fTableView rowHeight] + [fTableView intercellSpacing].height) * ([fDisplayedTorrents count]
                            - [fDisplayedGroupIndexes count]) - [fScrollView frame].size.height;
    
    return [self windowFrameByAddingHeight: heightChange checkLimits: YES];
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

- (NSSize) windowWillResize: (NSWindow *) sender toSize: (NSSize) proposedFrameSize
{
    //only resize horizontally if autosize is enabled
    if ([fDefaults boolForKey: @"AutoSize"])
        proposedFrameSize.height = [fWindow frame].size.height;
    return proposedFrameSize;
}

- (void) windowDidResize: (NSNotification *) notification
{
    if ([fFilterBar isHidden])
        return;
    
    //replace all buttons
    [fActiveFilterButton sizeToFit];
    [fDownloadFilterButton sizeToFit];
    [fSeedFilterButton sizeToFit];
    [fPauseFilterButton sizeToFit];
    
    NSRect activeRect = [fActiveFilterButton frame];
    
    NSRect downloadRect = [fDownloadFilterButton frame];
    downloadRect.origin.x = NSMaxX(activeRect) + 1.0;
    
    NSRect seedRect = [fSeedFilterButton frame];
    seedRect.origin.x = NSMaxX(downloadRect) + 1.0;
    
    NSRect pauseRect = [fPauseFilterButton frame];
    pauseRect.origin.x = NSMaxX(seedRect) + 1.0;
    
    //size search filter to not overlap buttons
    NSRect searchFrame = [fSearchFilterField frame];
    searchFrame.origin.x = NSMaxX(pauseRect) + 5.0;
    searchFrame.size.width = [fStatusBar frame].size.width - searchFrame.origin.x - 5.0;
    
    //make sure it is not too long
    if (searchFrame.size.width > SEARCH_FILTER_MAX_WIDTH)
    {
        searchFrame.origin.x += searchFrame.size.width - SEARCH_FILTER_MAX_WIDTH;
        searchFrame.size.width = SEARCH_FILTER_MAX_WIDTH;
    }
    else if (searchFrame.size.width < SEARCH_FILTER_MIN_WIDTH)
    {
        searchFrame.origin.x += searchFrame.size.width - SEARCH_FILTER_MIN_WIDTH;
        searchFrame.size.width = SEARCH_FILTER_MIN_WIDTH;
        
        //resize each button until they don't overlap search
        int download = 0;
        BOOL seeding = NO, paused = NO;
        do
        {
            if (download < 8)
            {
                download++;
                downloadRect.size.width--;
                
                seedRect.origin.x--;
                pauseRect.origin.x--;
            }
            else if (!seeding)
            {
                seeding = YES;
                seedRect.size.width--;
                
                pauseRect.origin.x--;
            }
            else if (!paused)
            {
                paused = YES;
                pauseRect.size.width--;
            }
            else
            {
                activeRect.size.width--;
                
                downloadRect.origin.x--;
                seedRect.origin.x--;
                pauseRect.origin.x--;
                
                //reset
                download = 0;
                seeding = NO;
                paused = NO;
            }
        }
        while (NSMaxX(pauseRect) + 5.0 > searchFrame.origin.x);
    }
    else;
    
    [fActiveFilterButton setFrame: activeRect];
    [fDownloadFilterButton setFrame: downloadRect];
    [fSeedFilterButton setFrame: seedRect];
    [fPauseFilterButton setFrame: pauseRect];
    
    [fSearchFilterField setFrame: searchFrame];
}

- (void) applicationWillUnhide: (NSNotification *) notification
{
    [self updateUI];
}

- (void) linkHomepage: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: WEBSITE_URL]];
}

- (void) linkForums: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: FORUM_URL]];
}

- (void) linkDonate: (id) sender
{
    [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: DONATE_URL]];
}

- (void) prepareForUpdate: (NSNotification *) notification
{
    fUpdateInProgress = YES;
}

- (NSDictionary *) registrationDictionaryForGrowl
{
    NSArray * notifications = [NSArray arrayWithObjects: GROWL_DOWNLOAD_COMPLETE, GROWL_SEEDING_COMPLETE,
                                                            GROWL_AUTO_ADD, GROWL_AUTO_SPEED_LIMIT, nil];
    return [NSDictionary dictionaryWithObjectsAndKeys: notifications, GROWL_NOTIFICATIONS_ALL,
                                notifications, GROWL_NOTIFICATIONS_DEFAULT, nil];
}

- (void) growlNotificationWasClicked: (id) clickContext
{
    if (!clickContext || ![clickContext isKindOfClass: [NSDictionary class]])
        return;
    
    NSString * type = [clickContext objectForKey: @"Type"], * location;
    if (([type isEqualToString: GROWL_DOWNLOAD_COMPLETE] || [type isEqualToString: GROWL_SEEDING_COMPLETE])
            && (location = [clickContext objectForKey: @"Location"]))
        [[NSWorkspace sharedWorkspace] selectFile: location inFileViewerRootedAtPath: nil];
}

- (void) ipcQuit
{
    fRemoteQuit = YES;
    [NSApp terminate: self];
}

- (NSArray *) ipcGetTorrentsByID: (NSArray *) idlist
{
    if (!idlist)
        return fTorrents;
    
    NSMutableArray * torrents = [NSMutableArray array];
    
    NSEnumerator * torrentEnum = [fTorrents objectEnumerator], * idEnum;
    int torId;
    Torrent * torrent;
    NSNumber * tempId;
    while ((torrent = [torrentEnum nextObject]))
    {
        torId = [torrent torrentID];
        
        idEnum = [idlist objectEnumerator];
        while ((tempId = [idEnum nextObject]))
        {
            if ([tempId intValue] == torId)
            {
                [torrents addObject: torrent];
                break;
            }
        }
    }

    return torrents;
}

- (NSArray *) ipcGetTorrentsByHash: (NSArray *) hashlist
{
    if (!hashlist)
        return fTorrents;
    
    NSMutableArray * torrents = [NSMutableArray array];
    
    NSEnumerator * torrentEnum = [fTorrents objectEnumerator], * hashEnum;
    NSString * torHash, * tempHash;
    Torrent * torrent;
    while ((torrent = [torrentEnum nextObject]))
    {
        torHash = [torrent hashString];
        
        hashEnum = [hashlist objectEnumerator];
        while ((tempHash = [hashEnum nextObject]))
        {
            if ([torHash caseInsensitiveCompare: tempHash] == NSOrderedSame)
            {
                [torrents addObject: torrent];
                break;
            }
        }
    }
    
    return torrents;
}

- (BOOL) ipcAddTorrents: (NSArray *) torrents
{
    int oldCount = [fTorrents count];
    
    [self openFiles: torrents addType: ADD_NORMAL forcePath: nil];
    
    return [fTorrents count] > oldCount;
}

- (BOOL) ipcAddTorrentFile: (NSString *) path directory: (NSString *) directory
{
    int oldCount = [fTorrents count];
    
    [self openFiles: [NSArray arrayWithObject: path] addType: ADD_NORMAL forcePath: directory];
    
    return [fTorrents count] > oldCount;
}

- (BOOL) ipcAddTorrentFileAutostart: (NSString *) path directory: (NSString *) directory autostart: (BOOL) autostart
{
    NSArray * torrents = nil;
    if (autostart)
        torrents = [fTorrents copy];
    BOOL success = [self ipcAddTorrentFile: path directory: directory];
    
    if (success && autostart)
    {
        NSEnumerator * enumerator = [torrents reverseObjectEnumerator];
        Torrent * torrent;
        while ((torrent = [enumerator nextObject]))
            if (![torrents containsObject: torrent])
                break;
        
        if (torrent)
            [torrent startTransfer];
        else
            success = NO;
    }
    
    [torrents release];
    return success;
}

- (BOOL) ipcAddTorrentData: (NSData *) data directory: (NSString *) directory
{
    return [self ipcAddTorrentDataAutostart: data directory: directory autostart: [fDefaults boolForKey: @"AutoStartDownload"]];
}

- (BOOL) ipcAddTorrentDataAutostart: (NSData *) data directory: (NSString *) directory autostart: (BOOL) autostart
{
    Torrent * torrent;
    if ((torrent = [[Torrent alloc] initWithData: data location: directory lib: fLib]))
    {
        [torrent update];
        [fTorrents addObject: torrent];
        
        if (autostart)
            [torrent startTransfer];
        
        [torrent release];
        
        [self updateTorrentsInQueue];
        return YES;
    }
    else
        return NO;
}

- (BOOL) ipcStartTorrents: (NSArray *) torrents
{
    if (!torrents)
        [self resumeAllTorrents: self];
    else
        [self resumeTorrents: torrents];

    return YES;
}

- (BOOL) ipcStopTorrents: (NSArray *) torrents
{
    if (!torrents)
        [self stopAllTorrents: self];
    else
        [self stopTorrents: torrents];

    return YES;
}

- (BOOL) ipcRemoveTorrents: (NSArray *) torrents
{
    if (!torrents)
        torrents = [NSArray arrayWithArray: fTorrents];
    [torrents retain];

    [self confirmRemoveTorrents: torrents deleteData: NO deleteTorrent: NO];

    return YES;
}

@end
