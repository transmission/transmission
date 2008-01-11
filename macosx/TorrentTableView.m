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

#import "TorrentTableView.h"
#import "TorrentCell.h"
#import "Controller.h"
#import "Torrent.h"
#import "NSApplicationAdditions.h"
#import "NSMenuAdditions.h"

#define PADDING 3.0

#define BUTTON_TO_TOP_REGULAR 33.0
#define BUTTON_TO_TOP_SMALL 20.0

#define ACTION_BUTTON_TO_TOP 44.0

#define ACTION_MENU_GLOBAL_TAG 101
#define ACTION_MENU_UNLIMITED_TAG 102
#define ACTION_MENU_LIMIT_TAG 103

@interface TorrentTableView (Private)

- (NSRect) pauseRectForRow: (int) row;
- (NSRect) revealRectForRow: (int) row;
- (NSRect) actionRectForRow: (int) row;

- (BOOL) pointInIconRect: (NSPoint) point;
- (BOOL) pointInProgressRect: (NSPoint) point;
- (BOOL) pointInMinimalStatusRect: (NSPoint) point;

- (BOOL) pointInPauseRect: (NSPoint) point;
- (BOOL) pointInRevealRect: (NSPoint) point;
- (BOOL) pointInActionRect: (NSPoint) point;

- (void) updateFileMenu: (NSMenu *) menu forFiles: (NSArray *) files;

@end

@implementation TorrentTableView

- (id) initWithCoder: (NSCoder *) decoder
{
    if ((self = [super initWithCoder: decoder]))
    {
        fClickPoint = NSZeroPoint;
        fClickIn = NO;
        
        fDefaults = [NSUserDefaults standardUserDefaults];
        
        [self setDelegate: self];
    }
    
    return self;
}

- (void) dealloc
{
    [fKeyStrokes release];
    [fMenuTorrent release];
    
    [super dealloc];
}

- (void) setTorrents: (NSArray *) torrents
{
    fTorrents = torrents;
}

- (void) tableView: (NSTableView *) tableView willDisplayCell: (id) cell forTableColumn: (NSTableColumn *) tableColumn row: (int) row
{
    [cell setRepresentedObject: [fTorrents objectAtIndex: row]];
}

- (NSString *) tableView: (NSTableView *) tableView typeSelectStringForTableColumn: (NSTableColumn *) tableColumn row: (int) row
{
    return [[fTorrents objectAtIndex: row] name];
}

- (void) mouseDown: (NSEvent *) event
{
    fClickPoint = [self convertPoint: [event locationInWindow] fromView: nil];

    if ([self pointInActionRect: fClickPoint])
    {
        int row = [self rowAtPoint: fClickPoint];
        [self setNeedsDisplayInRect: [self rectOfRow: row]]; //ensure button is pushed down
        
        [self displayTorrentMenuForEvent: event];
        
        fClickPoint = NSZeroPoint;
        [self setNeedsDisplayInRect: [self rectOfRow: row]];
    }
    else if ([self pointInPauseRect: fClickPoint] || [self pointInRevealRect: fClickPoint])
    {
        fClickIn = YES;
        [self setNeedsDisplayInRect: [self rectOfRow: [self rowAtPoint: fClickPoint]]];
    }
    else
    {
        if ([self pointInMinimalStatusRect: fClickPoint])
        {
            [fDefaults setBool: ![fDefaults boolForKey: @"DisplaySmallStatusRegular"] forKey: @"DisplaySmallStatusRegular"];
            fClickPoint = NSZeroPoint;
            [self reloadData];
        }
        
        [super mouseDown: event];
        
        if ([event clickCount] == 2 && !NSEqualPoints(fClickPoint, NSZeroPoint))
        {
            if ([self pointInProgressRect: fClickPoint])
            {
                [fDefaults setBool: ![fDefaults boolForKey: @"DisplayStatusProgressSelected"] forKey: @"DisplayStatusProgressSelected"];
                [self reloadData];
            }
            else if ([self pointInIconRect: fClickPoint])
                [[fTorrents objectAtIndex: [self rowAtPoint: fClickPoint]] revealData];
            else if (![self pointInActionRect: fClickPoint])
                [fController showInfo: nil];
            else;
        }
        else;
    }
}

