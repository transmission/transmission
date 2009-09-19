/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2009 Transmission authors and contributors
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
#import "TorrentCell.h"
#import "Torrent.h"
#import "TorrentGroup.h"
#import "FileListNode.h"
#import "NSApplicationAdditions.h"

#define MAX_GROUP 999999

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

- (void) createFileMenu: (NSMenu *) menu forFiles: (NSArray *) files;

- (NSArray *) quickLookableTorrents;

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
        
        fMouseControlRow = -1;
        fMouseRevealRow = -1;
        fMouseActionRow = -1;
        fActionPushedRow = -1;
        
        [self setDelegate: self];
        
        fPiecesBarPercent = [fDefaults boolForKey: @"PiecesBar"] ? 1.0f : 0.0f;
    }
    
    return self;
}

- (void) dealloc
{
    [fPreviewPanel release];
    
    [fCollapsedGroups release];
    
    [fPiecesBarAnimation release];
    [fMenuTorrent release];
    
    [fSelectedValues release];
    
    [fTorrentCell release];
    
    [super dealloc];
}

- (void) awakeFromNib
{
    //set group columns to show ratio, needs to be in awakeFromNib to size columns correctly
    [self setGroupStatusColumns];
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
    BOOL group = ![item isKindOfClass: [Torrent class]];
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
        [cell setRepresentedObject: item];
        
        const NSInteger row = [self rowForItem: item];
        [cell setControlHover: row == fMouseControlRow];
        [cell setRevealHover: row == fMouseRevealRow];
        [cell setActionHover: row == fMouseActionRow];
        [cell setActionPushed: row == fActionPushedRow];
    }
    else
    {
        NSString * ident = [tableColumn identifier];
        if ([ident isEqualToString: @"UL Image"] || [ident isEqualToString: @"DL Image"])
        {
            //ensure arrows are white only when selected
            if ([NSApp isOnSnowLeopardOrBetter])
                [[cell image] setTemplate: [cell backgroundStyle] == NSBackgroundStyleLowered];
            else
            {
                NSImage * image = [cell image];
                const BOOL template = [cell backgroundStyle] == NSBackgroundStyleLowered;
                if ([image isTemplate] != template)
                {
                    [image setTemplate: template];
                    [cell setImage: nil];
                    [cell setImage: image];
                }
            }
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
    return [item isKindOfClass: [Torrent class]] ? [item name]
            : [[self preparedCellAtColumn: [self columnWithIdentifier: @"Group"] row: [self rowForItem: item]] stringValue];
}

- (NSString *) outlineView: (NSOutlineView *) outlineView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) column item: (id) item mouseLocation: (NSPoint) mouseLocation
{
    NSString * ident = [column identifier];
    if ([ident isEqualToString: @"DL"] || [ident isEqualToString: @"DL Image"])
        return NSLocalizedString(@"Download speed", "Torrent table -> group row -> tooltip");
    else if ([ident isEqualToString: @"UL"] || [ident isEqualToString: @"UL Image"])
        return [fDefaults boolForKey: @"DisplayGroupRowRatio"] ? NSLocalizedString(@"Ratio", "Torrent table -> group row -> tooltip")
                : NSLocalizedString(@"Upload speed", "Torrent table -> group row -> tooltip");
    else if (ident)
    {
        NSInteger count = [[item torrents] count];
        if (count == 1)
            return NSLocalizedString(@"1 transfer", "Torrent table -> group row -> tooltip");
        else
            return [NSString stringWithFormat: NSLocalizedString(@"%d transfers", "Torrent table -> group row -> tooltip"), count];
    }
    else
        return nil;
}

- (void) updateTrackingAreas
{
    [super updateTrackingAreas];
    [self removeButtonTrackingAreas];
    
    NSRange rows = [self rowsInRect: [self visibleRect]];
    if (rows.length == 0)
        return;
    
    NSPoint mouseLocation = [self convertPoint: [[self window] convertScreenToBase: [NSEvent mouseLocation]] fromView: nil];
    for (NSUInteger row = rows.location; row < NSMaxRange(rows); row++)
    {
        if (![[self itemAtRow: row] isKindOfClass: [Torrent class]])
            continue;
        
        NSDictionary * userInfo = [NSDictionary dictionaryWithObject: [NSNumber numberWithInt: row] forKey: @"Row"];
        TorrentCell * cell = (TorrentCell *)[self preparedCellAtColumn: -1 row: row];
        [cell addTrackingAreasForView: self inRect: [self rectOfRow: row] withUserInfo: userInfo mouseLocation: mouseLocation];
    }
}

