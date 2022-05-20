// This file Copyright © 2006-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoWindowController.h"
#import "InfoViewController.h"
#import "InfoGeneralViewController.h"
#import "InfoActivityViewController.h"
#import "InfoTrackersViewController.h"
#import "InfoPeersViewController.h"
#import "InfoFileViewController.h"
#import "InfoOptionsViewController.h"
#import "NSImageAdditions.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

#define TAB_INFO_IDENT @"Info"
#define TAB_ACTIVITY_IDENT @"Activity"
#define TAB_TRACKER_IDENT @"Tracker"
#define TAB_PEERS_IDENT @"Peers"
#define TAB_FILES_IDENT @"Files"
#define TAB_OPTIONS_IDENT @"Options"

#define TAB_MIN_HEIGHT 250

#define INVALID -99

typedef NS_ENUM(unsigned int, tabTag) {
    TAB_GENERAL_TAG = 0,
    TAB_ACTIVITY_TAG = 1,
    TAB_TRACKERS_TAG = 2,
    TAB_PEERS_TAG = 3,
    TAB_FILE_TAG = 4,
    TAB_OPTIONS_TAG = 5
};

@interface InfoWindowController ()

@property(nonatomic, copy) NSArray<Torrent*>* fTorrents;

@property(nonatomic) CGFloat fMinWindowWidth;

@property(nonatomic) NSViewController<InfoViewController>* fViewController;
@property(nonatomic) NSInteger fCurrentTabTag;
@property(nonatomic) IBOutlet NSSegmentedControl* fTabs;

@property(nonatomic) InfoGeneralViewController* fGeneralViewController;
@property(nonatomic) InfoActivityViewController* fActivityViewController;
@property(nonatomic) InfoTrackersViewController* fTrackersViewController;
@property(nonatomic) InfoPeersViewController* fPeersViewController;
@property(nonatomic) InfoFileViewController* fFileViewController;
@property(nonatomic) InfoOptionsViewController* fOptionsViewController;

@property(nonatomic) IBOutlet NSImageView* fImageView;
@property(nonatomic) IBOutlet NSTextField* fNameField;
@property(nonatomic) IBOutlet NSTextField* fBasicInfoField;
@property(nonatomic) IBOutlet NSTextField* fNoneSelectedField;

- (void)resetInfo;
- (void)resetInfoForTorrent:(NSNotification*)notification;

@end

@implementation InfoWindowController

- (instancetype)init
{
    self = [super initWithWindowNibName:@"InfoWindow"];
    return self;
}

