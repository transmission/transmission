// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "MainToolbarController.h"

#import "ButtonToolbarItem.h"
#import "GroupToolbarItem.h"
#import "ShareToolbarItem.h"
#import "Toolbar.h"
#import "Torrent.h"

typedef NSString* ToolbarItemIdentifier NS_TYPED_EXTENSIBLE_ENUM;

static ToolbarItemIdentifier const ToolbarItemIdentifierCreate = @"Toolbar Create";
static ToolbarItemIdentifier const ToolbarItemIdentifierOpenFile = @"Toolbar Open";
static ToolbarItemIdentifier const ToolbarItemIdentifierOpenWeb = @"Toolbar Open Web";
static ToolbarItemIdentifier const ToolbarItemIdentifierRemove = @"Toolbar Remove";
static ToolbarItemIdentifier const ToolbarItemIdentifierInfo = @"Toolbar Info";
static ToolbarItemIdentifier const ToolbarItemIdentifierPauseAll = @"Toolbar Pause All";
static ToolbarItemIdentifier const ToolbarItemIdentifierResumeAll = @"Toolbar Resume All";
static ToolbarItemIdentifier const ToolbarItemIdentifierPauseResumeAll = @"Toolbar Pause / Resume All";
static ToolbarItemIdentifier const ToolbarItemIdentifierPauseSelected = @"Toolbar Pause Selected";
static ToolbarItemIdentifier const ToolbarItemIdentifierResumeSelected = @"Toolbar Resume Selected";
static ToolbarItemIdentifier const ToolbarItemIdentifierPauseResumeSelected = @"Toolbar Pause / Resume Selected";
static ToolbarItemIdentifier const ToolbarItemIdentifierFilter = @"Toolbar Toggle Filter";
static ToolbarItemIdentifier const ToolbarItemIdentifierQuickLook = @"Toolbar QuickLook";
static ToolbarItemIdentifier const ToolbarItemIdentifierShare = @"Toolbar Share";

typedef NS_ENUM(NSUInteger, ToolbarGroupTag) { //
    ToolbarGroupTagPause = 0,
    ToolbarGroupTagResume = 1
};

@interface MainToolbarController ()

@property(nonatomic, weak) id<MainToolbarControllerDelegate> delegate;

@end

@implementation MainToolbarController

- (instancetype)initWithDelegate:(id<MainToolbarControllerDelegate>)delegate
{
    if ((self = [super init]))
    {
        _delegate = delegate;
    }

    return self;
}

- (NSToolbar*)createToolbar
{
    Toolbar* toolbar = [[Toolbar alloc] initWithIdentifier:@"TRMainToolbar"];
    toolbar.delegate = self;
    toolbar.allowsUserCustomization = YES;
    toolbar.autosavesConfiguration = YES;
    toolbar.displayMode = NSToolbarDisplayModeIconOnly;

    return toolbar;
}

- (ButtonToolbarItem*)standardToolbarButtonWithIdentifier:(NSString*)ident
{
    return [self toolbarButtonWithIdentifier:ident forToolbarButtonClass:[ButtonToolbarItem class]];
}

