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

#import "TorrentTableView.h"
#import "Controller.h"
#import "FileListNode.h"
#import "InfoOptionsViewController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "Torrent.h"
#import "TorrentCell.h"
#import "TorrentGroup.h"

#define MAX_GROUP 999999

//eliminate when Lion-only
#define ACTION_MENU_GLOBAL_TAG 101
#define ACTION_MENU_UNLIMITED_TAG 102
#define ACTION_MENU_LIMIT_TAG 103

#define ACTION_MENU_PRIORITY_HIGH_TAG 101
#define ACTION_MENU_PRIORITY_NORMAL_TAG 102
#define ACTION_MENU_PRIORITY_LOW_TAG 103

#define TOGGLE_PROGRESS_SECONDS 0.175

@interface TorrentTableView (Private)

- (BOOL) pointInGroupStatusRect: (NSPoint) point;

- (void) setGroupStatusColumns;

@end

@implementation TorrentTableView

- (id) initWithCoder: (NSCoder *) decoder
{
    if ((self = [super initWithCoder: decoder]))
    {
        fDefaults = [NSUserDefaults standardUserDefaults];

        fTorrentCell = [[TorrentCell alloc] init];

        NSData * groupData = [fDefaults dataForKey: @"CollapsedGroups"];
        if (groupData)
            fCollapsedGroups = [[NSUnarchiver unarchiveObjectWithData: groupData] mutableCopy];
        else
            fCollapsedGroups = [[NSMutableIndexSet alloc] init];

        fMouseRow = -1;
        fMouseControlRow = -1;
        fMouseRevealRow = -1;
        fMouseActionRow = -1;

        fActionPopoverShown = NO;

        [self setDelegate: self];

        fPiecesBarPercent = [fDefaults boolForKey: @"PiecesBar"] ? 1.0 : 0.0;
    }

    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
}

- (void) awakeFromNib
{
    //set group columns to show ratio, needs to be in awakeFromNib to size columns correctly
    [self setGroupStatusColumns];

    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(setNeedsDisplay) name: @"RefreshTorrentTable" object: nil];
}

- (BOOL) isGroupCollapsed: (NSInteger) value
{
    if (value == -1)
        value = MAX_GROUP;

    return [fCollapsedGroups containsIndex: value];
}

- (void) removeCollapsedGroup: (NSInteger) value
{
    if (value == -1)
        value = MAX_GROUP;

    [fCollapsedGroups removeIndex: value];
}

- (void) removeAllCollapsedGroups
{
    [fCollapsedGroups removeAllIndexes];
}

- (void) saveCollapsedGroups
{
    [fDefaults setObject: [NSArchiver archivedDataWithRootObject: fCollapsedGroups] forKey: @"CollapsedGroups"];
}

- (BOOL) outlineView: (NSOutlineView *) outlineView isGroupItem: (id) item
{
    return ![item isKindOfClass: [Torrent class]];
}

- (CGFloat) outlineView: (NSOutlineView *) outlineView heightOfRowByItem: (id) item
{
    return [item isKindOfClass: [Torrent class]] ? [self rowHeight] : GROUP_SEPARATOR_HEIGHT;
}

- (NSCell *) outlineView: (NSOutlineView *) outlineView dataCellForTableColumn: (NSTableColumn *) tableColumn item: (id) item
{
    const BOOL group = ![item isKindOfClass: [Torrent class]];
    if (!tableColumn)
        return !group ? fTorrentCell : nil;
    else
        return group ? [tableColumn dataCellForRow: [self rowForItem: item]] : nil;
}

- (void) outlineView: (NSOutlineView *) outlineView willDisplayCell: (id) cell forTableColumn: (NSTableColumn *) tableColumn
    item: (id) item
{
    if ([item isKindOfClass: [Torrent class]])
    {
        if (!tableColumn)
        {
            [cell setRepresentedObject: item];

            const NSInteger row = [self rowForItem: item];
            [cell setHover: row == fMouseRow];
            [cell setControlHover: row == fMouseControlRow];
            [cell setRevealHover: row == fMouseRevealRow];
            [cell setActionHover: row == fMouseActionRow];
        }
    }
}