- (void)awakeFromNib
{
    self.fNoneSelectedField.stringValue = NSLocalizedString(@"No Torrents Selected", "Inspector -> selected torrents");

    //window location and size
    NSPanel* window = (NSPanel*)self.window;

    window.floatingPanel = NO;

    CGFloat const windowHeight = NSHeight(window.frame);
    self.fMinWindowWidth = window.minSize.width;

    [window setFrameAutosaveName:@"InspectorWindow"];
    [window setFrameUsingName:@"InspectorWindow"];

    NSRect windowRect = window.frame;
    windowRect.origin.y -= windowHeight - NSHeight(windowRect);
    windowRect.size.height = windowHeight;
    [window setFrame:windowRect display:NO];

    window.becomesKeyOnlyIfNeeded = YES;

    //set tab tooltips
    [self.fTabs.cell setToolTip:NSLocalizedString(@"General Info", "Inspector -> tab") forSegment:TAB_GENERAL_TAG];
    [self.fTabs.cell setToolTip:NSLocalizedString(@"Activity", "Inspector -> tab") forSegment:TAB_ACTIVITY_TAG];
    [self.fTabs.cell setToolTip:NSLocalizedString(@"Trackers", "Inspector -> tab") forSegment:TAB_TRACKERS_TAG];
    [self.fTabs.cell setToolTip:NSLocalizedString(@"Peers", "Inspector -> tab") forSegment:TAB_PEERS_TAG];
    [self.fTabs.cell setToolTip:NSLocalizedString(@"Files", "Inspector -> tab") forSegment:TAB_FILE_TAG];
    [self.fTabs.cell setToolTip:NSLocalizedString(@"Options", "Inspector -> tab") forSegment:TAB_OPTIONS_TAG];

    [self.fTabs setImage:[NSImage systemSymbol:@"info.circle" withFallback:@"InfoGeneral"] forSegment:TAB_GENERAL_TAG];
    [self.fTabs setImage:[NSImage systemSymbol:@"square.grid.3x3.fill.square" withFallback:@"InfoActivity"] forSegment:TAB_ACTIVITY_TAG];
    [self.fTabs setImage:[NSImage systemSymbol:@"antenna.radiowaves.left.and.right" withFallback:@"InfoTracker"]
              forSegment:TAB_TRACKERS_TAG];
    [self.fTabs setImage:[NSImage systemSymbol:@"person.2" withFallback:@"InfoPeers"] forSegment:TAB_PEERS_TAG];
    [self.fTabs setImage:[NSImage systemSymbol:@"doc.on.doc" withFallback:@"InfoFiles"] forSegment:TAB_FILE_TAG];
    [self.fTabs setImage:[NSImage systemSymbol:@"gearshape" withFallback:@"InfoOptions"] forSegment:TAB_OPTIONS_TAG];

    //set selected tab
    self.fCurrentTabTag = INVALID;
    NSString* identifier = [NSUserDefaults.standardUserDefaults stringForKey:@"InspectorSelected"];
    NSInteger tag;
    if ([identifier isEqualToString:TAB_INFO_IDENT])
    {
        tag = TAB_GENERAL_TAG;
    }
    else if ([identifier isEqualToString:TAB_ACTIVITY_IDENT])
    {
        tag = TAB_ACTIVITY_TAG;
    }
    else if ([identifier isEqualToString:TAB_TRACKER_IDENT])
    {
        tag = TAB_TRACKERS_TAG;
    }
    else if ([identifier isEqualToString:TAB_PEERS_IDENT])
    {
        tag = TAB_PEERS_TAG;
    }
    else if ([identifier isEqualToString:TAB_FILES_IDENT])
    {
        tag = TAB_FILE_TAG;
    }
    else if ([identifier isEqualToString:TAB_OPTIONS_IDENT])
    {
        tag = TAB_OPTIONS_TAG;
    }
    else //safety
    {
        [NSUserDefaults.standardUserDefaults setObject:TAB_INFO_IDENT forKey:@"InspectorSelected"];
        tag = TAB_GENERAL_TAG;
    }

    self.fTabs.target = self;
    self.fTabs.action = @selector(setTab:);

    self.fTabs.selectedSegment = tag;
    [self setTab:nil];

    //set blank inspector
    [self setInfoForTorrents:@[]];

    //allow for update notifications
    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
    [nc addObserver:self selector:@selector(resetInfoForTorrent:) name:@"ResetInspector" object:nil];
    [nc addObserver:self selector:@selector(updateInfoStats) name:@"UpdateStats" object:nil];
    [nc addObserver:self selector:@selector(updateOptions) name:@"UpdateOptions" object:nil];
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];

    if ([_fViewController respondsToSelector:@selector(saveViewSize)])
    {
        [_fViewController saveViewSize];
    }
}

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents
{
    if (self.fTorrents && [self.fTorrents isEqualToArray:torrents])
    {
        return;
    }

    self.fTorrents = torrents;

    [self resetInfo];
}

- (NSRect)windowWillUseStandardFrame:(NSWindow*)window defaultFrame:(NSRect)defaultFrame
{
    NSRect windowRect = window.frame;
    windowRect.size.width = window.minSize.width;
    return windowRect;
}

- (void)windowWillClose:(NSNotification*)notification
{
    if (self.fCurrentTabTag == TAB_FILE_TAG && ([QLPreviewPanel sharedPreviewPanelExists] && [QLPreviewPanel sharedPreviewPanel].visible))
    {
        [[QLPreviewPanel sharedPreviewPanel] reloadData];
    }
}

