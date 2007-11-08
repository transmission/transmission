/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2007 Transmission authors and contributors
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

#import "FileOutlineView.h"
#import "FileNameCell.h"
#import "FilePriorityCell.h"
#import "Torrent.h"
#import "CTGradient.h"

@implementation FileOutlineView

- (void) awakeFromNib
{
    FileNameCell * nameCell = [[FileNameCell alloc] init];
    [[self tableColumnWithIdentifier: @"Name"] setDataCell: nameCell];
    [nameCell release];
    
    FilePriorityCell * priorityCell = [[FilePriorityCell alloc] init];
    [[self tableColumnWithIdentifier: @"Priority"] setDataCell: priorityCell];
    [priorityCell release];
    
    [self setAutoresizesOutlineColumn: NO];
    [self setIndentationPerLevel: 14.0];
    
    NSColor * beginningColor = [NSColor colorWithCalibratedRed: 217.0/255.0 green: 250.0/255.0 blue: 211.0/255.0 alpha: 1.0];
    NSColor * endingColor = [NSColor colorWithCalibratedRed: 200.0/255.0 green: 231.0/255.0 blue: 194.0/255.0 alpha: 1.0];
    fHighPriorityGradient = [[CTGradient gradientWithBeginningColor: beginningColor endingColor: endingColor] retain];
    
    beginningColor = [NSColor colorWithCalibratedRed: 255.0/255.0 green: 243.0/255.0 blue: 206.0/255.0 alpha: 1.0];
    endingColor = [NSColor colorWithCalibratedRed: 235.0/255.0 green: 226.0/255.0 blue: 192.0/255.0 alpha: 1.0];
    fLowPriorityGradient = [[CTGradient gradientWithBeginningColor: beginningColor endingColor: endingColor] retain];
    
    beginningColor = [NSColor colorWithCalibratedRed: 225.0/255.0 green: 218.0/255.0 blue: 255.0/255.0 alpha: 1.0];
    endingColor = [NSColor colorWithCalibratedRed: 214.0/255.0 green: 207.0/255.0 blue: 242.0/255.0 alpha: 1.0];
    fMixedPriorityGradient = [[CTGradient gradientWithBeginningColor: beginningColor endingColor: endingColor] retain];
    
    fHoverRow = -1;
}

- (void) dealloc
{
    [fHighPriorityGradient release];
    [fLowPriorityGradient release];
    [fMixedPriorityGradient release];
    
    [super dealloc];
}

- (void) setTorrent: (Torrent *) torrent
{
    fTorrent = torrent;
}

- (Torrent *) torrent
{
    return fTorrent;
}

- (void) mouseDown: (NSEvent *) event
{
    [[self window] makeKeyWindow];
    [super mouseDown: event];
}

- (NSMenu *) menuForEvent: (NSEvent *) event
{
    int row = [self rowAtPoint: [self convertPoint: [event locationInWindow] fromView: nil]];
    
    if (row >= 0)
    {
        if (![self isRowSelected: row])
            [self selectRowIndexes: [NSIndexSet indexSetWithIndex: row] byExtendingSelection: NO];
    }
    else
        [self deselectAll: self];
    
    return [self menu];
}

- (void) setHoverRowForEvent: (NSEvent *) event
{
    int row = -1;
    if (event)
    {
        NSPoint point = [self convertPoint: [event locationInWindow] fromView: nil];
        if ([self columnAtPoint: point] == [self columnWithIdentifier: @"Priority"])
            row = [self rowAtPoint: point];
    }
    
    if (row != fHoverRow)
    {
        if (fHoverRow != -1)
            [self reloadItem: [self itemAtRow: fHoverRow]];
        fHoverRow = row;
        if (fHoverRow != -1)
            [self reloadItem: [self itemAtRow: fHoverRow]];
    }
}

- (int) hoverRow
{
    return fHoverRow;
}

- (void) drawRow: (int) row clipRect: (NSRect) clipRect
{
    if (![self isRowSelected: row])
    {
        NSDictionary * item = [self itemAtRow: row]; 
        NSIndexSet * indexes = [item objectForKey: @"Indexes"];
        
        if ([fTorrent checkForFiles: indexes] != NSOffState)
        {
            CTGradient * gradient = nil;
            
            NSSet * priorities = [fTorrent filePrioritiesForIndexes: indexes];
            int count = [priorities count];
            if (count == 1)
            {
                switch ([[priorities anyObject] intValue])
                {
                    case TR_PRI_LOW:
                        gradient = fLowPriorityGradient;
                        break;
                    case TR_PRI_HIGH:
                        gradient = fHighPriorityGradient;
                        break;
                }
            }
            else if (count > 1)
                gradient = fMixedPriorityGradient;
            else;
            
            if (gradient)
            {
                NSRect rect = [self rectOfRow: row];
                rect.size.height -= 1.0;
                [gradient fillRect: rect angle: 90];
            }
        }
    }
    
    [super drawRow: row clipRect: clipRect];
}

@end
