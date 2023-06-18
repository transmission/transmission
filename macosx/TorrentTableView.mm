// This file Copyright © 2005-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "CocoaCompatibility.h"

#import "TorrentTableView.h"
#import "Controller.h"
#import "FileListNode.h"
#import "InfoOptionsViewController.h"
#import "NSKeyedUnarchiverAdditions.h"
#import "NSStringAdditions.h"
#import "Torrent.h"
#import "TorrentCell.h"
#import "TorrentGroup.h"
#import "GroupTextCell.h"

CGFloat const kGroupSeparatorHeight = 18.0;

static NSInteger const kMaxGroup = 999999;

//eliminate when Lion-only
typedef NS_ENUM(NSUInteger, ActionMenuTag) {
    ActionMenuTagGlobal = 101,
    ActionMenuTagUnlimited = 102,
    ActionMenuTagLimit = 103,
};

typedef NS_ENUM(NSUInteger, ActionMenuPriorityTag) {
    ActionMenuPriorityTagHigh = 101,
    ActionMenuPriorityTagNormal = 102,
    ActionMenuPriorityTagLow = 103,
};

static NSTimeInterval const kToggleProgressSeconds = 0.175;

@interface TorrentTableView ()

@property(nonatomic) IBOutlet Controller* fController;

@property(nonatomic) TorrentCell* fTorrentCell;
@property(nonatomic) GroupTextCell* fGroupTextCell;

@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic, readonly) NSMutableIndexSet* fCollapsedGroups;

@property(nonatomic) IBOutlet NSMenu* fContextRow;
@property(nonatomic) IBOutlet NSMenu* fContextNoRow;

@property(nonatomic) NSArray* fSelectedValues;

@property(nonatomic) IBOutlet NSMenu* fActionMenu;
@property(nonatomic) IBOutlet NSMenu* fUploadMenu;
@property(nonatomic) IBOutlet NSMenu* fDownloadMenu;
@property(nonatomic) IBOutlet NSMenu* fRatioMenu;
@property(nonatomic) IBOutlet NSMenu* fPriorityMenu;
@property(nonatomic) IBOutlet NSMenuItem* fGlobalLimitItem;
@property(nonatomic, readonly) Torrent* fMenuTorrent;

@property(nonatomic) CGFloat piecesBarPercent;
@property(nonatomic) NSAnimation* fPiecesBarAnimation;

@property(nonatomic) BOOL fActionPopoverShown;
@property(nonatomic) NSView* fPositioningView;

@end

@implementation TorrentTableView

- (instancetype)initWithCoder:(NSCoder*)decoder
{
    if ((self = [super initWithCoder:decoder]))
    {
        _fDefaults = NSUserDefaults.standardUserDefaults;

        _fTorrentCell = [[TorrentCell alloc] init];
        _fGroupTextCell = [[GroupTextCell alloc] init];

        NSData* groupData;
        if ((groupData = [_fDefaults dataForKey:@"CollapsedGroupIndexes"]))
        {
            _fCollapsedGroups = [NSKeyedUnarchiver unarchivedObjectOfClass:NSMutableIndexSet.class fromData:groupData error:nil];
        }
        else if ((groupData = [_fDefaults dataForKey:@"CollapsedGroups"])) //handle old groups
        {
            _fCollapsedGroups = [[NSKeyedUnarchiver deprecatedUnarchiveObjectWithData:groupData] mutableCopy];
            [_fDefaults removeObjectForKey:@"CollapsedGroups"];
            [self saveCollapsedGroups];
        }
        if (_fCollapsedGroups == nil)
        {
            _fCollapsedGroups = [[NSMutableIndexSet alloc] init];
        }

        _hoverRow = -1;
        _controlButtonHoverRow = -1;
        _revealButtonHoverRow = -1;
        _actionButtonHoverRow = -1;

        _fActionPopoverShown = NO;

        self.delegate = self;

        _piecesBarPercent = [_fDefaults boolForKey:@"PiecesBar"] ? 1.0 : 0.0;

        if (@available(macOS 11.0, *))
        {
            self.style = NSTableViewStyleFullWidth;
        }
    }

    return self;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)awakeFromNib
{
    //set group columns to show ratio, needs to be in awakeFromNib to size columns correctly
    [self setGroupStatusColumns];

    //disable highlight color and set manually in drawRow
    [self setSelectionHighlightStyle:NSTableViewSelectionHighlightStyleNone];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(refreshTorrentTable) name:@"RefreshTorrentTable"
                                             object:nil];
}