- (void)setTab:(id)sender
{
    NSInteger const oldTabTag = self.fCurrentTabTag;
    self.fCurrentTabTag = self.fTabs.selectedSegment;
    if (self.fCurrentTabTag == oldTabTag)
    {
        return;
    }

    //take care of old view
    CGFloat oldHeight = 0;
    if (oldTabTag != INVALID)
    {
        if ([self.fViewController respondsToSelector:@selector(saveViewSize)])
        {
            [self.fViewController saveViewSize];
        }

        if ([self.fViewController respondsToSelector:@selector(clearView)])
        {
            [self.fViewController clearView];
        }

        NSView* oldView = self.fViewController.view;
        oldHeight = NSHeight(oldView.frame);

        //remove old view
        [oldView removeFromSuperview];
    }

    //set new tab item
    NSString* identifier;
    switch (self.fCurrentTabTag)
    {
    case TAB_GENERAL_TAG:
        if (!self.fGeneralViewController)
        {
            self.fGeneralViewController = [[InfoGeneralViewController alloc] init];
            [self.fGeneralViewController setInfoForTorrents:self.fTorrents];
        }

        self.fViewController = self.fGeneralViewController;
        identifier = TAB_INFO_IDENT;
        break;
    case TAB_ACTIVITY_TAG:
        if (!self.fActivityViewController)
        {
            self.fActivityViewController = [[InfoActivityViewController alloc] init];
            [self.fActivityViewController setInfoForTorrents:self.fTorrents];
        }

        self.fViewController = self.fActivityViewController;
        identifier = TAB_ACTIVITY_IDENT;
        break;
    case TAB_TRACKERS_TAG:
        if (!self.fTrackersViewController)
        {
            self.fTrackersViewController = [[InfoTrackersViewController alloc] init];
            [self.fTrackersViewController setInfoForTorrents:self.fTorrents];
        }

        self.fViewController = self.fTrackersViewController;
        identifier = TAB_TRACKER_IDENT;
        break;
    case TAB_PEERS_TAG:
        if (!self.fPeersViewController)
        {
            self.fPeersViewController = [[InfoPeersViewController alloc] init];
            [self.fPeersViewController setInfoForTorrents:self.fTorrents];
        }

        self.fViewController = self.fPeersViewController;
        identifier = TAB_PEERS_IDENT;
        break;
    case TAB_FILE_TAG:
        if (!self.fFileViewController)
        {
            self.fFileViewController = [[InfoFileViewController alloc] init];
            [self.fFileViewController setInfoForTorrents:self.fTorrents];
        }

        self.fViewController = self.fFileViewController;
        identifier = TAB_FILES_IDENT;
        break;
    case TAB_OPTIONS_TAG:
        if (!self.fOptionsViewController)
        {
            self.fOptionsViewController = [[InfoOptionsViewController alloc] init];
            [self.fOptionsViewController setInfoForTorrents:self.fTorrents];
        }

        self.fViewController = self.fOptionsViewController;
        identifier = TAB_OPTIONS_IDENT;
        break;
    default:
        NSAssert1(NO, @"Unknown info tab selected: %ld", self.fCurrentTabTag);
        return;
    }

    [NSUserDefaults.standardUserDefaults setObject:identifier forKey:@"InspectorSelected"];

    NSWindow* window = self.window;

    window.title = [NSString
        stringWithFormat:@"%@ - %@", self.fViewController.title, NSLocalizedString(@"Torrent Inspector", "Inspector -> title")];

    NSView* view = self.fViewController.view;

    [self.fViewController updateInfo];

    NSRect windowRect = window.frame, viewRect = view.frame;

    CGFloat const difference = NSHeight(viewRect) - oldHeight;
    windowRect.origin.y -= difference;
    windowRect.size.height += difference;

    CGFloat const minWindowWidth = MAX(self.fMinWindowWidth, view.fittingSize.width);
    windowRect.size.width = MAX(NSWidth(windowRect), minWindowWidth);

    if ([self.fViewController respondsToSelector:@selector(saveViewSize)]) //a little bit hacky, but avoids requiring an extra method
    {
        if (window.screen)
        {
            CGFloat const screenHeight = NSHeight(window.screen.visibleFrame);
            if (NSHeight(windowRect) > screenHeight)
            {
                CGFloat const difference = screenHeight - NSHeight(windowRect);
                windowRect.origin.y -= difference;
                windowRect.size.height += difference;

                viewRect.size.height += difference;
            }
        }

        window.minSize = NSMakeSize(minWindowWidth, NSHeight(windowRect) - NSHeight(viewRect) + TAB_MIN_HEIGHT);
        window.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
    }
    else
    {
        window.minSize = NSMakeSize(minWindowWidth, NSHeight(windowRect));
        window.maxSize = NSMakeSize(FLT_MAX, NSHeight(windowRect));
    }

    viewRect.size.width = NSWidth(windowRect);
    view.frame = viewRect;

    [window setFrame:windowRect display:YES animate:oldTabTag != INVALID];
    [window.contentView addSubview:view];

    [window.contentView addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|-0-[view]-0-|" options:0 metrics:nil
                                                                                 views:@{ @"view" : view }]];
    [window.contentView
        addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:[tabs]-0-[view]-0-|" options:0 metrics:nil
                                                                 views:@{ @"tabs" : self.fTabs, @"view" : view }]];

    if ((self.fCurrentTabTag == TAB_FILE_TAG || oldTabTag == TAB_FILE_TAG) &&
        ([QLPreviewPanel sharedPreviewPanelExists] && [QLPreviewPanel sharedPreviewPanel].visible))
    {
        [[QLPreviewPanel sharedPreviewPanel] reloadData];
    }
}

