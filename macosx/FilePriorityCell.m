/******************************************************************************
 * $Id$
 * 
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

#import "FilePriorityCell.h"
#import "FileOutlineView.h"
#import "FileListNode.h"
#import "NSImageAdditions.h"
#import "Torrent.h"

#define IMAGE_OVERLAP 1.0

@implementation FilePriorityCell

- (id) init
{
    if ((self = [super init]))
    {
        [self setTrackingMode: NSSegmentSwitchTrackingSelectAny];
        [self setControlSize: NSMiniControlSize];
        [self setSegmentCount: 3];
        
        for (NSInteger i = 0; i < [self segmentCount]; i++)
        {
            [self setLabel: @"" forSegment: i];
            [self setWidth: 9.0f forSegment: i]; //9 is minimum size to get proper look
        }
        
        [self setImage: [NSImage imageNamed: @"PriorityControlLow"] forSegment: 0];
        [self setImage: [NSImage imageNamed: @"PriorityControlNormal"] forSegment: 1];
        [self setImage: [NSImage imageNamed: @"PriorityControlHigh"] forSegment: 2];
        
        fHoverRow = NO;
    }
    return self;
}

- (id) copyWithZone: (NSZone *) zone
{
    id value = [super copyWithZone: zone];
    [value setRepresentedObject: [self representedObject]];
    return value;
}

- (void) setSelected: (BOOL) flag forSegment: (NSInteger) segment
{
    [super setSelected: flag forSegment: segment];
    
    //only for when clicking manually
    NSInteger priority;
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
    
    Torrent * torrent = [(FileListNode *)[self representedObject] torrent];
    [torrent setFilePriority: priority forIndexes: [(FileListNode *)[self representedObject] indexes]];
    
    FileOutlineView * controlView = (FileOutlineView *)[self controlView];
    [controlView setNeedsDisplay: YES];
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

- (void) setHovered: (BOOL) hovered
{
    fHoverRow = hovered;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    FileListNode * node = [self representedObject];
    Torrent * torrent = [node torrent];
    NSSet * priorities = [torrent filePrioritiesForIndexes: [node indexes]];
    
    const NSUInteger count = [priorities count];
    if (fHoverRow && count > 0)
    {
        [super setSelected: [priorities containsObject: [NSNumber numberWithInteger: TR_PRI_LOW]] forSegment: 0];
        [super setSelected: [priorities containsObject: [NSNumber numberWithInteger: TR_PRI_NORMAL]] forSegment: 1];
        [super setSelected: [priorities containsObject: [NSNumber numberWithInteger: TR_PRI_HIGH]] forSegment: 2];
        
        [super drawWithFrame: cellFrame inView: controlView];
    }
    else
    {
        NSMutableArray * images = [NSMutableArray arrayWithCapacity: MAX(count, 1)];
        CGFloat totalWidth;
        
        if (count == 0)
        {
            //if ([self backgroundStyle] != NSBackgroundStyleDark)
            {
                NSImage * image = [[NSImage imageNamed: @"PriorityNormalTemplate"] imageWithColor: [NSColor lightGrayColor]];
                [images addObject: image];
                totalWidth = [image size].width;
            }
        }
        else
        {
            NSColor * priorityColor = [self backgroundStyle] == NSBackgroundStyleDark ? [NSColor whiteColor] : [NSColor darkGrayColor];
            
            totalWidth = 0.0;
            if ([priorities containsObject: [NSNumber numberWithInteger: TR_PRI_LOW]])
            {
                NSImage * image = [[NSImage imageNamed: @"PriorityLowTemplate"] imageWithColor: priorityColor];
                [images addObject: image];
                totalWidth += [image size].width;
            }
            if ([priorities containsObject: [NSNumber numberWithInteger: TR_PRI_NORMAL]])
            {
                NSImage * image = [[NSImage imageNamed: @"PriorityNormalTemplate"] imageWithColor: priorityColor];
                [images addObject: image];
                totalWidth += [image size].width;
            }
            if ([priorities containsObject: [NSNumber numberWithInteger: TR_PRI_HIGH]])
            {
                NSImage * image = [[NSImage imageNamed: @"PriorityHighTemplate"] imageWithColor: priorityColor];
                [images addObject: image];
                totalWidth += [image size].width;
            }
        }
        
        if (count > 1)
            totalWidth -= IMAGE_OVERLAP * (count-1);
        
        CGFloat currentWidth = floor(NSMidX(cellFrame) - totalWidth * 0.5);
        
        for (NSImage * image in images)
        {
            const NSSize imageSize = [image size];
            const NSRect imageRect = NSMakeRect(currentWidth, floor(NSMidY(cellFrame) - imageSize.height * 0.5), imageSize.width, imageSize.height);
            
            [image drawInRect: imageRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];
            
            currentWidth += imageSize.width - IMAGE_OVERLAP;
        }
    }
}

@end