- (__kindof ButtonToolbarItem*)toolbarButtonWithIdentifier:(NSString*)ident forToolbarButtonClass:(Class)klass
{
    ButtonToolbarItem* item = [[klass alloc] initWithItemIdentifier:ident];

    NSButton* button = [[NSButton alloc] init];
    button.bezelStyle = NSBezelStyleTexturedRounded;
    button.stringValue = @"";

    item.view = button;
    item.target = self;

    return item;
}

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar itemForItemIdentifier:(NSString*)ident willBeInsertedIntoToolbar:(BOOL)flag
{
    if ([ident isEqualToString:ToolbarItemIdentifierCreate])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];

        item.label = NSLocalizedString(@"Create", "Create toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Create Torrent File", "Create toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Create torrent file", "Create toolbar item -> tooltip");
        item.image = [NSImage imageWithSystemSymbolName:@"doc.badge.plus" accessibilityDescription:nil];
        item.action = @selector(createFile:);
        item.autovalidates = NO;

        return item;
    }
    else if ([ident isEqualToString:ToolbarItemIdentifierOpenFile])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];

        item.label = NSLocalizedString(@"Open", "Open toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Open Torrent Files", "Open toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Open torrent files", "Open toolbar item -> tooltip");
        item.image = [NSImage imageWithSystemSymbolName:@"folder" accessibilityDescription:nil];
        item.action = @selector(openShowSheet:);
        item.autovalidates = NO;

        return item;
    }
    else if ([ident isEqualToString:ToolbarItemIdentifierOpenWeb])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];

        item.label = NSLocalizedString(@"Open Address", "Open address toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Open Torrent Address", "Open address toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Open torrent web address", "Open address toolbar item -> tooltip");
        item.image = [NSImage imageWithSystemSymbolName:@"globe" accessibilityDescription:nil];
        item.action = @selector(openURLShowSheet:);
        item.autovalidates = NO;

        return item;
    }
    else if ([ident isEqualToString:ToolbarItemIdentifierRemove])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];

        item.label = NSLocalizedString(@"Remove", "Remove toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Remove Selected", "Remove toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Remove selected transfers", "Remove toolbar item -> tooltip");
        item.image = [NSImage imageWithSystemSymbolName:@"nosign" accessibilityDescription:nil];
        item.action = @selector(removeNoDelete:);
        item.visibilityPriority = NSToolbarItemVisibilityPriorityHigh;

        return item;
    }
    else if ([ident isEqualToString:ToolbarItemIdentifierInfo])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];
        ((NSButtonCell*)((NSButton*)item.view).cell).showsStateBy = NSContentsCellMask; // blue when enabled

        item.label = NSLocalizedString(@"Inspector", "Inspector toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Toggle Inspector", "Inspector toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Toggle the torrent inspector", "Inspector toolbar item -> tooltip");
        item.image = [NSImage imageWithSystemSymbolName:@"info.circle" accessibilityDescription:nil];
        item.action = @selector(showInfo:);

        return item;
    }
    else if ([ident isEqualToString:ToolbarItemIdentifierPauseResumeAll])
    {
        GroupToolbarItem* groupItem = [[GroupToolbarItem alloc] initWithItemIdentifier:ident];

        NSToolbarItem* itemPause = [self standardToolbarButtonWithIdentifier:ToolbarItemIdentifierPauseAll];
        NSToolbarItem* itemResume = [self standardToolbarButtonWithIdentifier:ToolbarItemIdentifierResumeAll];

        NSSegmentedControl* segmentedControl = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
        segmentedControl.segmentStyle = NSSegmentStyleTexturedRounded;
        segmentedControl.trackingMode = NSSegmentSwitchTrackingMomentary;
        segmentedControl.segmentCount = 2;

        [segmentedControl setTag:ToolbarGroupTagPause forSegment:ToolbarGroupTagPause];
        [segmentedControl setImage:[NSImage imageWithSystemSymbolName:@"pause.circle.fill" accessibilityDescription:nil]
                        forSegment:ToolbarGroupTagPause];
        [segmentedControl setToolTip:NSLocalizedString(@"Pause all transfers", "All toolbar item -> tooltip")
                          forSegment:ToolbarGroupTagPause];

        [segmentedControl setTag:ToolbarGroupTagResume forSegment:ToolbarGroupTagResume];
        [segmentedControl setImage:[NSImage imageWithSystemSymbolName:@"arrow.clockwise.circle.fill" accessibilityDescription:nil]
                        forSegment:ToolbarGroupTagResume];
        [segmentedControl setToolTip:NSLocalizedString(@"Resume all transfers", "All toolbar item -> tooltip")
                          forSegment:ToolbarGroupTagResume];
        if ([toolbar isKindOfClass:Toolbar.class] && ((Toolbar*)toolbar).isRunningCustomizationPalette)
        {
            // On macOS 13.2, the palette autolayout will hang unless the segmentedControl width is longer than the groupItem paletteLabel (matters especially in Russian and French).
            [segmentedControl setWidth:64 forSegment:ToolbarGroupTagPause];
            [segmentedControl setWidth:64 forSegment:ToolbarGroupTagResume];
        }

        groupItem.label = NSLocalizedString(@"Apply All", "All toolbar item -> label");
        groupItem.paletteLabel = NSLocalizedString(@"Pause / Resume All", "All toolbar item -> palette label");
        groupItem.visibilityPriority = NSToolbarItemVisibilityPriorityHigh;
        groupItem.subitems = @[ itemPause, itemResume ];
        groupItem.view = segmentedControl;
        groupItem.target = self;
        groupItem.action = @selector(allToolbarClicked:);

        [groupItem createMenu:@[
            NSLocalizedString(@"Pause All", "All toolbar item -> label"),
            NSLocalizedString(@"Resume All", "All toolbar item -> label")
        ]];

        return groupItem;
    }
    else if ([ident isEqualToString:ToolbarItemIdentifierPauseResumeSelected])
    {
        GroupToolbarItem* groupItem = [[GroupToolbarItem alloc] initWithItemIdentifier:ident];

        NSToolbarItem* itemPause = [self standardToolbarButtonWithIdentifier:ToolbarItemIdentifierPauseSelected];
        NSToolbarItem* itemResume = [self standardToolbarButtonWithIdentifier:ToolbarItemIdentifierResumeSelected];

        NSSegmentedControl* segmentedControl = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
        segmentedControl.segmentStyle = NSSegmentStyleTexturedRounded;
        segmentedControl.trackingMode = NSSegmentSwitchTrackingMomentary;
        segmentedControl.segmentCount = 2;

        [segmentedControl setTag:ToolbarGroupTagPause forSegment:ToolbarGroupTagPause];
        [segmentedControl setImage:[NSImage imageWithSystemSymbolName:@"pause" accessibilityDescription:nil]
                        forSegment:ToolbarGroupTagPause];
        [segmentedControl setToolTip:NSLocalizedString(@"Pause selected transfers", "Selected toolbar item -> tooltip")
                          forSegment:ToolbarGroupTagPause];

        [segmentedControl setTag:ToolbarGroupTagResume forSegment:ToolbarGroupTagResume];
        [segmentedControl setImage:[NSImage imageWithSystemSymbolName:@"arrow.clockwise" accessibilityDescription:nil]
                        forSegment:ToolbarGroupTagResume];
        [segmentedControl setToolTip:NSLocalizedString(@"Resume selected transfers", "Selected toolbar item -> tooltip")
                          forSegment:ToolbarGroupTagResume];
        if ([toolbar isKindOfClass:Toolbar.class] && ((Toolbar*)toolbar).isRunningCustomizationPalette)
        {
            // On macOS 13.2, the palette autolayout will hang unless the segmentedControl width is longer than the groupItem paletteLabel (matters especially in Russian and French).
            [segmentedControl setWidth:64 forSegment:ToolbarGroupTagPause];
            [segmentedControl setWidth:64 forSegment:ToolbarGroupTagResume];
        }

        groupItem.label = NSLocalizedString(@"Apply Selected", "Selected toolbar item -> label");
        groupItem.paletteLabel = NSLocalizedString(@"Pause / Resume Selected", "Selected toolbar item -> palette label");
        groupItem.visibilityPriority = NSToolbarItemVisibilityPriorityHigh;
        groupItem.subitems = @[ itemPause, itemResume ];
        groupItem.view = segmentedControl;
        groupItem.target = self;
        groupItem.action = @selector(selectedToolbarClicked:);

        [groupItem createMenu:@[
            NSLocalizedString(@"Pause Selected", "Selected toolbar item -> label"),
            NSLocalizedString(@"Resume Selected", "Selected toolbar item -> label")
        ]];

        return groupItem;
    }
    else if ([ident isEqualToString:ToolbarItemIdentifierFilter])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];
        ((NSButtonCell*)((NSButton*)item.view).cell).showsStateBy = NSContentsCellMask; // blue when enabled

        item.label = NSLocalizedString(@"Filter", "Filter toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Toggle Filter", "Filter toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Toggle the filter bar", "Filter toolbar item -> tooltip");
        item.image = [NSImage imageWithSystemSymbolName:@"magnifyingglass" accessibilityDescription:nil];
        item.action = @selector(toggleFilterBar:);

        return item;
    }
    else if ([ident isEqualToString:ToolbarItemIdentifierQuickLook])
    {
        ButtonToolbarItem* item = [self standardToolbarButtonWithIdentifier:ident];
        ((NSButtonCell*)((NSButton*)item.view).cell).showsStateBy = NSContentsCellMask; // blue when enabled

        item.label = NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> label");
        item.paletteLabel = NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> palette label");
        item.toolTip = NSLocalizedString(@"Quick Look", "QuickLook toolbar item -> tooltip");
        item.image = [NSImage imageNamed:NSImageNameQuickLookTemplate];
        item.action = @selector(toggleQuickLook:);
        item.visibilityPriority = NSToolbarItemVisibilityPriorityLow;

        return item;
    }
    else if ([ident isEqualToString:ToolbarItemIdentifierShare])
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
    NSInteger tagValue = [sender isKindOfClass:[NSSegmentedControl class]] ? [(NSSegmentedControl*)sender selectedTag] :
                                                                             ((NSControl*)sender).tag;
    switch (tagValue)
    {
    case ToolbarGroupTagPause:
        [self.delegate mainToolbarStopAllTorrents:sender];
        break;
    case ToolbarGroupTagResume:
        [self.delegate mainToolbarResumeAllTorrents:sender];
        break;
    }
}