- (void)refreshTorrentTable
{
    self.needsDisplay = YES;
}

- (BOOL)isGroupCollapsed:(NSInteger)value
{
    if (value == -1)
    {
        value = kMaxGroup;
    }

    return [self.fCollapsedGroups containsIndex:value];
}

- (void)removeCollapsedGroup:(NSInteger)value
{
    if (value == -1)
    {
        value = kMaxGroup;
    }

    [self.fCollapsedGroups removeIndex:value];
}

- (void)removeAllCollapsedGroups
{
    [self.fCollapsedGroups removeAllIndexes];
}

- (void)saveCollapsedGroups
{
    [self.fDefaults setObject:[NSKeyedArchiver archivedDataWithRootObject:self.fCollapsedGroups requiringSecureCoding:YES error:nil]
                       forKey:@"CollapsedGroupIndexes"];
}

- (BOOL)outlineView:(NSOutlineView*)outlineView isGroupItem:(id)item
{
    //return no and style the groupItem cell manually in willDisplayCell
    //otherwise we get unwanted padding before each group header
    return NO;
}

- (CGFloat)outlineView:(NSOutlineView*)outlineView heightOfRowByItem:(id)item
{
    return [item isKindOfClass:[Torrent class]] ? self.rowHeight : kGroupSeparatorHeight;
}

- (NSCell*)outlineView:(NSOutlineView*)outlineView dataCellForTableColumn:(NSTableColumn*)tableColumn item:(id)item
{
    BOOL const group = ![item isKindOfClass:[Torrent class]];
    if (!tableColumn)
    {
        return !group ? self.fTorrentCell : nil;
    }
    else
    {
        NSString* ident = tableColumn.identifier;
        NSArray* imageColumns = @[ @"Color", @"DL Image", @"UL Image" ];
        if (![imageColumns containsObject:ident])
        {
            return group ? self.fGroupTextCell : nil;
        }
        else
        {
            return group ? [tableColumn dataCellForRow:[self rowForItem:item]] : nil;
        }
    }
}

- (void)outlineView:(NSOutlineView*)outlineView
    willDisplayCell:(id)cell
     forTableColumn:(NSTableColumn*)tableColumn
               item:(id)item
{
    if ([item isKindOfClass:[Torrent class]])
    {
        if (!tableColumn)
        {
            TorrentCell* torrentCell = cell;
            torrentCell.representedObject = item;

            NSInteger const row = [self rowForItem:item];
            torrentCell.hover = (row == self.hoverRow);
            torrentCell.hoverControl = (row == self.controlButtonHoverRow);
            torrentCell.hoverReveal = (row == self.revealButtonHoverRow);
            torrentCell.hoverAction = (row == self.actionButtonHoverRow);

            // if cell is selected, set backgroundStyle
            // then can provide alternate font color in TorrentCell - drawInteriorWithFrame
            NSIndexSet* selectedRowIndexes = self.selectedRowIndexes;
            if ([selectedRowIndexes containsIndex:row])
            {
                torrentCell.backgroundStyle = NSBackgroundStyleEmphasized;
            }
        }
    }
    else
    {
        if ([cell isKindOfClass:[GroupTextCell class]])
        {
            GroupTextCell* groupCell = cell;

            // if cell is selected, set selected flag so we can style the title in GroupTextCell drawInteriorWithFrame
            NSInteger const row = [self rowForItem:item];
            NSIndexSet* selectedRowIndexes = self.selectedRowIndexes;
            groupCell.selected = [selectedRowIndexes containsIndex:row];
        }
    }
}

//we override row highlighting because we are custom drawing the group rows
//see isGroupItem
- (void)drawRow:(NSInteger)row clipRect:(NSRect)clipRect
{
    NSColor* highlightColor = nil;

    id item = [self itemAtRow:row];

    //we only highlight torrent cells
    if ([item isKindOfClass:[Torrent class]])
    {
        //use system highlight color when Transmission is active
        if (self == [self.window firstResponder] && [self.window isMainWindow] && [self.window isKeyWindow])
        {
            highlightColor = [NSColor alternateSelectedControlColor];
        }
        else
        {
            highlightColor = [NSColor disabledControlTextColor];
        }

        NSIndexSet* selectedRowIndexes = [self selectedRowIndexes];
        if ([selectedRowIndexes containsIndex:row])
        {
            [highlightColor setFill];
            NSRectFill([self rectOfRow:row]);
        }
    }

    [super drawRow:row clipRect:clipRect];
}