- (NSRect) frameOfCellAtColumn: (NSInteger) column row: (NSInteger) row
{
    if (column == -1)
        return [self rectOfRow: row];
    else
    {
        NSRect rect = [super frameOfCellAtColumn: column row: row];

        //adjust placement for proper vertical alignment
        if (column == [self columnWithIdentifier: @"Group"])
            rect.size.height -= 1.0f;

        return rect;
    }
}

- (NSString *) outlineView: (NSOutlineView *) outlineView typeSelectStringForTableColumn: (NSTableColumn *) tableColumn item: (id) item
{
    if ([item isKindOfClass: [Torrent class]])
        return [(Torrent *)item name];
    else
        return [[self dataSource] outlineView:outlineView objectValueForTableColumn:[self tableColumnWithIdentifier:@"Group"] byItem:item];
}

- (NSString *) outlineView: (NSOutlineView *) outlineView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect tableColumn: (NSTableColumn *) column item: (id) item mouseLocation: (NSPoint) mouseLocation
{
    NSString * ident = [column identifier];
    if ([ident isEqualToString: @"DL"] || [ident isEqualToString: @"DL Image"])
        return NSLocalizedString(@"Download speed", "Torrent table -> group row -> tooltip");
    else if ([ident isEqualToString: @"UL"] || [ident isEqualToString: @"UL Image"])
        return [fDefaults boolForKey: @"DisplayGroupRowRatio"] ? NSLocalizedString(@"Ratio", "Torrent table -> group row -> tooltip")
                : NSLocalizedString(@"Upload speed", "Torrent table -> group row -> tooltip");
    else if (ident)
    {
        NSUInteger count = [[item torrents] count];
        if (count == 1)
            return NSLocalizedString(@"1 transfer", "Torrent table -> group row -> tooltip");
        else
            return [NSString stringWithFormat: NSLocalizedString(@"%@ transfers", "Torrent table -> group row -> tooltip"),
                    [NSString formattedUInteger: count]];
    }
    else
        return nil;
}

- (void) updateTrackingAreas
{
    [super updateTrackingAreas];
    [self removeTrackingAreas];

    const NSRange rows = [self rowsInRect: [self visibleRect]];
    if (rows.length == 0)
        return;

    NSPoint mouseLocation = [self convertPoint: [[self window] mouseLocationOutsideOfEventStream] fromView: nil];
    for (NSUInteger row = rows.location; row < NSMaxRange(rows); row++)
    {
        if (![[self itemAtRow: row] isKindOfClass: [Torrent class]])
            continue;

        NSDictionary * userInfo = @{@"Row": @(row)};
        TorrentCell * cell = (TorrentCell *)[self preparedCellAtColumn: -1 row: row];
        [cell addTrackingAreasForView: self inRect: [self rectOfRow: row] withUserInfo: userInfo mouseLocation: mouseLocation];
    }
}

- (void) removeTrackingAreas
{
    fMouseRow = -1;
    fMouseControlRow = -1;
    fMouseRevealRow = -1;
    fMouseActionRow = -1;

    for (NSTrackingArea * area in [self trackingAreas])
    {
        if ([area owner] == self && [area userInfo][@"Row"])
            [self removeTrackingArea: area];
    }
}

- (void) setRowHover: (NSInteger) row
{
    NSAssert([fDefaults boolForKey: @"SmallView"], @"cannot set a hover row when not in compact view");

    fMouseRow = row;
    if (row >= 0)
        [self setNeedsDisplayInRect: [self rectOfRow: row]];
}

- (void) setControlButtonHover: (NSInteger) row
{
    fMouseControlRow = row;
    if (row >= 0)
        [self setNeedsDisplayInRect: [self rectOfRow: row]];
}

- (void) setRevealButtonHover: (NSInteger) row
{
    fMouseRevealRow = row;
    if (row >= 0)
        [self setNeedsDisplayInRect: [self rectOfRow: row]];
}

- (void) setActionButtonHover: (NSInteger) row
{
    fMouseActionRow = row;
    if (row >= 0)
        [self setNeedsDisplayInRect: [self rectOfRow: row]];
}