- (void) mouseUp: (NSEvent *) event
{
    int oldRow;
    if (fClickIn)
    {
        NSPoint point = [self convertPoint: [event locationInWindow] fromView: nil];
        int row = [self rowAtPoint: point];
        oldRow = [self rowAtPoint: fClickPoint];
        
        if (row == oldRow && [self pointInPauseRect: point] && [self pointInPauseRect: fClickPoint])
        {
            Torrent * torrent = [fTorrents objectAtIndex: row];
            
            if ([torrent isActive])
                [fController stopTorrents: [NSArray arrayWithObject: torrent]];
            else
            {
                if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask)
                    [fController resumeTorrentsNoWait: [NSArray arrayWithObject: torrent]];
                else if ([torrent waitingToStart])
                    [fController stopTorrents: [NSArray arrayWithObject: torrent]];
                else
                    [fController resumeTorrents: [NSArray arrayWithObject: torrent]];
            }
        }
        else if (row == oldRow && [self pointInRevealRect: point] && [self pointInRevealRect: fClickPoint])
            [[fTorrents objectAtIndex: row] revealData];
        else;
    }
    
    [super mouseUp: event];

    fClickPoint = NSZeroPoint;
    
    #warning need fClickIn?
    BOOL wasClickIn = fClickIn;
    fClickIn = NO;
    
    if (wasClickIn)
        [self setNeedsDisplayInRect: [self rectOfRow: oldRow]];
}

- (void) mouseDragged: (NSEvent *) event
{
    if (NSEqualPoints(fClickPoint, NSZeroPoint))
    {
        [super mouseDragged: event];
        return;
    }
    
    NSPoint point = [self convertPoint: [event locationInWindow] fromView: nil];
    int oldRow = [self rowAtPoint: fClickPoint];
    
    BOOL inRect;
    if ([self pointInRevealRect: fClickPoint])
        inRect = oldRow == [self rowAtPoint: point] && [self pointInRevealRect: point];
    else if ([self pointInPauseRect: fClickPoint])
        inRect = oldRow == [self rowAtPoint: point] && [self pointInPauseRect: point];
    else
    {
        [super mouseDragged: event];
        return;
    }
    
    if (inRect != fClickIn)
    {
        fClickIn = inRect;
        [self setNeedsDisplayInRect: [self rectOfRow: oldRow]];
    }
}

- (NSMenu *) menuForEvent: (NSEvent *) event
{
    int row = [self rowAtPoint: [self convertPoint: [event locationInWindow] fromView: nil]];
    
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

- (void) flagsChanged: (NSEvent *) event
{
    [self display];
    [super flagsChanged: event];
}

- (void) keyDown: (NSEvent *) event
{
    //this is handled by the delegate in Leopard
    if ([NSApp isOnLeopardOrBetter])
    {
        [super keyDown: event];
        return;
    }
    
    if (!fKeyStrokes)
        fKeyStrokes = [[NSMutableArray alloc] init];
    
    unichar newChar = [[event characters] characterAtIndex: 0];
    if (newChar == ' ' || [[NSCharacterSet alphanumericCharacterSet] characterIsMember: newChar]
        || [[NSCharacterSet symbolCharacterSet] characterIsMember: newChar]
        || [[NSCharacterSet punctuationCharacterSet] characterIsMember: newChar])
    {
        if ([fKeyStrokes count] > 0 && [event timestamp] - [[fKeyStrokes lastObject] timestamp] > 1.0)
            [fKeyStrokes removeAllObjects];
        [fKeyStrokes addObject: event];
    
        [self interpretKeyEvents: fKeyStrokes];
    }
    else
    {
        [fKeyStrokes removeAllObjects];
        [super keyDown: event];
    }
}

- (void) insertText: (NSString *) text
{
    //this is handled by the delegate in Leopard
    if ([NSApp isOnLeopardOrBetter])
    {
        [super insertText: text];
        return;
    }
    
    //sort torrents by name before finding closest match
    NSSortDescriptor * nameDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"name" ascending: YES
                                            selector: @selector(caseInsensitiveCompare:)] autorelease];
    NSArray * descriptors = [[NSArray alloc] initWithObjects: nameDescriptor, nil];

    NSArray * tempTorrents = [fTorrents sortedArrayUsingDescriptors: descriptors];
    [descriptors release];
    
    text = [text lowercaseString];
    
    //select torrent closest to text that isn't before text alphabetically
    NSEnumerator * enumerator = [tempTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        if ([[[torrent name] lowercaseString] hasPrefix: text])
        {
            int row = [fTorrents indexOfObject: torrent];
            [self selectRow: row byExtendingSelection: NO];
            [self scrollRowToVisible: row];
            return;
        }
}

