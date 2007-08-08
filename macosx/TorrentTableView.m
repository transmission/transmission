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
        
        fKeyStrokes = [[NSMutableArray alloc] init];
        
        fSmallStatusAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                                        [NSFont messageFontOfSize: 9.0], NSFontAttributeName, nil];
        
        fDefaults = [NSUserDefaults standardUserDefaults];
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
    [super dealloc];
}

- (void) setTorrents: (NSArray *) torrents
{
    fTorrents = torrents;
}

- (void) mouseDown: (NSEvent *) event
{
    fClickPoint = [self convertPoint: [event locationInWindow] fromView: nil];

    if ([self pointInActionRect: fClickPoint])
    {
        #warning use icon that doesn't change?
        [self display]; //ensure button is pushed down
        [self displayTorrentMenuForEvent: event];
        #warning get icon to change back
    }
    else if (![self pointInPauseRect: fClickPoint] && ![self pointInRevealRect: fClickPoint])
    {
        if ([event modifierFlags] & NSAlternateKeyMask)
        {
            [fDefaults setBool: ![fDefaults boolForKey: @"UseAdvancedBar"] forKey: @"UseAdvancedBar"];
            fClickPoint = NSZeroPoint;
        }
        else
        {
            if ([self pointInMinimalStatusRect: fClickPoint])
            {
                [(TorrentCell *)[[self tableColumnWithIdentifier: @"Torrent"] dataCell] toggleMinimalStatus];
                fClickPoint = NSZeroPoint;
            }

            [super mouseDown: event];
        }
    }
    else;

    [self display];
}

- (void) mouseUp: (NSEvent *) event
{
    NSPoint point = [self convertPoint: [event locationInWindow] fromView: nil];
    int row = [self rowAtPoint: point];
    BOOL sameRow = row == [self rowAtPoint: fClickPoint];
    
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
        else
            [fController showInfo: nil];
    }
    else;
    
    [super mouseUp: event];

    fClickPoint = NSZeroPoint;
    [self display];
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
    
    NSMenu * torrentMenu = [[fTorrents objectAtIndex: row] torrentMenu];
    [torrentMenu setDelegate: self];
    
    NSRect rect = [self actionRectForRow: row];
    NSPoint location = rect.origin;
    location.y += rect.size.height + 5.0;
    location = [self convertPoint: location toView: nil];
    
    NSEvent * newEvent = [NSEvent mouseEventWithType: [event type] location: location
        modifierFlags: [event modifierFlags] timestamp: [event timestamp] windowNumber: [event windowNumber]
        context: [event context] eventNumber: [event eventNumber] clickCount: [event clickCount] pressure: [event pressure]];
    
    [NSMenu popUpContextMenu: torrentMenu withEvent: newEvent forView: self];
}

- (void) menuNeedsUpdate: (NSMenu *) menu
{
    BOOL create = [menu numberOfItems] <= 0, folder;
    
    Torrent * torrent = [fTorrents objectAtIndex: [self rowAtPoint: fClickPoint]];
    
    NSMenu * supermenu = [menu supermenu];
    NSArray * items;
    if (supermenu)
        items = [[[supermenu itemAtIndex: [supermenu indexOfItemWithSubmenu: menu]] representedObject] objectForKey: @"Children"];
    else
        items = [torrent fileList];
    NSEnumerator * enumerator = [items objectEnumerator];
    NSDictionary * dict;
    while ((dict = [enumerator nextObject]))
    {
        NSMenuItem * item;
        NSString * name = [dict objectForKey: @"Name"];
        
        folder = [[dict objectForKey: @"IsFolder"] boolValue];
        
        if (create)
        {
            item = [[NSMenuItem alloc] initWithTitle: name action: NULL keyEquivalent: @""];
            [menu addItem: item];
            [item release];
            
            NSImage * icon;
            if (!folder)
            {
                [item setAction: @selector(checkFile:)];
                
                icon = [[dict objectForKey: @"Icon"] copy];
                [icon setFlipped: NO];
            }
            else
            {
                NSMenu * menu = [[NSMenu alloc] initWithTitle: name];
                [menu setAutoenablesItems: NO];
                [item setSubmenu: menu];
                [menu setDelegate: self];
                
                icon = [[[NSWorkspace sharedWorkspace] iconForFileType: NSFileTypeForHFSTypeCode('fldr')] copy];
            }
            
            [item setRepresentedObject: dict];
            
            [icon setScalesWhenResized: YES];
            [icon setSize: NSMakeSize(16.0, 16.0)];
            [item setImage: icon];
            [icon release];
        }
        else
            item = [menu itemWithTitle: name];
        
        if (!folder)
        {
            NSIndexSet * indexSet = [dict objectForKey: @"Indexes"];
            [item setState: [torrent checkForFiles: indexSet]];
            [item setEnabled: [torrent canChangeDownloadCheckForFiles: indexSet]];
        }
    }
}

- (void) checkFile: (id) sender
{
    NSIndexSet * indexes = [[sender representedObject] objectForKey: @"Indexes"];
    Torrent * torrent = [fTorrents objectAtIndex: [self rowAtPoint: fClickPoint]];
    
    [torrent setFileCheckState: [sender state] != NSOnState ? NSOnState : NSOffState forIndexes: indexes];
    #warning reload inspector -> files
}

