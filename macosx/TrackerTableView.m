/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2008-2009 Transmission authors and contributors
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

#import "TrackerTableView.h"
#import "NSApplicationAdditions.h"
#import "TrackerNode.h"

@implementation TrackerTableView

- (void) mouseDown: (NSEvent *) event
{
    [[self window] makeKeyWindow];
    [super mouseDown: event];
}

- (void) setTrackers: (NSArray *) trackers
{
    fTrackers = trackers;
}

- (IBAction) copy: (id) sender
{
    NSMutableArray * addresses = [NSMutableArray arrayWithCapacity: [fTrackers count]];
    NSIndexSet * indexes = [self selectedRowIndexes];
    for (NSUInteger i = [indexes firstIndex]; i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
    {
        id item = [fTrackers objectAtIndex: i];
        if ([item isKindOfClass: [NSNumber class]])
        {
            for (++i; i < [fTrackers count] && ![[fTrackers objectAtIndex: i] isKindOfClass: [NSNumber class]]; ++i)
                [addresses addObject: [(TrackerNode *)[fTrackers objectAtIndex: i] fullAnnounceAddress]];
            --i;
        }
        else
            [addresses addObject: [(TrackerNode *)item fullAnnounceAddress]];
    }
    
    NSString * text = [addresses componentsJoinedByString: @"\n"];
    
    NSPasteboard * pb = [NSPasteboard generalPasteboard];
    if ([NSApp isOnSnowLeopardOrBetter])
    {
        [pb clearContents];
        [pb writeObjects: [NSArray arrayWithObject: text]];
    }
    else
    {
        [pb declareTypes: [NSArray arrayWithObject: NSStringPboardType] owner: nil];
        [pb setString: text forType: NSStringPboardType];
    }
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    const SEL action = [menuItem action];
    
    if (action == @selector(copy:))
        return [self numberOfSelectedRows] > 0;
    
    return YES;
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
        if (![[fTrackers objectAtIndex: rows.location] isKindOfClass: [NSNumber class]])
        {
            for (NSInteger i = rows.location-1; i>=0; i--)
            {
                if ([[fTrackers objectAtIndex: i] isKindOfClass: [NSNumber class]])
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
            if ([[fTrackers objectAtIndex: i] isKindOfClass: [NSNumber class]])
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

@end