- (void) removeButtonTrackingAreas
{
    fMouseControlRow = -1;
    fMouseRevealRow = -1;
    fMouseActionRow = -1;
    
    for (NSTrackingArea * area in [self trackingAreas])
    {
        if ([area owner] == self && [[area userInfo] objectForKey: @"Row"])
            [self removeTrackingArea: area];
    }
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
    if ((row = [dict objectForKey: @"Row"]))
    {
        NSInteger rowVal = [row intValue];
        NSString * type = [dict objectForKey: @"Type"];
        if ([type isEqualToString: @"Action"])
            fMouseActionRow = rowVal;
        else if ([type isEqualToString: @"Control"])
            fMouseControlRow = rowVal;
        else
            fMouseRevealRow = rowVal;
        
        [self setNeedsDisplayInRect: [self rectOfRow: rowVal]];
    }
}

- (void) mouseExited: (NSEvent *) event
{
    NSDictionary * dict = (NSDictionary *)[event userData];
    
    NSNumber * row;
    if ((row = [dict objectForKey: @"Row"]))
    {
        NSString * type = [dict objectForKey: @"Type"];
        if ([type isEqualToString: @"Action"])
            fMouseActionRow = -1;
        else if ([type isEqualToString: @"Control"])
            fMouseControlRow = -1;
        else
            fMouseRevealRow = -1;
        
        [self setNeedsDisplayInRect: [self rectOfRow: [row intValue]]];
    }
}

- (void) outlineViewSelectionIsChanging: (NSNotification *) notification
{
    if (fSelectedValues)
        [self selectValues: fSelectedValues];
}