- (NSRect)frameOfCellAtColumn:(NSInteger)column row:(NSInteger)row
{
    if (column == -1)
    {
        return [self rectOfRow:row];
    }
    else
    {
        NSRect rect = [super frameOfCellAtColumn:column row:row];

        //adjust placement for proper vertical alignment
        if (column == [self columnWithIdentifier:@"Group"])
        {
            rect.size.height -= 1.0f;
        }

        return rect;
    }
}

- (NSString*)outlineView:(NSOutlineView*)outlineView typeSelectStringForTableColumn:(NSTableColumn*)tableColumn item:(id)item
{
    if ([item isKindOfClass:[Torrent class]])
    {
        return ((Torrent*)item).name;
    }
    else
    {
        return [self.dataSource outlineView:outlineView objectValueForTableColumn:[self tableColumnWithIdentifier:@"Group"]
                                     byItem:item];
    }
}

- (NSString*)outlineView:(NSOutlineView*)outlineView
          toolTipForCell:(NSCell*)cell
                    rect:(NSRectPointer)rect
             tableColumn:(NSTableColumn*)column
                    item:(id)item
           mouseLocation:(NSPoint)mouseLocation
{
    NSString* ident = column.identifier;
    if ([ident isEqualToString:@"DL"] || [ident isEqualToString:@"DL Image"])
    {
        return NSLocalizedString(@"Download speed", "Torrent table -> group row -> tooltip");
    }
    else if ([ident isEqualToString:@"UL"] || [ident isEqualToString:@"UL Image"])
    {
        return [self.fDefaults boolForKey:@"DisplayGroupRowRatio"] ?
            NSLocalizedString(@"Ratio", "Torrent table -> group row -> tooltip") :
            NSLocalizedString(@"Upload speed", "Torrent table -> group row -> tooltip");
    }
    else if (ident)
    {
        NSUInteger count = ((TorrentGroup*)item).torrents.count;
        if (count == 1)
        {
            return NSLocalizedString(@"1 transfer", "Torrent table -> group row -> tooltip");
        }
        else
        {
            return [NSString localizedStringWithFormat:NSLocalizedString(@"%lu transfers", "Torrent table -> group row -> tooltip"), count];
        }
    }
    else
    {
        return nil;
    }
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    [self removeTrackingAreas];

    NSRange const rows = [self rowsInRect:self.visibleRect];
    if (rows.length == 0)
    {
        return;
    }

    NSPoint mouseLocation = [self convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil];
    for (NSUInteger row = rows.location; row < NSMaxRange(rows); row++)
    {
        if (![[self itemAtRow:row] isKindOfClass:[Torrent class]])
        {
            continue;
        }

        NSDictionary* userInfo = @{ @"Row" : @(row) };
        TorrentCell* cell = (TorrentCell*)[self preparedCellAtColumn:-1 row:row];
        [cell addTrackingAreasForView:self inRect:[self rectOfRow:row] withUserInfo:userInfo mouseLocation:mouseLocation];
    }
}

- (void)removeTrackingAreas
{
    _hoverRow = -1;
    _controlButtonHoverRow = -1;
    _revealButtonHoverRow = -1;
    _actionButtonHoverRow = -1;

    for (NSTrackingArea* area in self.trackingAreas)
    {
        if (area.owner == self && area.userInfo[@"Row"])
        {
            [self removeTrackingArea:area];
        }
    }
}

- (void)setHoverRow:(NSInteger)row
{
    NSAssert([self.fDefaults boolForKey:@"SmallView"], @"cannot set a hover row when not in compact view");

    _hoverRow = row;
    if (row >= 0)
    {
        [self setNeedsDisplayInRect:[self rectOfRow:row]];
    }
}

- (void)setControlButtonHoverRow:(NSInteger)row
{
    _controlButtonHoverRow = row;
    if (row >= 0)
    {
        [self setNeedsDisplayInRect:[self rectOfRow:row]];
    }
}

- (void)setRevealButtonHoverRow:(NSInteger)row
{
    _revealButtonHoverRow = row;
    if (row >= 0)
    {
        [self setNeedsDisplayInRect:[self rectOfRow:row]];
    }
}

- (void)setActionButtonHoverRow:(NSInteger)row
{
    _actionButtonHoverRow = row;
    if (row >= 0)
    {
        [self setNeedsDisplayInRect:[self rectOfRow:row]];
    }
}