#warning get rect to actually change
- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
    tableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row mouseLocation: (NSPoint) mousePoint
{
    if ([self pointInActionRect: mousePoint])
    {
        *rect = [self actionRectForRow: row];
        
        return NSLocalizedString(@"Shortcuts for changing transfer settings.", "Torrent Table -> tooltip");
    }
    else if ([self pointInPauseRect: mousePoint])
    {
        *rect = [self pauseRectForRow: row];
        
        Torrent * torrent = [fTorrents objectAtIndex: row];
        if ([torrent isActive])
            return NSLocalizedString(@"Pause the transfer.", "Torrent Table -> tooltip");
        else
        {
            if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask && [fDefaults boolForKey: @"Queue"])
                return NSLocalizedString(@"Resume the transfer right away.", "Torrent Table -> tooltip");
            else if ([torrent waitingToStart])
                return NSLocalizedString(@"Stop waiting to start.", "Torrent Table -> tooltip");
            else
                return NSLocalizedString(@"Resume the transfer.", "Torrent Table -> tooltip");
        }
    }
    else if ([self pointInRevealRect: mousePoint])
    {
        *rect = [self revealRectForRow: row];
        
        return NSLocalizedString(@"Reveal the data file in Finder.", "Torrent Table -> tooltip");
    }
    
    return nil;
}

