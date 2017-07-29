/******************************************************************************
 * Copyright (c) 2007-2012 Transmission authors and contributors
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

#import "InfoWindowController.h"
#import "FileListNode.h"
#import "FileNameCell.h"
#import "FileOutlineView.h"
#import "FilePriorityCell.h"
#import "Torrent.h"

@implementation FileOutlineView

- (void) awakeFromNib
{
    FileNameCell * nameCell = [[FileNameCell alloc] init];
    [[self tableColumnWithIdentifier: @"Name"] setDataCell: nameCell];

    FilePriorityCell * priorityCell = [[FilePriorityCell alloc] init];
    [[self tableColumnWithIdentifier: @"Priority"] setDataCell: priorityCell];

    [self setAutoresizesOutlineColumn: NO];
    [self setIndentationPerLevel: 14.0];

    fMouseRow = -1;
}


- (void) mouseDown: (NSEvent *) event
{
    [[self window] makeKeyWindow];
    [super mouseDown: event];
}

- (NSMenu *) menuForEvent: (NSEvent *) event
{
    const NSInteger row = [self rowAtPoint: [self convertPoint: [event locationInWindow] fromView: nil]];

    if (row >= 0)
    {
        if (![self isRowSelected: row])
            [self selectRowIndexes: [NSIndexSet indexSetWithIndex: row] byExtendingSelection: NO];
    }
    else
        [self deselectAll: self];

    return [self menu];
}

- (NSRect) iconRectForRow: (int) row
{
    FileNameCell * cell = (FileNameCell *)[self preparedCellAtColumn: [self columnWithIdentifier: @"Name"] row: row];
    NSRect iconRect = [cell imageRectForBounds: [self rectOfRow: row]];

    iconRect.origin.x += [self indentationPerLevel] * (CGFloat)([self levelForRow: row] + 1);
    return iconRect;
}

- (void) updateTrackingAreas
{
    [super updateTrackingAreas];

    for (NSTrackingArea * area in [self trackingAreas])
    {
        if ([area owner] == self && [area userInfo][@"Row"])
            [self removeTrackingArea: area];
    }

    NSRange visibleRows = [self rowsInRect: [self visibleRect]];
    if (visibleRows.length == 0)
        return;

    NSPoint mouseLocation = [self convertPoint: [[self window] mouseLocationOutsideOfEventStream] fromView: nil];

    for (NSInteger row = visibleRows.location, col = [self columnWithIdentifier: @"Priority"]; (NSUInteger)row < NSMaxRange(visibleRows); row++)
    {
        FilePriorityCell * cell = (FilePriorityCell *)[self preparedCellAtColumn: col row: row];

        NSDictionary * userInfo = @{@"Row": @(row)};
        [cell addTrackingAreasForView: self inRect: [self frameOfCellAtColumn: col row: row] withUserInfo: userInfo
                mouseLocation: mouseLocation];
    }
}

- (NSInteger) hoveredRow
{
    return fMouseRow;
}

- (void) mouseEntered: (NSEvent *) event
{
    NSNumber * row;
    if ((row = ((NSDictionary *)[event userData])[@"Row"]))
    {
        fMouseRow = [row intValue];
        [self setNeedsDisplayInRect: [self frameOfCellAtColumn: [self columnWithIdentifier: @"Priority"] row: fMouseRow]];
    }
}

- (void) mouseExited: (NSEvent *) event
{
    NSNumber * row;
    if ((row = ((NSDictionary *)[event userData])[@"Row"]))
    {
        [self setNeedsDisplayInRect: [self frameOfCellAtColumn: [self columnWithIdentifier: @"Priority"] row: [row intValue]]];
        fMouseRow = -1;
    }
}

@end