- (void) outlineViewItemDidExpand: (NSNotification *) notification
{
    NSInteger value = [[[notification userInfo] objectForKey: @"NSObject"] groupIndex];
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
    NSInteger value = [[[notification userInfo] objectForKey: @"NSObject"] groupIndex];
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
        fSelectedValues = [[self selectedValues] retain];
    
    [super mouseDown: event];
    
    [fSelectedValues release];
    fSelectedValues = nil;
    
    //avoid weird behavior when showing menu by doing this after mouse down
    if (row != -1 && fMouseActionRow == row)
    {
        fActionPushedRow = row;
        [self setNeedsDisplayInRect: [self rectOfRow: row]]; //ensure button is pushed down
        
        [self displayTorrentMenuForEvent: event];
        
        fActionPushedRow = -1;
        [self setNeedsDisplayInRect: [self rectOfRow: row]];
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
            NSInteger index = [self rowForItem: item];
            if (index != -1)
                [indexSet addIndex: index];
        }
        else
        {
            NSInteger group = [item groupIndex];
            for (NSInteger i = 0; i < [self numberOfRows]; i++)
            {
                if ([indexSet containsIndex: i])
                    continue;
                
                id tableItem = [self itemAtRow: i];
                if (![tableItem isKindOfClass: [Torrent class]] && group == [tableItem groupIndex])
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
        [fController openURL: url];
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];
    
    if (action == @selector(paste:))
        return [[[NSPasteboard generalPasteboard] types] containsObject: NSURLPboardType];
    
    return YES;
}

- (void) toggleControlForTorrent: (Torrent *) torrent
{
    if ([torrent isActive])
        [fController stopTorrents: [NSArray arrayWithObject: torrent]];
    else
    {
        if (([NSApp isOnSnowLeopardOrBetter] ? [NSEvent modifierFlags] : [[NSApp currentEvent] modifierFlags]) & NSAlternateKeyMask)
            [fController resumeTorrentsNoWait: [NSArray arrayWithObject: torrent]];
        else if ([torrent waitingToStart])
            [fController stopTorrents: [NSArray arrayWithObject: torrent]];
        else
            [fController resumeTorrents: [NSArray arrayWithObject: torrent]];
    }
}

- (void) displayTorrentMenuForEvent: (NSEvent *) event
{
    const NSInteger row = [self rowAtPoint: [self convertPoint: [event locationInWindow] fromView: nil]];
    if (row < 0)
        return;
    
    const NSInteger numberOfNonFileItems = [fActionMenu numberOfItems];
    
    //update file action menu
    fMenuTorrent = [[self itemAtRow: row] retain];
    [self createFileMenu: fActionMenu forFiles: [fMenuTorrent fileList]];
    
    //update global limit check
    [fGlobalLimitItem setState: [fMenuTorrent usesGlobalSpeedLimit] ? NSOnState : NSOffState];
    
    //place menu below button
    NSRect rect = [fTorrentCell iconRectForBounds: [self rectOfRow: row]];
    NSPoint location = rect.origin;
    location.y += rect.size.height + 5.0f;
    location = [self convertPoint: location toView: nil];
    
    NSEvent * newEvent = [NSEvent mouseEventWithType: [event type] location: location
        modifierFlags: [event modifierFlags] timestamp: [event timestamp] windowNumber: [event windowNumber]
        context: [event context] eventNumber: [event eventNumber] clickCount: [event clickCount] pressure: [event pressure]];
    
    [NSMenu popUpContextMenu: fActionMenu withEvent: newEvent forView: self];
    
    for (NSInteger i = [fActionMenu numberOfItems]-1; i >= numberOfNonFileItems; i--)
        [fActionMenu removeItemAtIndex: i];
    
    [fMenuTorrent release];
    fMenuTorrent = nil;
}

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
                [item setRepresentedObject: [NSNumber numberWithInt: speedLimitActionValue[i]]];
                [menu addItem: item];
                [item release];
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
            const CGFloat ratioLimitActionValue[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, -1.0f };
            
            for (NSInteger i = 0; ratioLimitActionValue[i] != -1.0f; i++)
            {
                item = [[NSMenuItem alloc] initWithTitle: [NSString localizedStringWithFormat: @"%.2f", ratioLimitActionValue[i]]
                        action: @selector(setQuickRatio:) keyEquivalent: @""];
                [item setTarget: self];
                [item setRepresentedObject: [NSNumber numberWithFloat: ratioLimitActionValue[i]]];
                [menu addItem: item];
                [item release];
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
    else //assume the menu is part of the file list
    {
        if ([menu numberOfItems] > 0)
            return;
        
        NSMenu * supermenu = [menu supermenu];
        [self createFileMenu: menu forFiles: [(FileListNode *)[[supermenu itemAtIndex: [supermenu indexOfItemWithSubmenu: menu]]
                                                representedObject] children]];
    }
}

//alternating rows - first row after group row is white
- (void) highlightSelectionInClipRect: (NSRect) clipRect
{
    NSRect visibleRect = clipRect;
    NSRange rows = [self rowsInRect: visibleRect];
    BOOL start = YES;
    
    const CGFloat totalRowHeight = [self rowHeight] + [self intercellSpacing].height;
    
    NSRect gridRects[(NSInteger)(ceil(visibleRect.size.height / totalRowHeight / 2.0)) + 1]; //add one if partial rows at top and bottom
    NSInteger rectNum = 0;
    
    if (rows.length > 0)
    {
        //determine what the first row color should be
        if ([[self itemAtRow: rows.location] isKindOfClass: [Torrent class]])
        {
            for (NSInteger i = rows.location-1; i>=0; i--)
            {
                if (![[self itemAtRow: i] isKindOfClass: [Torrent class]])
                    break;
                start = !start;
            }
        }
        else
        {
            rows.location++;
            rows.length--;
        }
        
        NSInteger i;
        for (i = rows.location; i < NSMaxRange(rows); i++)
        {
            if (![[self itemAtRow: i] isKindOfClass: [Torrent class]])
            {
                start = YES;
                continue;
            }
            
            if (!start && ![self isRowSelected: i])
                gridRects[rectNum++] = [self rectOfRow: i];
            
            start = !start;
        }
        
        const CGFloat newY = NSMaxY([self rectOfRow: i-1]);
        visibleRect.size.height -= newY - visibleRect.origin.y;
        visibleRect.origin.y = newY;
    }
    
    const NSInteger numberBlankRows = ceil(visibleRect.size.height / totalRowHeight);
    
    //remaining visible rows continue alternating
    visibleRect.size.height = totalRowHeight;
    if (start)
        visibleRect.origin.y += totalRowHeight;
    
    for (NSInteger i = start ? 1 : 0; i < numberBlankRows; i += 2)
    {
        gridRects[rectNum++] = visibleRect;
        visibleRect.origin.y += 2.0 * totalRowHeight;
    }
    
    NSAssert([[NSColor controlAlternatingRowBackgroundColors] count] >= 2, @"There should be 2 alternating row colors");
    
    [[[NSColor controlAlternatingRowBackgroundColors] objectAtIndex: 1] set];
    NSRectFillList(gridRects, rectNum);
    
    [super highlightSelectionInClipRect: clipRect];
}

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
    [fMenuTorrent setUseGlobalSpeedLimit: [sender state] != NSOnState];
    
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
            return;
    }
    
    [fMenuTorrent setPriority: priority];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
}

- (void) checkFile: (id) sender
{
    NSIndexSet * indexSet = [(FileListNode *)[sender representedObject] indexes];
    [fMenuTorrent setFileCheckState: [sender state] != NSOnState ? NSOnState : NSOffState forIndexes: indexSet];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateStats" object: nil];
}