- (void) displayTorrentMenuForEvent: (NSEvent *) event
{
    int row = [self rowAtPoint: [self convertPoint: [event locationInWindow] fromView: nil]];
    if (row < 0)
        return;
    
    //get and update file menu
    fMenuTorrent = [[fTorrents objectAtIndex: row] retain];
    NSMenu * fileMenu = [fMenuTorrent fileMenu];
    [self updateFileMenu: fileMenu forFiles: [fMenuTorrent fileList]];
    
    //add file menu items to action menu
    NSRange range = NSMakeRange(0, [fileMenu numberOfItems]);
    [fActionMenu appendItemsFromMenu: fileMenu atIndexes: [NSIndexSet indexSetWithIndexesInRange: range] atBottom: YES];
    
    //place menu below button
    NSRect rect = [self actionRectForRow: row];
    NSPoint location = rect.origin;
    location.y += rect.size.height + 5.0;
    location = [self convertPoint: location toView: nil];
    
    NSEvent * newEvent = [NSEvent mouseEventWithType: [event type] location: location
        modifierFlags: [event modifierFlags] timestamp: [event timestamp] windowNumber: [event windowNumber]
        context: [event context] eventNumber: [event eventNumber] clickCount: [event clickCount] pressure: [event pressure]];
    
    [NSMenu popUpContextMenu: fActionMenu withEvent: newEvent forView: self];
    
    //move file menu items back to the torrent's file menu
    range.location = [fActionMenu numberOfItems] - range.length;
    [fileMenu appendItemsFromMenu: fActionMenu atIndexes: [NSIndexSet indexSetWithIndexesInRange: range] atBottom: YES];
    
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
        if ([menu numberOfItems] == 4)
        {
            const int speedLimitActionValue[] = { 0, 5, 10, 20, 30, 40, 50, 75, 100, 150, 200, 250, 500, 750, -1 };
            
            int i;
            for (i = 0; speedLimitActionValue[i] != -1; i++)
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
        
        BOOL upload = menu == fUploadMenu;
        int mode = [fMenuTorrent speedMode: upload];
        
        item = [menu itemWithTag: ACTION_MENU_LIMIT_TAG];
        [item setState: mode == TR_SPEEDLIMIT_SINGLE ? NSOnState : NSOffState];
        [item setTitle: [NSString stringWithFormat: NSLocalizedString(@"Limit (%d KB/s)",
                    "torrent action menu -> upload/download limit"), [fMenuTorrent speedLimit: upload]]];
        
        item = [menu itemWithTag: ACTION_MENU_UNLIMITED_TAG];
        [item setState: mode == TR_SPEEDLIMIT_UNLIMITED ? NSOnState : NSOffState];
        
        item = [menu itemWithTag: ACTION_MENU_GLOBAL_TAG];
        [item setState: mode == TR_SPEEDLIMIT_GLOBAL ? NSOnState : NSOffState];
    }
    else if (menu == fRatioMenu)
    {
        NSMenuItem * item;
        if ([menu numberOfItems] == 4)
        {
            const float ratioLimitActionValue[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, -1.0 };
            
            int i;
            for (i = 0; ratioLimitActionValue[i] != -1.0; i++)
            {
                item = [[NSMenuItem alloc] initWithTitle: [NSString stringWithFormat: @"%.2f", ratioLimitActionValue[i]]
                        action: @selector(setQuickRatio:) keyEquivalent: @""];
                [item setTarget: self];
                [item setRepresentedObject: [NSNumber numberWithFloat: ratioLimitActionValue[i]]];
                [menu addItem: item];
                [item release];
            }
        }
        
        int mode = [fMenuTorrent ratioSetting];
        
        item = [menu itemWithTag: ACTION_MENU_LIMIT_TAG];
        [item setState: mode == NSOnState ? NSOnState : NSOffState];
        [item setTitle: [NSString stringWithFormat: NSLocalizedString(@"Stop at Ratio (%.2f)", "torrent action menu -> ratio stop"),
                            [fMenuTorrent ratioLimit]]];
        
        item = [menu itemWithTag: ACTION_MENU_UNLIMITED_TAG];
        [item setState: mode == NSOffState ? NSOnState : NSOffState];
        
        item = [menu itemWithTag: ACTION_MENU_GLOBAL_TAG];
        [item setState: mode == NSMixedState ? NSOnState : NSOffState];
    }
    else if ([menu supermenu]) //assume the menu is part of the file list
    {
        NSMenu * supermenu = [menu supermenu];
        [self updateFileMenu: menu forFiles: [[[supermenu itemAtIndex: [supermenu indexOfItemWithSubmenu: menu]]
                                                    representedObject] objectForKey: @"Children"]];
    }
    else;
}

- (void) setQuickLimitMode: (id) sender
{
    int mode;
    switch ([sender tag])
    {
        case ACTION_MENU_UNLIMITED_TAG:
            mode = TR_SPEEDLIMIT_UNLIMITED;
            break;
        case ACTION_MENU_LIMIT_TAG:
            mode = TR_SPEEDLIMIT_SINGLE;
            break;
        case ACTION_MENU_GLOBAL_TAG:
            mode = TR_SPEEDLIMIT_GLOBAL;
            break;
        default:
            return;
    }
    
    [fMenuTorrent setSpeedMode: mode upload: [sender menu] == fUploadMenu];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateOptions" object: nil];
}

- (void) setQuickLimit: (id) sender
{
    BOOL upload = [sender menu] == fUploadMenu;
    [fMenuTorrent setSpeedMode: TR_SPEEDLIMIT_SINGLE upload: upload];
    [fMenuTorrent setSpeedLimit: [[sender representedObject] intValue] upload: upload];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateOptions" object: nil];
}

- (void) setQuickRatioMode: (id) sender
{
    int mode;
    switch ([sender tag])
    {
        case ACTION_MENU_UNLIMITED_TAG:
            mode = NSOffState;
            break;
        case ACTION_MENU_LIMIT_TAG:
            mode = NSOnState;
            break;
        case ACTION_MENU_GLOBAL_TAG:
            mode = NSMixedState;
            break;
        default:
            return;
    }
    
    [fMenuTorrent setRatioSetting: mode];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateOptions" object: nil];
}

