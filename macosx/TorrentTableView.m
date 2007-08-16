/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

#define PADDING 3.0

#define BUTTON_TO_TOP_REGULAR 33.0
#define BUTTON_TO_TOP_SMALL 20.0

#define ACTION_BUTTON_TO_TOP 47.0

@interface TorrentTableView (Private)

- (NSRect) pauseRectForRow: (int) row;
- (NSRect) revealRectForRow: (int) row;
- (NSRect) actionRectForRow: (int) row;

- (BOOL) pointInIconRect: (NSPoint) point;
- (BOOL) pointInMinimalStatusRect: (NSPoint) point;

- (BOOL) pointInPauseRect: (NSPoint) point;
- (BOOL) pointInRevealRect: (NSPoint) point;
- (BOOL) pointInActionRect: (NSPoint) point;

@end

@implementation TorrentTableView

- (id) initWithCoder: (NSCoder *) decoder
{
    if ((self = [super initWithCoder: decoder]))
    {
        fResumeOnIcon = [NSImage imageNamed: @"ResumeOn.png"];
        fResumeOffIcon = [NSImage imageNamed: @"ResumeOff.png"];
        fPauseOnIcon = [NSImage imageNamed: @"PauseOn.png"];
        fPauseOffIcon = [NSImage imageNamed: @"PauseOff.png"];
        fResumeNoWaitOnIcon = [NSImage imageNamed: @"ResumeNoWaitOn.png"];
        fResumeNoWaitOffIcon = [NSImage imageNamed: @"ResumeNoWaitOff.png"];
        
        fRevealOnIcon = [NSImage imageNamed: @"RevealOn.png"];
        fRevealOffIcon = [NSImage imageNamed: @"RevealOff.png"];
        
        fActionOnIcon = [NSImage imageNamed: @"ActionOn.png"];
        fActionOffIcon = [NSImage imageNamed: @"ActionOff.png"];
        
        fClickPoint = NSZeroPoint;
        fClickIn = NO;
        
        fKeyStrokes = [[NSMutableArray alloc] init];
        
        fSmallStatusAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                                        [NSFont messageFontOfSize: 9.0], NSFontAttributeName, nil];
        
        fDefaults = [NSUserDefaults standardUserDefaults];
        
        [self setDelegate: self];
    }
    
    return self;
}
    
- (void) awakeFromNib
{
    [fContextRow setTitle: @"Context"];
    [fContextNoRow setTitle: @"Context"];
}

- (void) dealloc
{
    [fKeyStrokes release];
    [fSmallStatusAttributes release];
    
    [fMenuTorrent release];
    
    [super dealloc];
}

- (void) setTorrents: (NSArray *) torrents
{
    fTorrents = torrents;
}

- (void) tableView: (NSTableView *) tableView willDisplayCell: (id) cell
        forTableColumn: (NSTableColumn *) tableColumn row: (int) row
{
    [cell setRepresentedObject: [fTorrents objectAtIndex: row]];
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
        if ([event modifierFlags] & NSAlternateKeyMask)
        {
            [fDefaults setBool: ![fDefaults boolForKey: @"UseAdvancedBar"] forKey: @"UseAdvancedBar"];
            fClickPoint = NSZeroPoint;
            [self reloadData];
        }
        else
        {
            if ([self pointInMinimalStatusRect: fClickPoint])
            {
                [fDefaults setBool: ![fDefaults boolForKey: @"SmallStatusRegular"] forKey: @"SmallStatusRegular"];
                fClickPoint = NSZeroPoint;
                [self reloadData];
            }

            [super mouseDown: event];
        }
    }
}