- (void)mouseEntered:(NSEvent*)event
{
    NSDictionary* dict = (NSDictionary*)event.userData;

    NSNumber* row;
    if ((row = dict[@"Row"]))
    {
        NSInteger rowVal = row.integerValue;
        NSString* type = dict[@"Type"];
        if ([type isEqualToString:@"Action"])
        {
            _actionButtonHoverRow = rowVal;
        }
        else if ([type isEqualToString:@"Control"])
        {
            _controlButtonHoverRow = rowVal;
        }
        else if ([type isEqualToString:@"Reveal"])
        {
            _revealButtonHoverRow = rowVal;
        }
        else
        {
            _hoverRow = rowVal;
            if (![self.fDefaults boolForKey:@"SmallView"])
            {
                return;
            }
        }

        [self setNeedsDisplayInRect:[self rectOfRow:rowVal]];
    }
}

- (void)mouseExited:(NSEvent*)event
{
    NSDictionary* dict = (NSDictionary*)event.userData;

    NSNumber* row;
    if ((row = dict[@"Row"]))
    {
        NSString* type = dict[@"Type"];
        if ([type isEqualToString:@"Action"])
        {
            _actionButtonHoverRow = -1;
        }
        else if ([type isEqualToString:@"Control"])
        {
            _controlButtonHoverRow = -1;
        }
        else if ([type isEqualToString:@"Reveal"])
        {
            _revealButtonHoverRow = -1;
        }
        else
        {
            _hoverRow = -1;
            if (![self.fDefaults boolForKey:@"SmallView"])
            {
                return;
            }
        }

        [self setNeedsDisplayInRect:[self rectOfRow:row.integerValue]];
    }
}

- (void)outlineViewSelectionIsChanging:(NSNotification*)notification
{
#warning eliminate when view-based?
    //if pushing a button, don't change the selected rows
    if (self.fSelectedValues)
    {
        [self selectValues:self.fSelectedValues];
    }
}

- (void)outlineViewItemDidExpand:(NSNotification*)notification
{
    TorrentGroup* group = notification.userInfo[@"NSObject"];
    NSInteger value = group.groupIndex;
    if (value < 0)
    {
        value = kMaxGroup;
    }

    if ([self.fCollapsedGroups containsIndex:value])
    {
        [self.fCollapsedGroups removeIndex:value];
        [NSNotificationCenter.defaultCenter postNotificationName:@"OutlineExpandCollapse" object:self];
    }
}

- (void)outlineViewItemDidCollapse:(NSNotification*)notification
{
    TorrentGroup* group = notification.userInfo[@"NSObject"];
    NSInteger value = group.groupIndex;
    if (value < 0)
    {
        value = kMaxGroup;
    }

    [self.fCollapsedGroups addIndex:value];
    [NSNotificationCenter.defaultCenter postNotificationName:@"OutlineExpandCollapse" object:self];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    NSInteger const row = [self rowAtPoint:point];

    //check to toggle group status before anything else
    if ([self pointInGroupStatusRect:point])
    {
        [self.fDefaults setBool:![self.fDefaults boolForKey:@"DisplayGroupRowRatio"] forKey:@"DisplayGroupRowRatio"];
        [self setGroupStatusColumns];

        return;
    }

    BOOL const pushed = row != -1 &&
        (self.actionButtonHoverRow == row || self.revealButtonHoverRow == row || self.controlButtonHoverRow == row);

    //if pushing a button, don't change the selected rows
    if (pushed)
    {
        self.fSelectedValues = self.selectedValues;
    }

    [super mouseDown:event];

    self.fSelectedValues = nil;

    //avoid weird behavior when showing menu by doing this after mouse down
    if (row != -1 && self.actionButtonHoverRow == row)
    {
#warning maybe make appear on mouse down
        [self displayTorrentActionPopoverForEvent:event];
    }
    else if (!pushed && event.clickCount == 2) //double click
    {
        id item = nil;
        if (row != -1)
        {
            item = [self itemAtRow:row];
        }

        if (!item || [item isKindOfClass:[Torrent class]])
        {
            [self.fController showInfo:nil];
        }
        else
        {
            if ([self isItemExpanded:item])
            {
                [self collapseItem:item];
            }
            else
            {
                [self expandItem:item];
            }
        }
    }
}