- (void) setQuickRatio: (id) sender
{
    [fMenuTorrent setRatioSetting: NSOnState];
    [fMenuTorrent setRatioLimit: [[sender representedObject] floatValue]];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateOptions" object: nil];
}

- (void) checkFile: (id) sender
{
    NSIndexSet * indexSet = [[sender representedObject] objectForKey: @"Indexes"];
    [fMenuTorrent setFileCheckState: [sender state] != NSOnState ? NSOnState : NSOffState forIndexes: indexSet];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateStats" object: nil];
}

- (void) drawRow: (int) row clipRect: (NSRect) rect
{
    [super drawRow: row clipRect: rect];
    
    Torrent * torrent = [fTorrents objectAtIndex: row];
    
    //pause/resume icon
    NSImage * pauseImage;
    NSRect pauseRect  = [self pauseRectForRow: row];
    BOOL inPauseRect = fClickIn && NSPointInRect(fClickPoint, pauseRect);
    if ([torrent isActive])
        pauseImage = inPauseRect ? [NSImage imageNamed: @"PauseOn.png"] : [NSImage imageNamed: @"PauseOff.png"];
    else
    {
        if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask)
            pauseImage = inPauseRect ? [NSImage imageNamed: @"ResumeNoWaitOn.png"] : [NSImage imageNamed: @"ResumeNoWaitOff.png"];
        else if ([torrent waitingToStart])
            pauseImage = inPauseRect ? [NSImage imageNamed: @"PauseOn.png"] : [NSImage imageNamed: @"PauseOff.png"];
        else
            pauseImage = inPauseRect ? [NSImage imageNamed: @"ResumeOn.png"] : [NSImage imageNamed: @"ResumeOff.png"];
    }
    
    [pauseImage compositeToPoint: NSMakePoint(pauseRect.origin.x, NSMaxY(pauseRect)) operation: NSCompositeSourceOver];
    
    //reveal icon
    NSRect revealRect = [self revealRectForRow: row];
    NSImage * revealImage = fClickIn && NSPointInRect(fClickPoint, revealRect) ? [NSImage imageNamed: @"RevealOn.png"]
                                                                                : [NSImage imageNamed: @"RevealOff.png"];
    [revealImage compositeToPoint: NSMakePoint(revealRect.origin.x, NSMaxY(revealRect)) operation: NSCompositeSourceOver];
    
    //action icon
    if (![fDefaults boolForKey: @"SmallView"])
    {
        NSRect actionRect = [self actionRectForRow: row];
        NSImage * actionImage = NSPointInRect(fClickPoint, actionRect) ? [NSImage imageNamed: @"ActionOn.png"]
                                                                        : [NSImage imageNamed: @"ActionOff.png"];
        [actionImage compositeToPoint: NSMakePoint(actionRect.origin.x, NSMaxY(actionRect)) operation: NSCompositeSourceOver];
    }
}

@end

@implementation TorrentTableView (Private)

- (NSRect) pauseRectForRow: (int) row
{
    if (row < 0)
        return NSZeroRect;
    
    NSRect cellRect = [self frameOfCellAtColumn: 0 row: row];
    
    float buttonToTop = [fDefaults boolForKey: @"SmallView"] ? BUTTON_TO_TOP_SMALL : BUTTON_TO_TOP_REGULAR;
    
    return NSMakeRect(NSMaxX(cellRect) - PADDING - BUTTON_WIDTH - PADDING - BUTTON_WIDTH,
                        cellRect.origin.y + buttonToTop, BUTTON_WIDTH, BUTTON_WIDTH);
}

- (NSRect) revealRectForRow: (int) row
{
    if (row < 0)
        return NSZeroRect;
    
    NSRect cellRect = [self frameOfCellAtColumn: 0 row: row];
    
    float buttonToTop = [fDefaults boolForKey: @"SmallView"] ? BUTTON_TO_TOP_SMALL : BUTTON_TO_TOP_REGULAR;
    
    return NSMakeRect(NSMaxX(cellRect) - PADDING - BUTTON_WIDTH,
                        cellRect.origin.y + buttonToTop, BUTTON_WIDTH, BUTTON_WIDTH);
}