- (void)selectedToolbarClicked:(id)sender
{
    NSInteger tagValue = [sender isKindOfClass:[NSSegmentedControl class]] ? [(NSSegmentedControl*)sender selectedTag] :
                                                                             ((NSControl*)sender).tag;
    switch (tagValue)
    {
    case ToolbarGroupTagPause:
        [self.delegate mainToolbarStopSelectedTorrents:sender];
        break;
    case ToolbarGroupTagResume:
        [self.delegate mainToolbarResumeSelectedTorrents:sender];
        break;
    }
}

- (NSArray*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar
{
    return @[
        ToolbarItemIdentifierCreate,
        ToolbarItemIdentifierOpenFile,
        ToolbarItemIdentifierOpenWeb,
        ToolbarItemIdentifierRemove,
        ToolbarItemIdentifierPauseResumeSelected,
        ToolbarItemIdentifierPauseResumeAll,
        ToolbarItemIdentifierShare,
        ToolbarItemIdentifierQuickLook,
        ToolbarItemIdentifierFilter,
        ToolbarItemIdentifierInfo,
        NSToolbarSpaceItemIdentifier,
        NSToolbarFlexibleSpaceItemIdentifier
    ];
}

- (NSArray*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar
{
    return @[
        ToolbarItemIdentifierCreate,
        ToolbarItemIdentifierOpenFile,
        ToolbarItemIdentifierRemove,
        NSToolbarSpaceItemIdentifier,
        ToolbarItemIdentifierPauseResumeAll,
        NSToolbarFlexibleSpaceItemIdentifier,
        ToolbarItemIdentifierShare,
        ToolbarItemIdentifierQuickLook,
        ToolbarItemIdentifierFilter,
        ToolbarItemIdentifierInfo,
    ];
}

- (BOOL)validateToolbarItem:(NSToolbarItem*)toolbarItem
{
    NSString* ident = toolbarItem.itemIdentifier;

    // enable remove item
    if ([ident isEqualToString:ToolbarItemIdentifierRemove])
    {
        return [self.delegate mainToolbarSelectedTorrentCount] > 0;
    }

    // enable pause all item
    if ([ident isEqualToString:ToolbarItemIdentifierPauseAll])
    {
        for (Torrent* torrent in [self.delegate mainToolbarAllTorrents])
        {
            if (torrent.active || torrent.waitingToStart)
            {
                return YES;
            }
        }
        return NO;
    }

    // enable resume all item
    if ([ident isEqualToString:ToolbarItemIdentifierResumeAll])
    {
        for (Torrent* torrent in [self.delegate mainToolbarAllTorrents])
        {
            if (!torrent.active && !torrent.waitingToStart && !torrent.finishedSeeding)
            {
                return YES;
            }
        }
        return NO;
    }

    // enable pause item
    if ([ident isEqualToString:ToolbarItemIdentifierPauseSelected])
    {
        for (Torrent* torrent in [self.delegate mainToolbarSelectedTorrents])
        {
            if (torrent.active || torrent.waitingToStart)
            {
                return YES;
            }
        }
        return NO;
    }

    // enable resume item
    if ([ident isEqualToString:ToolbarItemIdentifierResumeSelected])
    {
        for (Torrent* torrent in [self.delegate mainToolbarSelectedTorrents])
        {
            if (!torrent.active && !torrent.waitingToStart)
            {
                return YES;
            }
        }
        return NO;
    }

    // set info item
    if ([ident isEqualToString:ToolbarItemIdentifierInfo])
    {
        ((NSButton*)toolbarItem.view).state = [self.delegate mainToolbarInfoVisible];
        return YES;
    }

    // set filter item
    if ([ident isEqualToString:ToolbarItemIdentifierFilter])
    {
        ((NSButton*)toolbarItem.view).state = [self.delegate mainToolbarFilterBarVisible] ? NSControlStateValueOn : NSControlStateValueOff;
        return YES;
    }

    // set quick look item
    if ([ident isEqualToString:ToolbarItemIdentifierQuickLook])
    {
        ((NSButton*)toolbarItem.view).state = [self.delegate mainToolbarQuickLookVisible];
        return [self.delegate mainToolbarSelectedTorrentCount] > 0;
    }

    // enable share item
    if ([ident isEqualToString:ToolbarItemIdentifierShare])
    {
        return [self.delegate mainToolbarSelectedTorrentCount] > 0;
    }

    return YES;
}

- (void)createFile:(id)sender
{
    [self.delegate mainToolbarCreateFile:sender];
}

- (void)openShowSheet:(id)sender
{
    [self.delegate mainToolbarOpenFile:sender];
}

- (void)openURLShowSheet:(id)sender
{
    [self.delegate mainToolbarOpenURL:sender];
}

- (void)removeNoDelete:(id)sender
{
    [self.delegate mainToolbarRemoveSelected:sender];
}

- (void)showInfo:(id)sender
{
    [self.delegate mainToolbarToggleInfo:sender];
}

- (void)toggleFilterBar:(id)sender
{
    [self.delegate mainToolbarToggleFilterBar:sender];
}

- (void)toggleQuickLook:(id)sender
{
    [self.delegate mainToolbarToggleQuickLook:sender];
}

- (void)showToolbarShare:(id)sender
{
    [self.delegate mainToolbarShowShare:sender];
}

@end