- (void) mouseUp: (NSEvent *) event
{
    NSPoint point = [self convertPoint: [event locationInWindow] fromView: nil];
    int row = [self rowAtPoint: point], oldRow = [self rowAtPoint: fClickPoint];
    BOOL sameRow = row == oldRow;
    
    if (sameRow && [self pointInPauseRect: point] && [self pointInPauseRect: fClickPoint])
    {
        Torrent * torrent = [fTorrents objectAtIndex: row];

        if ([torrent isActive])
            [fController stopTorrents: [NSArray arrayWithObject: torrent]];
        else if ([torrent isPaused])
        {
            if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask)
                [fController resumeTorrentsNoWait: [NSArray arrayWithObject: torrent]];
            else if ([torrent waitingToStart])
                [fController stopTorrents: [NSArray arrayWithObject: torrent]];
            else
                [fController resumeTorrents: [NSArray arrayWithObject: torrent]];
        }
        else;
    }
    else if (sameRow && [self pointInRevealRect: point] && [self pointInRevealRect: fClickPoint])
        [[fTorrents objectAtIndex: row] revealData];
    else if ([event clickCount] == 2 && !NSEqualPoints(fClickPoint, NSZeroPoint))
    {
        if ([self pointInIconRect: point])
            [[fTorrents objectAtIndex: row] revealData];
        else if (![self pointInActionRect: point])
            [fController showInfo: nil];
        else;
    }
    else;
    
    [super mouseUp: event];

    fClickPoint = NSZeroPoint;
    fClickIn = NO;
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

- (void) keyDown: (NSEvent *) event
{
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
        if ([fKeyStrokes count] > 0)
            [fKeyStrokes removeAllObjects];
        
        [super keyDown: event];
    }
}

- (void) flagsChanged: (NSEvent *) event
{
    [self display];
    [super flagsChanged: event];
}

- (void) insertText: (NSString *) text
{
    //sort torrents by name before finding closest match
    NSSortDescriptor * nameDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"name" ascending: YES
                                            selector: @selector(caseInsensitiveCompare:)] autorelease];
    NSArray * descriptors = [[NSArray alloc] initWithObjects: nameDescriptor, nil];

    NSArray * tempTorrents = [fTorrents sortedArrayUsingDescriptors: descriptors];
    [descriptors release];
    
    //select torrent closest to text that isn't before text alphabetically
    int row;
    NSEnumerator * enumerator = [tempTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        if ([[torrent name] caseInsensitiveCompare: text] != NSOrderedAscending)
        {
            row = [fTorrents indexOfObject: torrent];
            break;
        }
    
    //select last torrent alphabetically if no match found
    if (!torrent)
        row = [fTorrents indexOfObject: [tempTorrents lastObject]];
    
    [self selectRow: row byExtendingSelection: NO];
    [self scrollRowToVisible: row];
}

- (void) displayTorrentMenuForEvent: (NSEvent *) event
{
    int row = [self rowAtPoint: [self convertPoint: [event locationInWindow] fromView: nil]];
    if (row < 0)
        return;
    
    fMenuTorrent = [[fTorrents objectAtIndex: row] retain];
    NSMenu * torrentMenu = [fMenuTorrent torrentMenu];
    [torrentMenu setDelegate: self];
    
    NSRect rect = [self actionRectForRow: row];
    NSPoint location = rect.origin;
    location.y += rect.size.height + 5.0;
    location = [self convertPoint: location toView: nil];
    
    NSEvent * newEvent = [NSEvent mouseEventWithType: [event type] location: location
        modifierFlags: [event modifierFlags] timestamp: [event timestamp] windowNumber: [event windowNumber]
        context: [event context] eventNumber: [event eventNumber] clickCount: [event clickCount] pressure: [event pressure]];
    
    [NSMenu popUpContextMenu: torrentMenu withEvent: newEvent forView: self];
    
    [fMenuTorrent release];
    fMenuTorrent = nil;
}

