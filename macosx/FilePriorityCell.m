/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2007-2008 Transmission authors and contributors
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

#import "FilePriorityCell.h"
#import "FileOutlineView.h"
#import "Torrent.h"

@implementation FilePriorityCell

- (id) init
{
    if ((self = [super init]))
    {
        [self setTrackingMode: NSSegmentSwitchTrackingSelectAny];
        [self setControlSize: NSMiniControlSize];
        [self setSegmentCount: 3];
        
        int i;
        for (i = 0; i < [self segmentCount]; i++)
        {
            [self setLabel: @"" forSegment: i];
            [self setWidth: 9.0 forSegment: i]; //9 is minimum size to get proper look
        }
        
        [self setImage: [NSImage imageNamed: @"PriorityControlLow.png"] forSegment: 0];
        [self setImage: [NSImage imageNamed: @"PriorityControlNormal.png"] forSegment: 1];
        [self setImage: [NSImage imageNamed: @"PriorityControlHigh.png"] forSegment: 2];
        
        fHoverRow = NO;
    }
    return self;
}

- (void) setSelected: (BOOL) flag forSegment: (int) segment
{
    [super setSelected: flag forSegment: segment];
    
    //only for when clicking manually
    int priority;
    switch (segment)
    {
        case 0:
            priority = TR_PRI_LOW;
            break;
        case 1:
            priority = TR_PRI_NORMAL;
            break;
        case 2:
            priority = TR_PRI_HIGH;
            break;
    }
    
    FileOutlineView * controlView = (FileOutlineView *)[self controlView];
    Torrent * torrent = [controlView torrent];
    [torrent setFilePriority: priority forIndexes: [[self representedObject] objectForKey: @"Indexes"]];
    [controlView reloadData];
}

- (void) addTrackingAreasForView: (NSView *) controlView inRect: (NSRect) cellFrame withUserInfo: (NSDictionary *) userInfo
            mouseLocation: (NSPoint) mouseLocation
{
    NSTrackingAreaOptions options = NSTrackingEnabledDuringMouseDrag | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways;
    
    if (NSMouseInRect(mouseLocation, cellFrame, [controlView isFlipped]))
    {
        options |= NSTrackingAssumeInside;
        [controlView setNeedsDisplayInRect: cellFrame];
    }
    
    NSTrackingArea * area = [[NSTrackingArea alloc] initWithRect: cellFrame options: options owner: controlView userInfo: userInfo];
    [controlView addTrackingArea: area];
    [area release];
}

- (void) mouseEntered: (NSEvent *) event
{
    fHoverRow = YES;
    [(NSControl *)[self controlView] updateCell: self];
}

- (void) mouseExited: (NSEvent *) event
{
    fHoverRow = NO;
    [(NSControl *)[self controlView] updateCell: self];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    Torrent * torrent = [(FileOutlineView *)controlView torrent];
    NSDictionary * dict = [self representedObject];
    NSSet * priorities = [torrent filePrioritiesForIndexes: [dict objectForKey: @"Indexes"]];
    
    int count = [priorities count];
    if (fHoverRow && count > 0)
    {
        [super setSelected: [priorities containsObject: [NSNumber numberWithInt: TR_PRI_LOW]] forSegment: 0];
        [super setSelected: [priorities containsObject: [NSNumber numberWithInt: TR_PRI_NORMAL]] forSegment: 1];
        [super setSelected: [priorities containsObject: [NSNumber numberWithInt: TR_PRI_HIGH]] forSegment: 2];
        
        [super drawWithFrame: cellFrame inView: controlView];
    }
    else
    {
        NSImage * image;
        if (count == 0)
            image = [NSImage imageNamed: @"PriorityNone.png"];
        else if (count > 1)
            image = [NSImage imageNamed: @"PriorityMixed.png"];
        else
        {
            switch ([[priorities anyObject] intValue])
            {
                case TR_PRI_NORMAL:
                    image = [NSImage imageNamed: @"PriorityNormal.png"];
                    break;
                case TR_PRI_LOW:
                    image = [NSImage imageNamed: @"PriorityLow.png"];
                    break;
                case TR_PRI_HIGH:
                    image = [NSImage imageNamed: @"PriorityHigh.png"];
                    break;
            }
        }
        
        NSSize imageSize = [image size];
        [image compositeToPoint: NSMakePoint(cellFrame.origin.x + (cellFrame.size.width - imageSize.width) * 0.5,
                cellFrame.origin.y + (cellFrame.size.height + imageSize.height) * 0.5) operation: NSCompositeSourceOver];
    }
}

@end