- (void) togglePiecesBar
{
    //stop previous animation
    if (fPiecesBarAnimation)
        [fPiecesBarAnimation release];
    
    NSMutableArray * progressMarks = [NSMutableArray arrayWithCapacity: 16];
    for (NSAnimationProgress i = 0.0625f; i <= 1.0f; i += 0.0625f)
        [progressMarks addObject: [NSNumber numberWithFloat: i]];
    
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
        [fPiecesBarAnimation release];
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
            fPiecesBarPercent = 1.0f - progress;
        
        [self reloadData];
    }
}

- (CGFloat) piecesBarPercent
{
    return fPiecesBarPercent;
}

- (BOOL) acceptsPreviewPanelControl: (QLPreviewPanel *) panel
{
    return YES;
}

- (void) beginPreviewPanelControl: (QLPreviewPanel *) panel
{
    fPreviewPanel = [panel retain];
    fPreviewPanel.delegate = self;
    fPreviewPanel.dataSource = self;
}

- (void) endPreviewPanelControl: (QLPreviewPanel *) panel
{
    [fPreviewPanel release];
    fPreviewPanel = nil;
}

- (NSInteger) numberOfPreviewItemsInPreviewPanel: (QLPreviewPanel *) panel
{
    return [[self quickLookableTorrents] count];
}

- (id <QLPreviewItem>) previewPanel: (QLPreviewPanel *)panel previewItemAtIndex: (NSInteger) index
{
    return [[self quickLookableTorrents] objectAtIndex: index];
}

- (BOOL) previewPanel: (QLPreviewPanel *) panel handleEvent: (NSEvent *) event
{
    if ([event type] == NSKeyDown)
    {
        [super keyDown: event];
        return YES;
    }
    
    return NO;
}

- (NSRect) previewPanel: (QLPreviewPanel *) panel sourceFrameOnScreenForPreviewItem: (id <QLPreviewItem>) item
{
    const NSInteger row = [self rowForItem: item];
    if (row == -1)
        return NSZeroRect;
    
    NSRect frame = [self iconRectForRow: row];
    frame.origin = [self convertPoint: frame.origin toView: nil];
    frame.origin = [[self window] convertBaseToScreen: frame.origin];
    frame.origin.y -= frame.size.height;
    return frame;
}

@end

@implementation TorrentTableView (Private)

- (BOOL) pointInGroupStatusRect: (NSPoint) point
{
    NSInteger row = [self rowAtPoint: point];
    if (row < 0 || [[self itemAtRow: row] isKindOfClass: [Torrent class]])
        return NO;
    
    NSString * ident = [[[self tableColumns] objectAtIndex: [self columnAtPoint: point]] identifier];
    return [ident isEqualToString: @"UL"] || [ident isEqualToString: @"UL Image"]
            || [ident isEqualToString: @"DL"] || [ident isEqualToString: @"DL Image"];
}

- (void) setGroupStatusColumns
{
    const BOOL ratio = [fDefaults boolForKey: @"DisplayGroupRowRatio"];
    
    [[self tableColumnWithIdentifier: @"DL"] setHidden: ratio];
    [[self tableColumnWithIdentifier: @"DL Image"] setHidden: ratio];
}

- (void) createFileMenu: (NSMenu *) menu forFiles: (NSArray *) files
{
    for (FileListNode * node in files)
    {
        NSString * name = [node name];
        
        NSMenuItem * item = [[NSMenuItem alloc] initWithTitle: name action: @selector(checkFile:) keyEquivalent: @""];
        
        NSImage * icon;
        if (![node isFolder])
            icon = [[NSWorkspace sharedWorkspace] iconForFileType: [name pathExtension]];
        else
        {
            NSMenu * itemMenu = [[NSMenu alloc] initWithTitle: name];
            [itemMenu setAutoenablesItems: NO];
            [item setSubmenu: itemMenu];
            [itemMenu setDelegate: self];
            [itemMenu release];
            
            icon = [[NSWorkspace sharedWorkspace] iconForFileType: NSFileTypeForHFSTypeCode('fldr')];
        }
        
        [item setRepresentedObject: node];
        
        [icon setSize: NSMakeSize(16.0, 16.0)];
        [item setImage: icon];
        
        NSIndexSet * indexSet = [node indexes];
        [item setState: [fMenuTorrent checkForFiles: indexSet]];
        [item setEnabled: [fMenuTorrent canChangeDownloadCheckForFiles: indexSet]];
        
        [menu addItem: item];
        [item release];
    }
}

- (NSArray *) quickLookableTorrents
{
    NSArray * selectedTorrents = [self selectedTorrents];
    NSMutableArray * qlArray = [NSMutableArray arrayWithCapacity: [selectedTorrents count]];
    
    for (Torrent * torrent in selectedTorrents)
        if (([torrent isFolder] || [torrent isComplete]) && [[NSFileManager defaultManager] fileExistsAtPath: [torrent dataLocation]])
            [qlArray addObject: torrent];
    
    return qlArray;
}

@end