- (void) menuNeedsUpdate: (NSMenu *) menu
{
    //this method seems to be called when it shouldn't be
    if (!fMenuTorrent)
        return;
    
    BOOL create = [menu numberOfItems] <= 0, folder;
    
    NSMenu * supermenu = [menu supermenu];
    NSArray * items;
    if (supermenu)
        items = [[[supermenu itemAtIndex: [supermenu indexOfItemWithSubmenu: menu]] representedObject] objectForKey: @"Children"];
    else
        items = [fMenuTorrent fileList];
    NSEnumerator * enumerator = [items objectEnumerator];
    NSDictionary * dict;
    NSMenuItem * item;
    while ((dict = [enumerator nextObject]))
    {
        NSString * name = [dict objectForKey: @"Name"];
        
        folder = [[dict objectForKey: @"IsFolder"] boolValue];
        
        if (create)
        {
            item = [[NSMenuItem alloc] initWithTitle: name action: NULL keyEquivalent: @""];
            
            NSImage * icon;
            if (!folder)
            {
                [item setAction: @selector(checkFile:)];
                
                icon = [[dict objectForKey: @"Icon"] copy];
                [icon setFlipped: NO];
            }
            else
            {
                NSMenu * itemMenu = [[NSMenu alloc] initWithTitle: name];
                [itemMenu setAutoenablesItems: NO];
                [item setSubmenu: itemMenu];
                [itemMenu setDelegate: self];
                [itemMenu release];
                
                icon = [[[NSWorkspace sharedWorkspace] iconForFileType: NSFileTypeForHFSTypeCode('fldr')] copy];
            }
            
            [item setRepresentedObject: dict];
            
            [icon setScalesWhenResized: YES];
            [icon setSize: NSMakeSize(16.0, 16.0)];
            [item setImage: icon];
            [icon release];
            
            [menu addItem: item];
            [item release];
        }
        else
            item = [menu itemWithTitle: name];
        
        if (!folder)
        {
            NSIndexSet * indexSet = [dict objectForKey: @"Indexes"];
            [item setState: [fMenuTorrent checkForFiles: indexSet]];
            [item setEnabled: [fMenuTorrent canChangeDownloadCheckForFiles: indexSet]];
        }
    }
}

- (void) checkFile: (id) sender
{
    NSIndexSet * indexes = [[sender representedObject] objectForKey: @"Indexes"];
    [fMenuTorrent setFileCheckState: [sender state] != NSOnState ? NSOnState : NSOffState forIndexes: indexes];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateStats" object: nil];
}

- (void) drawRow: (int) row clipRect: (NSRect) rect
{
    [super drawRow: row clipRect: rect];
    
    Torrent * torrent = [fTorrents objectAtIndex: row];
    
    //pause/resume icon
    NSImage * pauseImage = nil;
    NSRect pauseRect  = [self pauseRectForRow: row];
    if ([torrent isActive])
    {
        if (![torrent isChecking])
            pauseImage = fClickIn && NSPointInRect(fClickPoint, pauseRect) ? fPauseOnIcon : fPauseOffIcon;
    }
    else if ([torrent isPaused])
    {
        BOOL inPauseRect = fClickIn && NSPointInRect(fClickPoint, pauseRect);
        if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask && [fDefaults boolForKey: @"Queue"])
            pauseImage = inPauseRect ? fResumeNoWaitOnIcon : fResumeNoWaitOffIcon;
        else if ([torrent waitingToStart])
            pauseImage = inPauseRect ? fPauseOnIcon : fPauseOffIcon;
        else
            pauseImage = inPauseRect ? fResumeOnIcon : fResumeOffIcon;
    }
    else;
    
    if (pauseImage)
        [pauseImage compositeToPoint: NSMakePoint(pauseRect.origin.x, NSMaxY(pauseRect)) operation: NSCompositeSourceOver];
    
    //reveal icon
    NSRect revealRect = [self revealRectForRow: row];
    NSImage * revealImage = fClickIn && NSPointInRect(fClickPoint, revealRect) ? fRevealOnIcon : fRevealOffIcon;
    [revealImage compositeToPoint: NSMakePoint(revealRect.origin.x, NSMaxY(revealRect)) operation: NSCompositeSourceOver];
    
    //action icon
    if (![fDefaults boolForKey: @"SmallView"])
    {
        NSRect actionRect = [self actionRectForRow: row];
        NSImage * actionImage = NSPointInRect(fClickPoint, actionRect) ? fActionOnIcon : fActionOffIcon;
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

@end