- (void) mouseEntered: (NSEvent *) event
{
    NSDictionary * dict = (NSDictionary *)[event userData];

    NSNumber * row;
    if ((row = dict[@"Row"]))
    {
        NSInteger rowVal = [row integerValue];
        NSString * type = dict[@"Type"];
        if ([type isEqualToString: @"Action"])
            fMouseActionRow = rowVal;
        else if ([type isEqualToString: @"Control"])
            fMouseControlRow = rowVal;
        else if ([type isEqualToString: @"Reveal"])
            fMouseRevealRow = rowVal;
        else
        {
            fMouseRow = rowVal;
            if (![fDefaults boolForKey: @"SmallView"])
                return;
        }

        [self setNeedsDisplayInRect: [self rectOfRow: rowVal]];
    }
}

- (void) mouseExited: (NSEvent *) event
{
    NSDictionary * dict = (NSDictionary *)[event userData];

    NSNumber * row;
    if ((row = dict[@"Row"]))
    {
        NSString * type = dict[@"Type"];
        if ([type isEqualToString: @"Action"])
            fMouseActionRow = -1;
        else if ([type isEqualToString: @"Control"])
            fMouseControlRow = -1;
        else if ([type isEqualToString: @"Reveal"])
            fMouseRevealRow = -1;
        else
        {
            fMouseRow = -1;
            if (![fDefaults boolForKey: @"SmallView"])
                return;
        }

        [self setNeedsDisplayInRect: [self rectOfRow: [row integerValue]]];
    }
}

- (void) outlineViewSelectionIsChanging: (NSNotification *) notification
{
    #warning elliminate when view-based?
    //if pushing a button, don't change the selected rows
    if (fSelectedValues)
        [self selectValues: fSelectedValues];
}

- (void) outlineViewItemDidExpand: (NSNotification *) notification
{
    NSInteger value = [[notification userInfo][@"NSObject"] groupIndex];
    if (value < 0)
        value = MAX_GROUP;

    if ([fCollapsedGroups containsIndex: value])
    {
        [fCollapsedGroups removeIndex: value];
        [[NSNotificationCenter defaultCenter] postNotificationName: @"OutlineExpandCollapse" object: self];
    }
}

- (void) outlineViewItemDidCollapse: (NSNotification *) notification
{
    NSInteger value = [[notification userInfo][@"NSObject"] groupIndex];
    if (value < 0)
        value = MAX_GROUP;

    [fCollapsedGroups addIndex: value];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"OutlineExpandCollapse" object: self];
}

- (void) mouseDown: (NSEvent *) event
{
    NSPoint point = [self convertPoint: [event locationInWindow] fromView: nil];
    const NSInteger row = [self rowAtPoint: point];

    //check to toggle group status before anything else
    if ([self pointInGroupStatusRect: point])
    {
        [fDefaults setBool: ![fDefaults boolForKey: @"DisplayGroupRowRatio"] forKey: @"DisplayGroupRowRatio"];
        [self setGroupStatusColumns];

        return;
    }

    const BOOL pushed = row != -1 && (fMouseActionRow == row || fMouseRevealRow == row || fMouseControlRow == row);

    //if pushing a button, don't change the selected rows
    if (pushed)
        fSelectedValues = [self selectedValues];

    [super mouseDown: event];

    fSelectedValues = nil;

    //avoid weird behavior when showing menu by doing this after mouse down
    if (row != -1 && fMouseActionRow == row)
    {
        #warning maybe make appear on mouse down
        [self displayTorrentActionPopoverForEvent: event];
    }
    else if (!pushed && [event clickCount] == 2) //double click
    {
        id item = nil;
        if (row != -1)
            item = [self itemAtRow: row];

        if (!item || [item isKindOfClass: [Torrent class]])
            [fController showInfo: nil];
        else
        {
            if ([self isItemExpanded: item])
                [self collapseItem: item];
            else
                [self expandItem: item];
        }
    }
    else;
}