- (void)selectValues:(NSArray*)values
{
    NSMutableIndexSet* indexSet = [NSMutableIndexSet indexSet];

    for (id item in values)
    {
        if ([item isKindOfClass:[Torrent class]])
        {
            NSInteger const index = [self rowForItem:item];
            if (index != -1)
            {
                [indexSet addIndex:index];
            }
        }
        else
        {
            TorrentGroup* group = (TorrentGroup*)item;
            NSInteger const groupIndex = group.groupIndex;
            for (NSInteger i = 0; i < self.numberOfRows; i++)
            {
                id tableItem = [self itemAtRow:i];
                if ([tableItem isKindOfClass:[TorrentGroup class]] && groupIndex == ((TorrentGroup*)tableItem).groupIndex)
                {
                    [indexSet addIndex:i];
                    break;
                }
            }
        }
    }

    [self selectRowIndexes:indexSet byExtendingSelection:NO];
}

- (NSArray*)selectedValues
{
    NSIndexSet* selectedIndexes = self.selectedRowIndexes;
    NSMutableArray* values = [NSMutableArray arrayWithCapacity:selectedIndexes.count];

    for (NSUInteger i = selectedIndexes.firstIndex; i != NSNotFound; i = [selectedIndexes indexGreaterThanIndex:i])
    {
        [values addObject:[self itemAtRow:i]];
    }

    return values;
}

- (NSArray<Torrent*>*)selectedTorrents
{
    NSIndexSet* selectedIndexes = self.selectedRowIndexes;
    NSMutableArray* torrents = [NSMutableArray arrayWithCapacity:selectedIndexes.count]; //take a shot at guessing capacity

    for (NSUInteger i = selectedIndexes.firstIndex; i != NSNotFound; i = [selectedIndexes indexGreaterThanIndex:i])
    {
        id item = [self itemAtRow:i];
        if ([item isKindOfClass:[Torrent class]])
        {
            [torrents addObject:item];
        }
        else
        {
            NSArray* groupTorrents = ((TorrentGroup*)item).torrents;
            [torrents addObjectsFromArray:groupTorrents];
            if ([self isItemExpanded:item])
            {
                i += groupTorrents.count;
            }
        }
    }

    return torrents;
}

