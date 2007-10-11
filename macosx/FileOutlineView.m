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
#import "InfoWindowController.h"
#import "Torrent.h"

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
    
    fHighPriorityColor = [[NSColor colorWithCalibratedRed: 0.8588 green: 0.9961 blue: 0.8311 alpha: 1.0] retain];
    fLowPriorityColor = [[NSColor colorWithCalibratedRed: 1.0 green: 0.9529 blue: 0.8078 alpha: 1.0] retain];
    fMixedPriorityColor = [[NSColor colorWithCalibratedRed: 0.9216 green: 0.9059 blue: 1.0 alpha: 1.0] retain];
    
    fHoverRow = -1;
}

- (void) dealloc
{
    [fHighPriorityColor release];
    [fLowPriorityColor release];
    [fMixedPriorityColor release];
    
    [super dealloc];
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
        Torrent * torrent = [(InfoWindowController *)[[self window] windowController] selectedTorrent];
        NSIndexSet * indexes = [item objectForKey: @"Indexes"];
        
        if ([torrent checkForFiles: indexes] != NSOffState)
        {
            NSSet * priorities = [torrent filePrioritiesForIndexes: indexes];
            int count = [priorities count];
            if (count > 0)
            {
                BOOL custom = YES;
                if (count > 1)
                    [fMixedPriorityColor set];
                else
                {
                    switch ([[priorities anyObject] intValue])
                    {
                        case TR_PRI_LOW:
                            [fLowPriorityColor set];
                            break;
                        case TR_PRI_HIGH:
                            [fHighPriorityColor set];
                            break;
                        default:
                            custom = NO;
                    }
                }
                
                if (custom)
                {
                    NSRect rect = [self rectOfRow: row];
                    rect.size.height -= 1.0;
            
                    NSRectFill(rect);
                }
            }
        }
    }
    
    [super drawRow: row clipRect: clipRect];
}

@end
