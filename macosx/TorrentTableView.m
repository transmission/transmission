/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#define BUTTON_TO_TOP_REGULAR 33.5
#define BUTTON_TO_TOP_SMALL 20.0

#define BUTTON_WIDTH 14.0
#define DISTANCE_FROM_CENTER 2.5
//change BUTTONS_TOTAL_WIDTH when changing this
#define AREA_CENTER 21.0

@interface TorrentTableView (Private)

- (NSRect) pauseRectForRow: (int) row;
- (NSRect) revealRectForRow: (int) row;
- (BOOL) pointInPauseRect: (NSPoint) point;
- (BOOL) pointInRevealRect: (NSPoint) point;
- (BOOL) pointInIconRect: (NSPoint) point;
- (BOOL) pointInMinimalStatusRect: (NSPoint) point;

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
        fRevealOnIcon = [NSImage imageNamed: @"RevealOn.png"];
        fRevealOffIcon = [NSImage imageNamed: @"RevealOff.png"];
        
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

    if ([event modifierFlags] & NSAlternateKeyMask)
    {
        [fController toggleAdvancedBar: self];
        fClickPoint = NSZeroPoint;
    }
    else if (![self pointInPauseRect: fClickPoint] && ![self pointInRevealRect: fClickPoint])
    {
        if ([self pointInMinimalStatusRect: fClickPoint])
            [(TorrentCell *)[[self tableColumnWithIdentifier: @"Torrent"] dataCell] toggleMinimalStatus];

        [super mouseDown: event];
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

		if ([torrent isPaused])
			[fController resumeTorrents: [NSArray arrayWithObject: torrent]];
		else if ([torrent isActive])
            [fController stopTorrents: [NSArray arrayWithObject: torrent]];
		else;
    }
    else if (sameRow && [self pointInRevealRect: point] && [self pointInRevealRect: fClickPoint])
        [[fTorrents objectAtIndex: row] revealData];
	else if ([event clickCount] == 2)
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

- (void) insertText: (NSString *) text
{
    //sort torrents by name before finding closest match
    NSSortDescriptor * nameDescriptor = [[[NSSortDescriptor alloc] initWithKey: @"name" ascending: YES
                                            selector: @selector(caseInsensitiveCompare:)] autorelease];
    NSArray * descriptors = [[NSArray alloc] initWithObjects: nameDescriptor, nil];

    NSArray * tempTorrents = [fTorrents sortedArrayUsingDescriptors: descriptors];
    [descriptors release];
    
    //select torrent closest to text that isn't before text alphabetically
    NSEnumerator * enumerator = [tempTorrents objectEnumerator];
    Torrent * torrent;
    while ((torrent = [enumerator nextObject]))
        if ([[torrent name] caseInsensitiveCompare: text] != NSOrderedAscending)
        {
            [self selectRow: [fTorrents indexOfObject: torrent] byExtendingSelection: NO];
            break;
        }
    
    //select last torrent alphabetically if no match found
    if (!torrent)
        [self selectRow: [fTorrents indexOfObject: [tempTorrents lastObject]] byExtendingSelection: NO];
}

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

        if ([torrent isPaused])
            image = NSPointInRect(fClickPoint, rect) ? fResumeOnIcon : fResumeOffIcon;
        else if ([torrent isActive])
            image = NSPointInRect(fClickPoint, rect) ? fPauseOnIcon : fPauseOffIcon;
        else
            image = nil;

        if (image)
        {
            [image setFlipped: YES];
            [image drawAtPoint: rect.origin fromRect: NSMakeRect(0, 0, BUTTON_WIDTH, BUTTON_WIDTH)
                operation: NSCompositeSourceOver fraction: 1.0];
        }

        rect = [self revealRectForRow: i];
        image = NSPointInRect(fClickPoint, rect) ? fRevealOnIcon : fRevealOffIcon;
        [image setFlipped: YES];
        [image drawAtPoint: rect.origin fromRect: NSMakeRect(0, 0, BUTTON_WIDTH, BUTTON_WIDTH)
            operation: NSCompositeSourceOver fraction: 1.0];
    }
}

@end

@implementation TorrentTableView (Private)

- (NSRect) pauseRectForRow: (int) row
{
    NSRect cellRect = [self frameOfCellAtColumn: [self columnWithIdentifier: @"Torrent"] row: row];
    
    float buttonToTop = [fDefaults boolForKey: @"SmallView"] ? BUTTON_TO_TOP_SMALL : BUTTON_TO_TOP_REGULAR;
    
    return NSMakeRect(NSMaxX(cellRect) - AREA_CENTER - DISTANCE_FROM_CENTER - BUTTON_WIDTH,
                        cellRect.origin.y + buttonToTop, BUTTON_WIDTH, BUTTON_WIDTH);
}

- (NSRect) revealRectForRow: (int) row
{
    NSRect cellRect = [self frameOfCellAtColumn: [self columnWithIdentifier: @"Torrent"] row: row];
    
    float buttonToTop = [fDefaults boolForKey: @"SmallView"] ? BUTTON_TO_TOP_SMALL : BUTTON_TO_TOP_REGULAR;
    
    return NSMakeRect(NSMaxX(cellRect) - AREA_CENTER + DISTANCE_FROM_CENTER,
                        cellRect.origin.y + buttonToTop, BUTTON_WIDTH, BUTTON_WIDTH);
}

- (BOOL) pointInIconRect: (NSPoint) point
{
    int row = [self rowAtPoint: point];
    if (row < 0)
        return NO;

    NSRect cellRect = [self frameOfCellAtColumn: [self columnWithIdentifier: @"Torrent"] row: row];
    NSSize iconSize = [fDefaults boolForKey: @"SmallView"] ? [[[fTorrents objectAtIndex: row] iconSmall] size]
                                                        : [[[fTorrents objectAtIndex: row] iconFlipped] size];
    
    NSRect iconRect = NSMakeRect(cellRect.origin.x + 3.0, cellRect.origin.y
            + (cellRect.size.height - iconSize.height) * 0.5, iconSize.width, iconSize.height);
    
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
    return NSPointInRect(point, [self pauseRectForRow: [self rowAtPoint: point]]);
}

- (BOOL) pointInRevealRect: (NSPoint) point
{
    return NSPointInRect(point, [self revealRectForRow: [self rowAtPoint: point]]);
}

@end