- (void) selectValues: (NSArray *) values
{
    NSMutableIndexSet * indexSet = [NSMutableIndexSet indexSet];

    for (id item in values)
    {
        if ([item isKindOfClass: [Torrent class]])
        {
            const NSInteger index = [self rowForItem: item];
            if (index != -1)
                [indexSet addIndex: index];
        }
        else
        {
            const NSInteger group = [item groupIndex];
            for (NSInteger i = 0; i < [self numberOfRows]; i++)
            {
                id tableItem = [self itemAtRow: i];
                if ([tableItem isKindOfClass: [TorrentGroup class]] && group == [tableItem groupIndex])
                {
                    [indexSet addIndex: i];
                    break;
                }
            }
        }
    }

    [self selectRowIndexes: indexSet byExtendingSelection: NO];
}

- (NSArray *) selectedValues
{
    NSIndexSet * selectedIndexes = [self selectedRowIndexes];
    NSMutableArray * values = [NSMutableArray arrayWithCapacity: [selectedIndexes count]];

    for (NSUInteger i = [selectedIndexes firstIndex]; i != NSNotFound; i = [selectedIndexes indexGreaterThanIndex: i])
        [values addObject: [self itemAtRow: i]];

    return values;
}

- (NSArray *) selectedTorrents
{
    NSIndexSet * selectedIndexes = [self selectedRowIndexes];
    NSMutableArray * torrents = [NSMutableArray arrayWithCapacity: [selectedIndexes count]]; //take a shot at guessing capacity

    for (NSUInteger i = [selectedIndexes firstIndex]; i != NSNotFound; i = [selectedIndexes indexGreaterThanIndex: i])
    {
        id item = [self itemAtRow: i];
        if ([item isKindOfClass: [Torrent class]])
            [torrents addObject: item];
        else
        {
            NSArray * groupTorrents = [item torrents];
            [torrents addObjectsFromArray: groupTorrents];
            if ([self isItemExpanded: item])
                i +=[groupTorrents count];
        }
    }

    return torrents;
}

- (NSMenu *) menuForEvent: (NSEvent *) event
{
    NSInteger row = [self rowAtPoint: [self convertPoint: [event locationInWindow] fromView: nil]];
    if (row >= 0)
    {
        if (![self isRowSelected: row])
            [self selectRowIndexes: [NSIndexSet indexSetWithIndex: row] byExtendingSelection: NO];
        return fContextRow;
    }
    else
    {
        [self deselectAll: self];
        return fContextNoRow;
    }
}

//make sure that the pause buttons become orange when holding down the option key
- (void) flagsChanged: (NSEvent *) event
{
    [self display];
    [super flagsChanged: event];
}

//option-command-f will focus the filter bar's search field
- (void) keyDown: (NSEvent *) event
{
    const unichar firstChar = [[event charactersIgnoringModifiers] characterAtIndex: 0];

    if (firstChar == 'f' && [event modifierFlags] & NSAlternateKeyMask && [event modifierFlags] & NSCommandKeyMask)
        [fController focusFilterField];
    else if (firstChar == ' ')
        [fController toggleQuickLook: nil];
    else if ([event keyCode] == 53) //esc key
        [self deselectAll: nil];
    else
        [super keyDown: event];
}

- (NSRect) iconRectForRow: (NSInteger) row
{
    return [fTorrentCell iconRectForBounds: [self rectOfRow: row]];
}

