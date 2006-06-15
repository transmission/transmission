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
#import "Controller.h"
#import "Torrent.h"

#define BUTTON_WIDTH 14.0
#define BUTTON_TO_TOP 33.5
#define DISTANCE_FROM_CENTER 2.5
//change BUTTONS_TOTAL_WIDTH when changing this
#define AREA_CENTER 21.0

@interface TorrentTableView (Private)

- (NSRect) pauseRectForRow: (int) row;
- (NSRect) revealRectForRow: (int) row;
- (BOOL) pointInPauseRect: (NSPoint) point;
- (BOOL) pointInRevealRect: (NSPoint) point;
- (BOOL) pointInIconRect: (NSPoint) point;

@end

@implementation TorrentTableView

- (void) awakeFromNib
{
    [fContextRow setTitle: @"Context"];
    [fContextNoRow setTitle: @"Context"];
}

- (void) setTorrents: (NSArray *) torrents
{
    fTorrents = torrents;
}

- (void) mouseDown: (NSEvent *) e
{
    fClickPoint = [self convertPoint: [e locationInWindow] fromView: nil];
    int row = [self rowAtPoint: fClickPoint];

    if( [e modifierFlags] & NSAlternateKeyMask )
    {
        [fController advancedChanged: self];
        fClickPoint = NSMakePoint( 0, 0 );
    }
    else if( ![self pointInPauseRect: fClickPoint] &&
             ![self pointInRevealRect: fClickPoint] )
        [super mouseDown: e];
    else;

    [self display];
}

- (void) mouseUp: (NSEvent *) e
{
    NSPoint point;
    int row;
    bool sameRow;
    Torrent * torrent;

    point = [self convertPoint: [e locationInWindow] fromView: nil];
    row   = [self rowAtPoint: point];
    sameRow = row == [self rowAtPoint: fClickPoint];
    
    if( sameRow && [self pointInPauseRect: point]
            && [self pointInPauseRect: fClickPoint] )
    {
        torrent = [fTorrents objectAtIndex: row];

		if( [torrent isPaused] )
			[fController resumeTorrentWithIndex: [NSIndexSet indexSetWithIndex: row]];
		else if( [torrent isActive] )
			[fController stopTorrentWithIndex: [NSIndexSet indexSetWithIndex: row]];
		else;
    }
    else if( sameRow && [self pointInRevealRect: point]
                && [self pointInRevealRect: fClickPoint] )
        [[fTorrents objectAtIndex: row] reveal];
	else
        if ([e clickCount] == 2)
        {
            if ([self pointInIconRect: point])
                [[fTorrents objectAtIndex: row] reveal];
            else
                [fController showInfo: nil];
        }
    
	[super mouseUp: e];

    fClickPoint = NSMakePoint( 0, 0 );
    [self display];
}

- (NSMenu *) menuForEvent: (NSEvent *) e
{
    NSPoint point;
    int row;

    point = [self convertPoint: [e locationInWindow] fromView: nil];
    row = [self rowAtPoint: point];
    
    if( row >= 0 )
    {
        if (![self isRowSelected: row])
            [self selectRowIndexes: [NSIndexSet indexSetWithIndex: row]
                byExtendingSelection: NO];
                
        return fContextRow;
    }
    else
    {
        [self deselectAll: self];
        return fContextNoRow;
    }
}

- (void) drawRect: (NSRect) r
{
    unsigned i;
    NSRect rect;
    NSImage * image;
    Torrent * torrent;

    [super drawRect: r];

    for( i = 0; i < [fTorrents count]; i++ )
    {
        torrent = [fTorrents objectAtIndex: i];
        rect  = [self pauseRectForRow: i];
        image = nil;

        if( [torrent isPaused] )
        {
            image = NSPointInRect( fClickPoint, rect ) ?
                [NSImage imageNamed: @"ResumeOn.png"] :
                [NSImage imageNamed: @"ResumeOff.png"];
        }
        else if( [torrent isActive] )
        {
            image = NSPointInRect( fClickPoint, rect ) ?
                [NSImage imageNamed: @"PauseOn.png"] :
                [NSImage imageNamed: @"PauseOff.png"];
        }
        else;

        if( image )
        {
            [image setFlipped: YES];
            [image drawAtPoint: rect.origin fromRect:
                NSMakeRect(0, 0, BUTTON_WIDTH, BUTTON_WIDTH) operation:
                NSCompositeSourceOver fraction: 1.0];
        }

        rect  = [self revealRectForRow: i];
        image = NSPointInRect( fClickPoint, rect ) ?
            [NSImage imageNamed: @"RevealOn.png"] :
            [NSImage imageNamed: @"RevealOff.png"];
        [image setFlipped: YES];
        [image drawAtPoint: rect.origin fromRect:
            NSMakeRect(0, 0, BUTTON_WIDTH, BUTTON_WIDTH) operation:
            NSCompositeSourceOver fraction: 1.0];
    }
}

@end

@implementation TorrentTableView (Private)

- (NSRect) pauseRectForRow: (int) row
{
    NSRect cellRect = [self frameOfCellAtColumn:
                [self columnWithIdentifier: @"Torrent"] row: row];
                
    return NSMakeRect(cellRect.origin.x + cellRect.size.width
                        - AREA_CENTER - DISTANCE_FROM_CENTER - BUTTON_WIDTH,
                        cellRect.origin.y + BUTTON_TO_TOP, BUTTON_WIDTH, BUTTON_WIDTH);
}

- (NSRect) revealRectForRow: (int) row
{
    NSRect cellRect = [self frameOfCellAtColumn:
                [self columnWithIdentifier: @"Torrent"] row: row];
    
    return NSMakeRect(cellRect.origin.x + cellRect.size.width
                        - AREA_CENTER + DISTANCE_FROM_CENTER,
                        cellRect.origin.y + BUTTON_TO_TOP, BUTTON_WIDTH, BUTTON_WIDTH);
}

- (BOOL) pointInIconRect: (NSPoint) point
{
    int row = [self rowAtPoint: point];
    if (row < 0)
        return NO;
    
    NSRect cellRect = [self frameOfCellAtColumn:
                [self columnWithIdentifier: @"Torrent"] row: row];
    NSSize iconSize = [[[fTorrents objectAtIndex: row] iconFlipped] size];
    
    NSRect iconRect = NSMakeRect(cellRect.origin.x + 3.0, cellRect.origin.y
                        + (cellRect.size.height - iconSize.height) * 0.5,
                        iconSize.width, iconSize.height);
    
    return NSPointInRect(point, iconRect);
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
