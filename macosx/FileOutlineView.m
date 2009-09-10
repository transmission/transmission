/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2007-2009 Transmission authors and contributors
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
#import "FileOutlineView.h"
#import "FileNameCell.h"
#import "FilePriorityCell.h"
#import "Torrent.h"
#import "FileListNode.h"
#import <Quartz/Quartz.h>

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
    
    NSColor * endingColor = [NSColor colorWithCalibratedRed: 217.0/255.0 green: 250.0/255.0 blue: 211.0/255.0 alpha: 1.0];
    NSColor * beginningColor = [endingColor blendedColorWithFraction: 0.3 ofColor: [NSColor whiteColor]];
    fHighPriorityGradient = [[NSGradient alloc] initWithStartingColor: beginningColor endingColor: endingColor];
    
    endingColor = [NSColor colorWithCalibratedRed: 255.0/255.0 green: 243.0/255.0 blue: 206.0/255.0 alpha: 1.0];
    beginningColor = [endingColor blendedColorWithFraction: 0.3 ofColor: [NSColor whiteColor]];
    fLowPriorityGradient = [[NSGradient alloc] initWithStartingColor: beginningColor endingColor: endingColor];
    
    endingColor = [NSColor colorWithCalibratedRed: 225.0/255.0 green: 218.0/255.0 blue: 255.0/255.0 alpha: 1.0];
    beginningColor = [endingColor blendedColorWithFraction: 0.3 ofColor: [NSColor whiteColor]];
    fMixedPriorityGradient = [[NSGradient alloc] initWithStartingColor: beginningColor endingColor: endingColor];
    
    fMouseRow = -1;
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

- (void) keyDown: (NSEvent *) event
{
    const unichar firstChar = [[event charactersIgnoringModifiers] characterAtIndex: 0];
    
    //don't allow quick look on add window
    if ([NSApp isOnSnowLeopardOrBetter] && firstChar == ' ' && [[[self window] windowController] conformsToProtocol: @protocol(QLPreviewPanelDataSource)])
    {
        if ([[QLPreviewPanel sharedPreviewPanel] isVisible])
            [[QLPreviewPanel sharedPreviewPanel] orderOut: nil];
        else
            [[QLPreviewPanel sharedPreviewPanel] makeKeyAndOrderFront: nil];
    }
    
    [super keyDown: event];  
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

- (NSRect) iconRectForRow: (int) row
{
    FileNameCell * cell = (FileNameCell *)[self preparedCellAtColumn: [self columnWithIdentifier: @"Name"] row: row];
    NSRect iconRect = [cell imageRectForBounds: [self rectOfRow: row]];
    
    iconRect.origin.x += [self indentationPerLevel] * (float)([self levelForRow: row] + 1);
    return iconRect;
}

- (void) updateTrackingAreas
{
    [super updateTrackingAreas];
    
    for (NSTrackingArea * area in [self trackingAreas])
    {
        if ([area owner] == self && [[area userInfo] objectForKey: @"Row"])
            [self removeTrackingArea: area];
    }
    
    NSRange visibleRows = [self rowsInRect: [self visibleRect]];
    if (visibleRows.length == 0)
        return;
    
    NSPoint mouseLocation = [self convertPoint: [[self window] convertScreenToBase: [NSEvent mouseLocation]] fromView: nil];
    
    for (NSInteger row = visibleRows.location, col = [self columnWithIdentifier: @"Priority"]; row < NSMaxRange(visibleRows); row++)
    {
        FilePriorityCell * cell = (FilePriorityCell *)[self preparedCellAtColumn: col row: row];
        
        NSDictionary * userInfo = [NSDictionary dictionaryWithObject: [NSNumber numberWithInt: row] forKey: @"Row"];
        [cell addTrackingAreasForView: self inRect: [self frameOfCellAtColumn: col row: row] withUserInfo: userInfo
                mouseLocation: mouseLocation];
    }
}

- (int) hoveredRow
{
    return fMouseRow;
}

- (void) mouseEntered: (NSEvent *) event
{
    NSNumber * row;
    if ((row = [(NSDictionary *)[event userData] objectForKey: @"Row"]))
    {
        fMouseRow = [row intValue];
        [self setNeedsDisplayInRect: [self frameOfCellAtColumn: [self columnWithIdentifier: @"Priority"] row: fMouseRow]];
    }
}

- (void) mouseExited: (NSEvent *) event
{
    NSNumber * row;
    if ((row = [(NSDictionary *)[event userData] objectForKey: @"Row"]))
    {
        [self setNeedsDisplayInRect: [self frameOfCellAtColumn: [self columnWithIdentifier: @"Priority"] row: [row intValue]]];
        fMouseRow = -1;
    }
}

- (void) drawRow: (NSInteger) row clipRect: (NSRect) clipRect
{
    if (![self isRowSelected: row])
    {
        NSDictionary * item = [self itemAtRow: row]; 
        NSIndexSet * indexes = [(FileListNode *)item indexes];
        
        if ([fTorrent checkForFiles: indexes] != NSOffState)
        {
            NSGradient * gradient = nil;
            
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
                [gradient drawInRect: rect angle: 90];
            }
        }
    }
    
    [super drawRow: row clipRect: clipRect];
}

@end