#warning only update shown
- (void) drawRect: (NSRect) r
{
    NSRect rect;
    Torrent * torrent;
    NSImage * image;

    [super drawRect: r];

    int i;
    for (i = 0; i < [fTorrents count]; i++)
    {
        torrent = [fTorrents objectAtIndex: i];
        rect  = [self pauseRectForRow: i];
        
        image = nil;
        if ([torrent isActive])
        {
            if (![torrent isChecking])
                image = NSPointInRect(fClickPoint, rect) ? fPauseOnIcon : fPauseOffIcon;
        }
        else if ([torrent isPaused])
        {
            if ([[NSApp currentEvent] modifierFlags] & NSAlternateKeyMask && [fDefaults boolForKey: @"Queue"])
                image = NSPointInRect(fClickPoint, rect) ? fResumeNoWaitOnIcon : fResumeNoWaitOffIcon;
            else if ([torrent waitingToStart])
                image = NSPointInRect(fClickPoint, rect) ? fPauseOnIcon : fPauseOffIcon;
            else
                image = NSPointInRect(fClickPoint, rect) ? fResumeOnIcon : fResumeOffIcon;
        }
        else;
        
        if (image)
            [image compositeToPoint: NSMakePoint(rect.origin.x, NSMaxY(rect)) operation: NSCompositeSourceOver];

        rect = [self revealRectForRow: i];
        image = NSPointInRect(fClickPoint, rect) ? fRevealOnIcon : fRevealOffIcon;
        [image compositeToPoint: NSMakePoint(rect.origin.x, NSMaxY(rect)) operation: NSCompositeSourceOver];
        
        #warning make change
        rect = [self actionRectForRow: i];
        [fActionOffIcon compositeToPoint: NSMakePoint(rect.origin.x, NSMaxY(rect)) operation: NSCompositeSourceOver];
    }
}

@end

@implementation TorrentTableView (Private)

- (NSRect) pauseRectForRow: (int) row
{
    NSRect cellRect = [self frameOfCellAtColumn: [self columnWithIdentifier: @"Torrent"] row: row];
    
    float buttonToTop = [fDefaults boolForKey: @"SmallView"] ? BUTTON_TO_TOP_SMALL : BUTTON_TO_TOP_REGULAR;
    
    return NSMakeRect(NSMaxX(cellRect) - PADDING - BUTTON_WIDTH - PADDING - BUTTON_WIDTH,
                        cellRect.origin.y + buttonToTop, BUTTON_WIDTH, BUTTON_WIDTH);
}

- (NSRect) revealRectForRow: (int) row
{
    NSRect cellRect = [self frameOfCellAtColumn: [self columnWithIdentifier: @"Torrent"] row: row];
    
    float buttonToTop = [fDefaults boolForKey: @"SmallView"] ? BUTTON_TO_TOP_SMALL : BUTTON_TO_TOP_REGULAR;
    
    return NSMakeRect(NSMaxX(cellRect) - PADDING - BUTTON_WIDTH,
                        cellRect.origin.y + buttonToTop, BUTTON_WIDTH, BUTTON_WIDTH);
}

- (NSRect) actionRectForRow: (int) row
{
    #warning return small icon rect
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSRect cellRect = [self frameOfCellAtColumn: [self columnWithIdentifier: @"Torrent"] row: row];
    
    #warning constant
    return NSMakeRect(cellRect.origin.x + PADDING +
                        ([[[fTorrents objectAtIndex: row] iconFlipped] size].width - ACTION_BUTTON_WIDTH) * 0.5,
                        cellRect.origin.y + ACTION_BUTTON_TO_TOP, ACTION_BUTTON_WIDTH, ACTION_BUTTON_HEIGHT);
}

- (BOOL) pointInIconRect: (NSPoint) point
{
    int row = [self rowAtPoint: point];
    if (row < 0)
        return NO;
    
    #warning move to own method
    NSRect cellRect = [self frameOfCellAtColumn: [self columnWithIdentifier: @"Torrent"] row: row];
    NSSize iconSize = [fDefaults boolForKey: @"SmallView"] ? [[[fTorrents objectAtIndex: row] iconSmall] size]
                                                        : [[[fTorrents objectAtIndex: row] iconFlipped] size];
    
    NSRect iconRect = NSMakeRect(cellRect.origin.x + 3.0, cellRect.origin.y + (cellRect.size.height - iconSize.height) * 0.5,
                            iconSize.width, iconSize.height);
    
    return NSPointInRect(point, iconRect);
}

- (BOOL) pointInMinimalStatusRect: (NSPoint) point
{
    int row = [self rowAtPoint: point];
    if (row < 0 || ![fDefaults boolForKey: @"SmallView"])
        return NO;

    Torrent * torrent = [fTorrents objectAtIndex: row];
    NSString * statusString = ![fDefaults boolForKey: @"SmallStatusRegular"] && [torrent isActive]
                                    ? [torrent remainingTimeString] : [torrent shortStatusString];
    
    float statusWidth = [statusString sizeWithAttributes: fSmallStatusAttributes].width + 3.0;
    
    NSRect cellRect = [self frameOfCellAtColumn: [self columnWithIdentifier: @"Torrent"] row: row];
    NSRect statusRect = NSMakeRect(NSMaxX(cellRect) - statusWidth, cellRect.origin.y,
                                    statusWidth, cellRect.size.height - BUTTON_WIDTH);
    
    return NSPointInRect(point, statusRect);
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