- (void)setNextTab
{
    NSInteger tag = self.fTabs.selectedSegment + 1;
    if (tag >= self.fTabs.segmentCount)
    {
        tag = 0;
    }

    self.fTabs.selectedSegment = tag;
    [self setTab:nil];
}

- (void)setPreviousTab
{
    NSInteger tag = self.fTabs.selectedSegment - 1;
    if (tag < 0)
    {
        tag = self.fTabs.segmentCount - 1;
    }

    self.fTabs.selectedSegment = tag;
    [self setTab:nil];
}

- (void)swipeWithEvent:(NSEvent*)event
{
    if (event.deltaX < 0.0)
    {
        [self setNextTab];
    }
    else if (event.deltaX > 0.0)
    {
        [self setPreviousTab];
    }
}

- (void)updateInfoStats
{
    [self.fViewController updateInfo];
}

- (void)updateOptions
{
    [self.fOptionsViewController updateOptions];
}

- (NSArray<NSURL*>*)quickLookURLs
{
    return self.fFileViewController.quickLookURLs;
}

- (BOOL)canQuickLook
{
    if (self.fCurrentTabTag != TAB_FILE_TAG || !self.window.visible)
    {
        return NO;
    }

    return self.fFileViewController.canQuickLook;
}

- (NSRect)quickLookSourceFrameForPreviewItem:(id<QLPreviewItem>)item
{
    return [self.fFileViewController quickLookSourceFrameForPreviewItem:item];
}

#pragma mark - Private