- (void) paste: (id) sender
{
    NSURL * url;
    if ((url = [NSURL URLFromPasteboard: [NSPasteboard generalPasteboard]]))
        [fController openURL: [url absoluteString]];
    else
    {
        NSArray * items = [[NSPasteboard generalPasteboard] readObjectsForClasses: @[[NSString class]] options: nil];
        if (items)
        {
            NSDataDetector * detector = [NSDataDetector dataDetectorWithTypes: NSTextCheckingTypeLink error: nil];
            for (__strong NSString * pbItem in items)
            {
                pbItem = [pbItem stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                if ([pbItem rangeOfString: @"magnet:" options: (NSAnchoredSearch | NSCaseInsensitiveSearch)].location != NSNotFound)
                    [fController openURL: pbItem];
                else
                {
                    #warning only accept full text?
                    for (NSTextCheckingResult * result in [detector matchesInString: pbItem options: 0 range: NSMakeRange(0, [pbItem length])])
                        [fController openURL: [[result URL] absoluteString]];
                }
            }
        }
    }
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];

    if (action == @selector(paste:))
    {
        if ([[[NSPasteboard generalPasteboard] types] containsObject: NSURLPboardType])
            return YES;

        NSArray * items = [[NSPasteboard generalPasteboard] readObjectsForClasses: @[[NSString class]] options: nil];
        if (items)
        {
            NSDataDetector * detector = [NSDataDetector dataDetectorWithTypes: NSTextCheckingTypeLink error: nil];
            for (__strong NSString * pbItem in items)
            {
                pbItem = [pbItem stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                if (([pbItem rangeOfString: @"magnet:" options: (NSAnchoredSearch | NSCaseInsensitiveSearch)].location != NSNotFound)
                    || [detector firstMatchInString: pbItem options: 0 range: NSMakeRange(0, [pbItem length])])
                    return YES;
            }
        }

    return NO;
    }

    return YES;
}

- (void) toggleControlForTorrent: (Torrent *) torrent
{
    if ([torrent isActive])
        [fController stopTorrents: @[torrent]];
    else
    {
        if ([NSEvent modifierFlags] & NSAlternateKeyMask)
            [fController resumeTorrentsNoWait: @[torrent]];
        else if ([torrent waitingToStart])
            [fController stopTorrents: @[torrent]];
        else
            [fController resumeTorrents: @[torrent]];
    }
}

- (void) displayTorrentActionPopoverForEvent: (NSEvent *) event
{
    const NSInteger row = [self rowAtPoint: [self convertPoint: [event locationInWindow] fromView: nil]];
    if (row < 0)
        return;

    const NSRect rect = [fTorrentCell iconRectForBounds: [self rectOfRow: row]];

    if (fActionPopoverShown)
        return;

    Torrent * torrent = [self itemAtRow: row];

    NSPopover * popover = [[NSPopover alloc] init];
    [popover setBehavior: NSPopoverBehaviorTransient];
    InfoOptionsViewController * infoViewController = [[InfoOptionsViewController alloc] init];
    [popover setContentViewController: infoViewController];
    [popover setDelegate: self];

    [popover showRelativeToRect: rect ofView: self preferredEdge: NSMaxYEdge];
    [infoViewController setInfoForTorrents: @[torrent]];
    [infoViewController updateInfo];

}

//don't show multiple popovers when clicking the gear button repeatedly
- (void) popoverWillShow: (NSNotification *) notification
{
    fActionPopoverShown = YES;
}

- (void) popoverWillClose: (NSNotification *) notification
{
    fActionPopoverShown = NO;
}

//eliminate when Lion-only, along with all the menu item instance variables
- (void) menuNeedsUpdate: (NSMenu *) menu
{
    //this method seems to be called when it shouldn't be
    if (!fMenuTorrent || ![menu supermenu])
        return;

    if (menu == fUploadMenu || menu == fDownloadMenu)
    {
        NSMenuItem * item;
        if ([menu numberOfItems] == 3)
        {
            const NSInteger speedLimitActionValue[] = { 0, 5, 10, 20, 30, 40, 50, 75, 100, 150, 200, 250, 500,
                                                        750, 1000, 1500, 2000, -1 };

            for (NSInteger i = 0; speedLimitActionValue[i] != -1; i++)
            {
                item = [[NSMenuItem alloc] initWithTitle: [NSString stringWithFormat: NSLocalizedString(@"%d KB/s",
                        "Action menu -> upload/download limit"), speedLimitActionValue[i]] action: @selector(setQuickLimit:)
                        keyEquivalent: @""];
                [item setTarget: self];
                [item setRepresentedObject: @(speedLimitActionValue[i])];
                [menu addItem: item];
            }
        }

        const BOOL upload = menu == fUploadMenu;
        const BOOL limit = [fMenuTorrent usesSpeedLimit: upload];

        item = [menu itemWithTag: ACTION_MENU_LIMIT_TAG];
        [item setState: limit ? NSOnState : NSOffState];
        [item setTitle: [NSString stringWithFormat: NSLocalizedString(@"Limit (%d KB/s)",
                            "torrent action menu -> upload/download limit"), [fMenuTorrent speedLimit: upload]]];

        item = [menu itemWithTag: ACTION_MENU_UNLIMITED_TAG];
        [item setState: !limit ? NSOnState : NSOffState];
    }
    else if (menu == fRatioMenu)
    {
        NSMenuItem * item;
        if ([menu numberOfItems] == 4)
        {
            const float ratioLimitActionValue[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, -1.0 };

            for (NSInteger i = 0; ratioLimitActionValue[i] != -1.0; i++)
            {
                item = [[NSMenuItem alloc] initWithTitle: [NSString localizedStringWithFormat: @"%.2f", ratioLimitActionValue[i]]
                        action: @selector(setQuickRatio:) keyEquivalent: @""];
                [item setTarget: self];
                [item setRepresentedObject: @(ratioLimitActionValue[i])];
                [menu addItem: item];
            }
        }

        const tr_ratiolimit mode = [fMenuTorrent ratioSetting];

        item = [menu itemWithTag: ACTION_MENU_LIMIT_TAG];
        [item setState: mode == TR_RATIOLIMIT_SINGLE ? NSOnState : NSOffState];
        [item setTitle: [NSString localizedStringWithFormat: NSLocalizedString(@"Stop at Ratio (%.2f)",
            "torrent action menu -> ratio stop"), [fMenuTorrent ratioLimit]]];

        item = [menu itemWithTag: ACTION_MENU_UNLIMITED_TAG];
        [item setState: mode == TR_RATIOLIMIT_UNLIMITED ? NSOnState : NSOffState];

        item = [menu itemWithTag: ACTION_MENU_GLOBAL_TAG];
        [item setState: mode == TR_RATIOLIMIT_GLOBAL ? NSOnState : NSOffState];
    }
    else if (menu == fPriorityMenu)
    {
        const tr_priority_t priority = [fMenuTorrent priority];

        NSMenuItem * item = [menu itemWithTag: ACTION_MENU_PRIORITY_HIGH_TAG];
        [item setState: priority == TR_PRI_HIGH ? NSOnState : NSOffState];

        item = [menu itemWithTag: ACTION_MENU_PRIORITY_NORMAL_TAG];
        [item setState: priority == TR_PRI_NORMAL ? NSOnState : NSOffState];

        item = [menu itemWithTag: ACTION_MENU_PRIORITY_LOW_TAG];
        [item setState: priority == TR_PRI_LOW ? NSOnState : NSOffState];
    }
}

//the following methods might not be needed when Lion-only
- (void) setQuickLimitMode: (id) sender
{
    const BOOL limit = [sender tag] == ACTION_MENU_LIMIT_TAG;
    [fMenuTorrent setUseSpeedLimit: limit upload: [sender menu] == fUploadMenu];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateOptions" object: nil];
}

- (void) setQuickLimit: (id) sender
{
    const BOOL upload = [sender menu] == fUploadMenu;
    [fMenuTorrent setUseSpeedLimit: YES upload: upload];
    [fMenuTorrent setSpeedLimit: [[sender representedObject] intValue] upload: upload];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateOptions" object: nil];
}

- (void) setGlobalLimit: (id) sender
{
    [fMenuTorrent setUseGlobalSpeedLimit: [(NSButton *)sender state] != NSOnState];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateOptions" object: nil];
}

- (void) setQuickRatioMode: (id) sender
{
    tr_ratiolimit mode;
    switch ([sender tag])
    {
        case ACTION_MENU_UNLIMITED_TAG:
            mode = TR_RATIOLIMIT_UNLIMITED;
            break;
        case ACTION_MENU_LIMIT_TAG:
            mode = TR_RATIOLIMIT_SINGLE;
            break;
        case ACTION_MENU_GLOBAL_TAG:
            mode = TR_RATIOLIMIT_GLOBAL;
            break;
        default:
            return;
    }

    [fMenuTorrent setRatioSetting: mode];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateOptions" object: nil];
}

- (void) setQuickRatio: (id) sender
{
    [fMenuTorrent setRatioSetting: TR_RATIOLIMIT_SINGLE];
    [fMenuTorrent setRatioLimit: [[sender representedObject] floatValue]];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateOptions" object: nil];
}

- (void) setPriority: (id) sender
{
    tr_priority_t priority;
    switch ([sender tag])
    {
        case ACTION_MENU_PRIORITY_HIGH_TAG:
            priority = TR_PRI_HIGH;
            break;
        case ACTION_MENU_PRIORITY_NORMAL_TAG:
            priority = TR_PRI_NORMAL;
            break;
        case ACTION_MENU_PRIORITY_LOW_TAG:
            priority = TR_PRI_LOW;
            break;
        default:
            NSAssert1(NO, @"Unknown priority: %ld", [sender tag]);
            priority = TR_PRI_NORMAL;
    }

    [fMenuTorrent setPriority: priority];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
}

- (void) togglePiecesBar
{
    NSMutableArray * progressMarks = [NSMutableArray arrayWithCapacity: 16];
    for (NSAnimationProgress i = 0.0625; i <= 1.0; i += 0.0625)
        [progressMarks addObject: @(i)];

    //this stops a previous animation
    fPiecesBarAnimation = [[NSAnimation alloc] initWithDuration: TOGGLE_PROGRESS_SECONDS animationCurve: NSAnimationEaseIn];
    [fPiecesBarAnimation setAnimationBlockingMode: NSAnimationNonblocking];
    [fPiecesBarAnimation setProgressMarks: progressMarks];
    [fPiecesBarAnimation setDelegate: self];

    [fPiecesBarAnimation startAnimation];
}

- (void) animationDidEnd: (NSAnimation *) animation
{
    if (animation == fPiecesBarAnimation)
    {
        fPiecesBarAnimation = nil;
    }
}

- (void) animation: (NSAnimation *) animation didReachProgressMark: (NSAnimationProgress) progress
{
    if (animation == fPiecesBarAnimation)
    {
        if ([fDefaults boolForKey: @"PiecesBar"])
            fPiecesBarPercent = progress;
        else
            fPiecesBarPercent = 1.0 - progress;

        [self setNeedsDisplay: YES];
    }
}

- (CGFloat) piecesBarPercent
{
    return fPiecesBarPercent;
}

- (void) selectAndScrollToRow: (NSInteger) row
{
    NSParameterAssert(row >= 0);
    NSParameterAssert(row < [self numberOfRows]);

    [self selectRowIndexes: [NSIndexSet indexSetWithIndex: row] byExtendingSelection: NO];

    const NSRect rowRect = [self rectOfRow: row];
    const NSRect viewRect = [[self superview] frame];

    NSPoint scrollOrigin = rowRect.origin;
    scrollOrigin.y += (rowRect.size.height - viewRect.size.height) / 2;
    if (scrollOrigin.y < 0)
        scrollOrigin.y = 0;

    [[[self superview] animator] setBoundsOrigin: scrollOrigin];
}

@end

@implementation TorrentTableView (Private)

- (BOOL) pointInGroupStatusRect: (NSPoint) point
{
    NSInteger row = [self rowAtPoint: point];
    if (row < 0 || [[self itemAtRow: row] isKindOfClass: [Torrent class]])
        return NO;

    NSString * ident = [[self tableColumns][[self columnAtPoint: point]] identifier];
    return [ident isEqualToString: @"UL"] || [ident isEqualToString: @"UL Image"]
            || [ident isEqualToString: @"DL"] || [ident isEqualToString: @"DL Image"];
}

- (void) setGroupStatusColumns
{
    const BOOL ratio = [fDefaults boolForKey: @"DisplayGroupRowRatio"];

    [[self tableColumnWithIdentifier: @"DL"] setHidden: ratio];
    [[self tableColumnWithIdentifier: @"DL Image"] setHidden: ratio];
}

@end