- (NSRect) actionRectForRow: (int) row
{
    if (row < 0)
        return NSZeroRect;
    
    TorrentCell * cell = [[self tableColumnWithIdentifier: @"Torrent"] dataCell];
    NSRect cellRect = [self frameOfCellAtColumn: 0 row: row],
            iconRect = [cell iconRectForBounds: cellRect];
    
    if ([fDefaults boolForKey: @"SmallView"])
        return iconRect;
    else
        return NSMakeRect(iconRect.origin.x + (iconRect.size.width - ACTION_BUTTON_WIDTH) * 0.5,
                        cellRect.origin.y + ACTION_BUTTON_TO_TOP, ACTION_BUTTON_WIDTH, ACTION_BUTTON_HEIGHT);
}

- (BOOL) pointInIconRect: (NSPoint) point
{
    int row = [self rowAtPoint: point];
    if (row < 0)
        return NO;
    
    TorrentCell * cell = [[self tableColumnWithIdentifier: @"Torrent"] dataCell];
    return NSPointInRect(point, [cell iconRectForBounds: [self frameOfCellAtColumn: 0 row: row]]);
}

- (BOOL) pointInProgressRect: (NSPoint) point
{
    int row = [self rowAtPoint: point];
    if (row < 0 || [fDefaults boolForKey: @"SmallView"])
        return NO;
    
    TorrentCell * cell = [[self tableColumnWithIdentifier: @"Torrent"] dataCell];
    [cell setRepresentedObject: [fTorrents objectAtIndex: row]];
    return NSPointInRect(point, [cell progressRectForBounds: [self frameOfCellAtColumn: 0 row: row]]);
}

- (BOOL) pointInMinimalStatusRect: (NSPoint) point
{
    int row = [self rowAtPoint: point];
    if (row < 0 || ![fDefaults boolForKey: @"SmallView"])
        return NO;
    
    TorrentCell * cell = [[self tableColumnWithIdentifier: @"Torrent"] dataCell];
    [cell setRepresentedObject: [fTorrents objectAtIndex: row]];
    return NSPointInRect(point, [cell minimalStatusRectForBounds: [self frameOfCellAtColumn: 0 row: row]]);
}

- (BOOL) pointInPauseRect: (NSPoint) point
{
    int row = [self rowAtPoint: point];
    if (row < 0)
        return NO;
    
    return NSPointInRect(point, [self pauseRectForRow: row]);
}

- (BOOL) pointInRevealRect: (NSPoint) point
{
    int row = [self rowAtPoint: point];
    if (row < 0)
        return NO;
    
    return NSPointInRect(point, [self revealRectForRow: row]);
}

- (BOOL) pointInActionRect: (NSPoint) point
{
    int row = [self rowAtPoint: point];
    if (row < 0)
        return NO;
    
    return NSPointInRect(point, [self actionRectForRow: row]);
}

- (void) updateFileMenu: (NSMenu *) menu forFiles: (NSArray *) files
{
    BOOL create = [menu numberOfItems] <= 0;
    
    NSEnumerator * enumerator = [files objectEnumerator];
    NSDictionary * dict;
    NSMenuItem * item;
    while ((dict = [enumerator nextObject]))
    {
        NSString * name = [dict objectForKey: @"Name"];
        
        if (create)
        {
            item = [[NSMenuItem alloc] initWithTitle: name action: @selector(checkFile:) keyEquivalent: @""];
            
            NSImage * icon;
            if (![[dict objectForKey: @"IsFolder"] boolValue])
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
            
            [item setRepresentedObject: dict];
            
            [icon setScalesWhenResized: YES];
            [icon setSize: NSMakeSize(16.0, 16.0)];
            [item setImage: icon];
            
            [menu addItem: item];
            [item release];
        }
        else
            item = [menu itemWithTitle: name];
        
        NSIndexSet * indexSet = [dict objectForKey: @"Indexes"];
        [item setState: [fMenuTorrent checkForFiles: indexSet]];
        [item setEnabled: [fMenuTorrent canChangeDownloadCheckForFiles: indexSet]];
    }
}

@end