- (void)resetInfo
{
    NSUInteger const numberSelected = self.fTorrents.count;
    if (numberSelected != 1)
    {
        if (numberSelected > 0)
        {
            self.fImageView.image = [NSImage imageNamed:NSImageNameMultipleDocuments];

            self.fNameField.stringValue = [NSString
                stringWithFormat:NSLocalizedString(@"%lu Torrents Selected", "Inspector -> selected torrents"), numberSelected];
            self.fNameField.hidden = NO;

            uint64_t size = 0;
            NSUInteger fileCount = 0, magnetCount = 0;
            for (Torrent* torrent in self.fTorrents)
            {
                size += torrent.size;
                fileCount += torrent.fileCount;
                if (torrent.magnet)
                {
                    ++magnetCount;
                }
            }

            NSMutableArray* fileStrings = [NSMutableArray arrayWithCapacity:2];
            if (fileCount > 0)
            {
                NSString* fileString;
                if (fileCount == 1)
                {
                    fileString = NSLocalizedString(@"1 file", "Inspector -> selected torrents");
                }
                else
                {
                    fileString = [NSString stringWithFormat:NSLocalizedString(@"%lu files", "Inspector -> selected torrents"), fileCount];
                }
                [fileStrings addObject:fileString];
            }
            if (magnetCount > 0)
            {
                NSString* magnetString;
                if (magnetCount == 1)
                {
                    magnetString = NSLocalizedString(@"1 magnetized transfer", "Inspector -> selected torrents");
                }
                else
                {
                    magnetString = [NSString
                        stringWithFormat:NSLocalizedString(@"%lu magnetized transfers", "Inspector -> selected torrents"), magnetCount];
                }
                [fileStrings addObject:magnetString];
            }

            NSString* fileString = [fileStrings componentsJoinedByString:@" + "];

            if (magnetCount < numberSelected)
            {
                self.fBasicInfoField.stringValue = [NSString
                    stringWithFormat:@"%@, %@",
                                     fileString,
                                     [NSString stringWithFormat:NSLocalizedString(@"%@ total", "Inspector -> selected torrents"),
                                                                [NSString stringForFileSize:size]]];

                NSByteCountFormatter* formatter = [[NSByteCountFormatter alloc] init];
                formatter.allowedUnits = NSByteCountFormatterUseBytes;
                self.fBasicInfoField.toolTip = [formatter stringFromByteCount:size];
            }
            else
            {
                self.fBasicInfoField.stringValue = fileString;
                self.fBasicInfoField.toolTip = nil;
            }
            self.fBasicInfoField.hidden = NO;

            self.fNoneSelectedField.hidden = YES;
        }
        else
        {
            self.fImageView.image = [NSImage imageNamed:NSImageNameApplicationIcon];
            self.fNoneSelectedField.hidden = NO;

            self.fNameField.hidden = YES;
            self.fBasicInfoField.hidden = YES;
        }

        self.fNameField.toolTip = nil;
    }
    else
    {
        Torrent* torrent = self.fTorrents[0];

        self.fImageView.image = torrent.icon;

        NSString* name = torrent.name;
        self.fNameField.stringValue = name;
        self.fNameField.toolTip = name;
        self.fNameField.hidden = NO;

        if (!torrent.magnet)
        {
            NSString* basicString = [NSString stringForFileSize:torrent.size];
            if (torrent.folder)
            {
                NSString* fileString;
                NSUInteger const fileCount = torrent.fileCount;
                if (fileCount == 1)
                {
                    fileString = NSLocalizedString(@"1 file", "Inspector -> selected torrents");
                }
                else
                {
                    fileString = [NSString stringWithFormat:NSLocalizedString(@"%lu files", "Inspector -> selected torrents"), fileCount];
                }
                basicString = [NSString stringWithFormat:@"%@, %@", fileString, basicString];
            }
            self.fBasicInfoField.stringValue = basicString;

            NSByteCountFormatter* formatter = [[NSByteCountFormatter alloc] init];
            formatter.allowedUnits = NSByteCountFormatterUseBytes;
            self.fBasicInfoField.toolTip = [formatter stringFromByteCount:torrent.size];
        }
        else
        {
            self.fBasicInfoField.stringValue = NSLocalizedString(@"Magnetized transfer", "Inspector -> selected torrents");
            self.fBasicInfoField.toolTip = nil;
        }
        self.fBasicInfoField.hidden = NO;

        self.fNoneSelectedField.hidden = YES;
    }

    [self.fGeneralViewController setInfoForTorrents:self.fTorrents];
    [self.fActivityViewController setInfoForTorrents:self.fTorrents];
    [self.fTrackersViewController setInfoForTorrents:self.fTorrents];
    [self.fPeersViewController setInfoForTorrents:self.fTorrents];
    [self.fFileViewController setInfoForTorrents:self.fTorrents];
    [self.fOptionsViewController setInfoForTorrents:self.fTorrents];

    [self.fViewController updateInfo];
}

- (void)resetInfoForTorrent:(NSNotification*)notification
{
    Torrent* torrent = notification.userInfo[@"Torrent"];
    if (self.fTorrents && (!torrent || [self.fTorrents containsObject:torrent]))
    {
        [self resetInfo];
    }
}

@end