- (NSMenu*)menuForEvent:(NSEvent*)event
{
    NSInteger row = [self rowAtPoint:[self convertPoint:event.locationInWindow fromView:nil]];
    if (row >= 0)
    {
        if (![self isRowSelected:row])
        {
            [self selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        }
        return self.fContextRow;
    }
    else
    {
        [self deselectAll:self];
        return self.fContextNoRow;
    }
}

//make sure that the pause buttons become orange when holding down the option key
- (void)flagsChanged:(NSEvent*)event
{
    [self display];
    [super flagsChanged:event];
}

//option-command-f will focus the filter bar's search field
- (void)keyDown:(NSEvent*)event
{
    unichar const firstChar = [event.charactersIgnoringModifiers characterAtIndex:0];

    if (firstChar == 'f' && event.modifierFlags & NSEventModifierFlagOption && event.modifierFlags & NSEventModifierFlagCommand)
    {
        [self.fController focusFilterField];
    }
    else if (firstChar == ' ')
    {
        [self.fController toggleQuickLook:nil];
    }
    else if (event.keyCode == 53) //esc key
    {
        [self deselectAll:nil];
    }
    else
    {
        [super keyDown:event];
    }
}

- (NSRect)iconRectForRow:(NSInteger)row
{
    return [self.fTorrentCell iconRectForBounds:[self rectOfRow:row]];
}

- (BOOL)acceptsFirstResponder
{
    // add support to `copy:`
    return YES;
}

- (void)copy:(id)sender
{
    NSArray<Torrent*>* selectedTorrents = self.selectedTorrents;
    if (selectedTorrents.count == 0)
    {
        return;
    }
    NSPasteboard* pasteBoard = NSPasteboard.generalPasteboard;
    NSString* links = [[selectedTorrents valueForKeyPath:@"magnetLink"] componentsJoinedByString:@"\n"];
    [pasteBoard declareTypes:@[ NSStringPboardType ] owner:nil];
    [pasteBoard setString:links forType:NSStringPboardType];
}

- (void)paste:(id)sender
{
    NSURL* url;
    if ((url = [NSURL URLFromPasteboard:NSPasteboard.generalPasteboard]))
    {
        [self.fController openURL:url.absoluteString];
    }
    else
    {
        NSArray<NSString*>* items = [NSPasteboard.generalPasteboard readObjectsForClasses:@[ [NSString class] ] options:nil];
        if (!items)
        {
            return;
        }
        NSDataDetector* detector = [NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeLink error:nil];
        for (NSString* itemString in items)
        {
            NSArray<NSString*>* itemLines = [itemString componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet];
            for (__strong NSString* pbItem in itemLines)
            {
                pbItem = [pbItem stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
                if ([pbItem rangeOfString:@"magnet:" options:(NSAnchoredSearch | NSCaseInsensitiveSearch)].location != NSNotFound)
                {
                    [self.fController openURL:pbItem];
                }
                else
                {
#warning only accept full text?
                    for (NSTextCheckingResult* result in [detector matchesInString:pbItem options:0
                                                                             range:NSMakeRange(0, pbItem.length)])
                        [self.fController openURL:result.URL.absoluteString];
                }
            }
        }
    }
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL action = menuItem.action;

    if (action == @selector(paste:))
    {
        if ([NSPasteboard.generalPasteboard.types containsObject:NSURLPboardType])
        {
            return YES;
        }

        NSArray* items = [NSPasteboard.generalPasteboard readObjectsForClasses:@[ [NSString class] ] options:nil];
        if (items)
        {
            NSDataDetector* detector = [NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeLink error:nil];
            for (__strong NSString* pbItem in items)
            {
                pbItem = [pbItem stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
                if (([pbItem rangeOfString:@"magnet:" options:(NSAnchoredSearch | NSCaseInsensitiveSearch)].location != NSNotFound) ||
                    [detector firstMatchInString:pbItem options:0 range:NSMakeRange(0, pbItem.length)])
                {
                    return YES;
                }
            }
        }

        return NO;
    }

    return YES;
}

- (void)toggleControlForTorrent:(Torrent*)torrent
{
    if (torrent.active)
    {
        [self.fController stopTorrents:@[ torrent ]];
    }
    else
    {
        if (NSEvent.modifierFlags & NSEventModifierFlagOption)
        {
            [self.fController resumeTorrentsNoWait:@[ torrent ]];
        }
        else if (torrent.waitingToStart)
        {
            [self.fController stopTorrents:@[ torrent ]];
        }
        else
        {
            [self.fController resumeTorrents:@[ torrent ]];
        }
    }
}

- (void)displayTorrentActionPopoverForEvent:(NSEvent*)event
{
    NSInteger const row = [self rowAtPoint:[self convertPoint:event.locationInWindow fromView:nil]];
    if (row < 0)
    {
        return;
    }

    NSRect const rect = [self.fTorrentCell actionRectForBounds:[self rectOfRow:row]];

    if (self.fActionPopoverShown)
    {
        return;
    }

    Torrent* torrent = [self itemAtRow:row];

    NSPopover* popover = [[NSPopover alloc] init];
    popover.behavior = NSPopoverBehaviorTransient;
    InfoOptionsViewController* infoViewController = [[InfoOptionsViewController alloc] init];
    popover.contentViewController = infoViewController;
    popover.delegate = self;

    [popover showRelativeToRect:rect ofView:self preferredEdge:NSMaxYEdge];
    [infoViewController setInfoForTorrents:@[ torrent ]];
    [infoViewController updateInfo];

    CGFloat width = NSWidth(rect);

    if (NSMinX(self.window.frame) < width || NSMaxX(self.window.screen.frame) - NSMinX(self.window.frame) < 72)
    {
        // Ugly hack to hide NSPopover arrow.
        self.fPositioningView = [[NSView alloc] initWithFrame:rect];
        self.fPositioningView.identifier = @"positioningView";
        [self addSubview:self.fPositioningView];
        [popover showRelativeToRect:self.fPositioningView.bounds ofView:self.fPositioningView preferredEdge:NSMaxYEdge];
        self.fPositioningView.bounds = NSOffsetRect(self.fPositioningView.bounds, 0, NSHeight(self.fPositioningView.bounds));
    }
    else
    {
        [popover showRelativeToRect:rect ofView:self preferredEdge:NSMaxYEdge];
    }
}

//don't show multiple popovers when clicking the gear button repeatedly
- (void)popoverWillShow:(NSNotification*)notification
{
    self.fActionPopoverShown = YES;
}

- (void)popoverDidClose:(NSNotification*)notification
{
    [self.fPositioningView removeFromSuperview];
    self.fPositioningView = nil;
    self.fActionPopoverShown = NO;
}

//eliminate when Lion-only, along with all the menu item instance variables
- (void)menuNeedsUpdate:(NSMenu*)menu
{
    //this method seems to be called when it shouldn't be
    if (!self.fMenuTorrent || !menu.supermenu)
    {
        return;
    }

    if (menu == self.fUploadMenu || menu == self.fDownloadMenu)
    {
        NSMenuItem* item;
        if (menu.numberOfItems == 3)
        {
            static NSArray<NSNumber*>* const speedLimitActionValues = @[ @50, @100, @250, @500, @1000, @2500, @5000, @10000 ];

            for (NSNumber* i in speedLimitActionValues)
            {
                item = [[NSMenuItem alloc]
                    initWithTitle:[NSString localizedStringWithFormat:NSLocalizedString(@"%ld KB/s", "Action menu -> upload/download limit"),
                                                                      i.integerValue]
                           action:@selector(setQuickLimit:)
                    keyEquivalent:@""];
                item.target = self;
                item.representedObject = i;
                [menu addItem:item];
            }
        }

        BOOL const upload = menu == self.fUploadMenu;
        BOOL const limit = [self.fMenuTorrent usesSpeedLimit:upload];

        item = [menu itemWithTag:ActionMenuTagLimit];
        item.state = limit ? NSControlStateValueOn : NSControlStateValueOff;
        item.title = [NSString localizedStringWithFormat:NSLocalizedString(@"Limit (%ld KB/s)", "torrent action menu -> upload/download limit"),
                                                         [self.fMenuTorrent speedLimit:upload]];

        item = [menu itemWithTag:ActionMenuTagUnlimited];
        item.state = !limit ? NSControlStateValueOn : NSControlStateValueOff;
    }
    else if (menu == self.fRatioMenu)
    {
        NSMenuItem* item;
        if (menu.numberOfItems == 4)
        {
            static NSArray<NSNumber*>* const ratioLimitActionValue = @[ @0.25, @0.5, @0.75, @1.0, @1.5, @2.0, @3.0 ];

            for (NSNumber* i in ratioLimitActionValue)
            {
                item = [[NSMenuItem alloc] initWithTitle:[NSString localizedStringWithFormat:@"%.2f", i.floatValue]
                                                  action:@selector(setQuickRatio:)
                                           keyEquivalent:@""];
                item.target = self;
                item.representedObject = i;
                [menu addItem:item];
            }
        }

        tr_ratiolimit const mode = self.fMenuTorrent.ratioSetting;

        item = [menu itemWithTag:ActionMenuTagLimit];
        item.state = mode == TR_RATIOLIMIT_SINGLE ? NSControlStateValueOn : NSControlStateValueOff;
        item.title = [NSString localizedStringWithFormat:NSLocalizedString(@"Stop at Ratio (%.2f)", "torrent action menu -> ratio stop"),
                                                         self.fMenuTorrent.ratioLimit];

        item = [menu itemWithTag:ActionMenuTagUnlimited];
        item.state = mode == TR_RATIOLIMIT_UNLIMITED ? NSControlStateValueOn : NSControlStateValueOff;

        item = [menu itemWithTag:ActionMenuTagGlobal];
        item.state = mode == TR_RATIOLIMIT_GLOBAL ? NSControlStateValueOn : NSControlStateValueOff;
    }
    else if (menu == self.fPriorityMenu)
    {
        tr_priority_t const priority = self.fMenuTorrent.priority;

        NSMenuItem* item = [menu itemWithTag:ActionMenuPriorityTagHigh];
        item.state = priority == TR_PRI_HIGH ? NSControlStateValueOn : NSControlStateValueOff;

        item = [menu itemWithTag:ActionMenuPriorityTagNormal];
        item.state = priority == TR_PRI_NORMAL ? NSControlStateValueOn : NSControlStateValueOff;

        item = [menu itemWithTag:ActionMenuPriorityTagLow];
        item.state = priority == TR_PRI_LOW ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

//the following methods might not be needed when Lion-only
- (void)setQuickLimitMode:(id)sender
{
    BOOL const limit = [sender tag] == ActionMenuTagLimit;
    [self.fMenuTorrent setUseSpeedLimit:limit upload:[sender menu] == self.fUploadMenu];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptions" object:nil];
}

- (void)setQuickLimit:(id)sender
{
    BOOL const upload = [sender menu] == self.fUploadMenu;
    [self.fMenuTorrent setUseSpeedLimit:YES upload:upload];
    [self.fMenuTorrent setSpeedLimit:[[sender representedObject] intValue] upload:upload];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptions" object:nil];
}

- (void)setGlobalLimit:(id)sender
{
    self.fMenuTorrent.usesGlobalSpeedLimit = ((NSButton*)sender).state != NSControlStateValueOn;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptions" object:nil];
}

- (void)setQuickRatioMode:(id)sender
{
    tr_ratiolimit mode;
    switch ([sender tag])
    {
    case ActionMenuTagUnlimited:
        mode = TR_RATIOLIMIT_UNLIMITED;
        break;
    case ActionMenuTagLimit:
        mode = TR_RATIOLIMIT_SINGLE;
        break;
    case ActionMenuTagGlobal:
        mode = TR_RATIOLIMIT_GLOBAL;
        break;
    default:
        return;
    }

    self.fMenuTorrent.ratioSetting = mode;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptions" object:nil];
}

- (void)setQuickRatio:(id)sender
{
    self.fMenuTorrent.ratioSetting = TR_RATIOLIMIT_SINGLE;
    self.fMenuTorrent.ratioLimit = [[sender representedObject] floatValue];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateOptions" object:nil];
}

- (void)setPriority:(id)sender
{
    tr_priority_t priority;
    switch ([sender tag])
    {
    case ActionMenuPriorityTagHigh:
        priority = TR_PRI_HIGH;
        break;
    case ActionMenuPriorityTagNormal:
        priority = TR_PRI_NORMAL;
        break;
    case ActionMenuPriorityTagLow:
        priority = TR_PRI_LOW;
        break;
    default:
        NSAssert1(NO, @"Unknown priority: %ld", [sender tag]);
        priority = TR_PRI_NORMAL;
    }

    self.fMenuTorrent.priority = priority;

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];
}

- (void)togglePiecesBar
{
    NSMutableArray* progressMarks = [NSMutableArray arrayWithCapacity:16];
    for (NSAnimationProgress i = 0.0625; i <= 1.0; i += 0.0625)
    {
        [progressMarks addObject:@(i)];
    }

    //this stops a previous animation
    self.fPiecesBarAnimation = [[NSAnimation alloc] initWithDuration:kToggleProgressSeconds animationCurve:NSAnimationEaseIn];
    self.fPiecesBarAnimation.animationBlockingMode = NSAnimationNonblocking;
    self.fPiecesBarAnimation.progressMarks = progressMarks;
    self.fPiecesBarAnimation.delegate = self;

    [self.fPiecesBarAnimation startAnimation];
}

- (void)animationDidEnd:(NSAnimation*)animation
{
    if (animation == self.fPiecesBarAnimation)
    {
        self.fPiecesBarAnimation = nil;
    }
}

- (void)animation:(NSAnimation*)animation didReachProgressMark:(NSAnimationProgress)progress
{
    if (animation == self.fPiecesBarAnimation)
    {
        if ([self.fDefaults boolForKey:@"PiecesBar"])
        {
            self.piecesBarPercent = progress;
        }
        else
        {
            self.piecesBarPercent = 1.0 - progress;
        }

        self.needsDisplay = YES;
    }
}

- (void)selectAndScrollToRow:(NSInteger)row
{
    NSParameterAssert(row >= 0);
    NSParameterAssert(row < self.numberOfRows);

    [self selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];

    NSRect const rowRect = [self rectOfRow:row];
    NSRect const viewRect = self.superview.frame;

    NSPoint scrollOrigin = rowRect.origin;
    scrollOrigin.y += (rowRect.size.height - viewRect.size.height) / 2;
    if (scrollOrigin.y < 0)
    {
        scrollOrigin.y = 0;
    }

    [[self.superview animator] setBoundsOrigin:scrollOrigin];
}

#pragma mark - Private

- (BOOL)pointInGroupStatusRect:(NSPoint)point
{
    NSInteger row = [self rowAtPoint:point];
    if (row < 0 || [[self itemAtRow:row] isKindOfClass:[Torrent class]])
    {
        return NO;
    }

    NSString* ident = (self.tableColumns[[self columnAtPoint:point]]).identifier;
    return [ident isEqualToString:@"UL"] || [ident isEqualToString:@"UL Image"] || [ident isEqualToString:@"DL"] ||
        [ident isEqualToString:@"DL Image"];
}

- (void)setGroupStatusColumns
{
    BOOL const ratio = [self.fDefaults boolForKey:@"DisplayGroupRowRatio"];

    [self tableColumnWithIdentifier:@"DL"].hidden = ratio;
    [self tableColumnWithIdentifier:@"DL Image"].hidden = ratio;
}

@end
