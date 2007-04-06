/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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

#import "TorrentCell.h"
#import "TorrentTableView.h"
#import "StringAdditions.h"

//also defined in Torrent.m
#define BAR_HEIGHT 12.0

@interface TorrentCell (Private)

- (void) placeBar: (NSImage *) barImage width: (float) width point: (NSPoint) point;
- (void) buildSimpleBar: (float) width point: (NSPoint) point;
- (void) buildAdvancedBar: (float) widthFloat point: (NSPoint) point;

@end

@implementation TorrentCell

- (id) init
{
    if ((self = [super init]))
    {
        fDefaults = [NSUserDefaults standardUserDefaults];
    
        NSSize startSize = NSMakeSize(100.0, BAR_HEIGHT);
        
        fProgressWhite = [NSImage imageNamed: @"ProgressBarWhite.png"];
        [fProgressWhite setScalesWhenResized: YES];
        
        fProgressBlue = [NSImage imageNamed: @"ProgressBarBlue.png"];
        [fProgressBlue setScalesWhenResized: YES];
        [fProgressBlue setSize: startSize];
        
        fProgressGray = [NSImage imageNamed: @"ProgressBarGray.png"];
        [fProgressGray setScalesWhenResized: YES];
        [fProgressGray setSize: startSize];
        
        fProgressGreen = [NSImage imageNamed: @"ProgressBarGreen.png"];
        [fProgressGreen setScalesWhenResized: YES];
        
        fProgressLightGreen = [NSImage imageNamed: @"ProgressBarLightGreen.png"];
        [fProgressLightGreen setScalesWhenResized: YES];
        
        fProgressAdvanced = [NSImage imageNamed: @"ProgressBarAdvanced.png"];
        [fProgressAdvanced setScalesWhenResized: YES];
        
        fProgressEndWhite = [NSImage imageNamed: @"ProgressBarEndWhite.png"];
        fProgressEndBlue = [NSImage imageNamed: @"ProgressBarEndBlue.png"];
        fProgressEndGray = [NSImage imageNamed: @"ProgressBarEndGray.png"];
        fProgressEndGreen = [NSImage imageNamed: @"ProgressBarEndGreen.png"];
        fProgressEndAdvanced = [NSImage imageNamed: @"ProgressBarEndAdvanced.png"];
        
        fErrorImage = [[NSImage imageNamed: @"Error.tiff"] copy]; //no need to release for some reason
        [fErrorImage setFlipped: YES];
    }
    return self;
}

- (void) placeBar: (NSImage *) barImage width: (float) width point: (NSPoint) point
{
    if (width <= 0.0)
        return;
    
    if ([barImage size].width < width)
        [barImage setSize: NSMakeSize(width * 2.0, BAR_HEIGHT)];

    [barImage compositeToPoint: point fromRect: NSMakeRect(0, 0, width, BAR_HEIGHT) operation: NSCompositeSourceOver];
}

- (void) buildSimpleBar: (float) width point: (NSPoint) point
{
    NSDictionary * info = [self objectValue];

    width -= 2.0;
    float completedWidth, remainingWidth;
    
    //bar images and widths
    NSImage * barLeftEnd, * barRightEnd, * barComplete, * barRemaining;
    if ([[info objectForKey: @"Seeding"] boolValue])
    {
        float stopRatio, ratio;
        if ((stopRatio = [[info objectForKey: @"StopRatio"] floatValue]) != INVALID
                && (ratio = [[info objectForKey: @"Ratio"] floatValue]) < stopRatio)
		{
			if (ratio < 0)
				ratio = 0;
            completedWidth = width * ratio / stopRatio;
		}
        else
            completedWidth = width;
        remainingWidth = width - completedWidth;
        
        barLeftEnd = fProgressEndGreen;
        barRightEnd = fProgressEndGreen;
        barComplete = fProgressGreen;
        barRemaining = fProgressLightGreen;
    }
    else
    {
        completedWidth = [[info objectForKey: @"Progress"] floatValue] * width;
        remainingWidth = width - completedWidth;
        
		BOOL isActive = [[info objectForKey: @"Active"] boolValue];
		
        if (remainingWidth == width)
            barLeftEnd = fProgressEndWhite;
        else if (isActive)
            barLeftEnd = fProgressEndBlue;
        else
            barLeftEnd = fProgressEndGray;
        
        if (completedWidth < width)
            barRightEnd = fProgressEndWhite;
        else if (isActive)
            barRightEnd = fProgressEndBlue;
        else
            barRightEnd = fProgressEndGray;
        
        barComplete = isActive ? fProgressBlue : fProgressGray;
        barRemaining = fProgressWhite;
    }
    
    //place bar
    [barLeftEnd compositeToPoint: point operation: NSCompositeSourceOver];
    
    point.x += 1.0;
    [self placeBar: barComplete width: completedWidth point: point];
    
    point.x += completedWidth;
    [self placeBar: barRemaining width: remainingWidth point: point];
    
    point.x += remainingWidth;
    [barRightEnd compositeToPoint: point operation: NSCompositeSourceOver];
}

- (void) buildAdvancedBar: (float) widthFloat point: (NSPoint) point
{
    NSDictionary * info = [self objectValue];
    
    //draw overlay over advanced bar
    [fProgressEndAdvanced compositeToPoint: point operation: NSCompositeSourceOver];
    
    widthFloat -= 2.0;
    point.x += 1.0;
    
    //place actual advanced bar
    NSImage * image = [info objectForKey: @"AdvancedBar"];
    [image setSize: NSMakeSize(widthFloat, BAR_HEIGHT)];
    [image compositeToPoint: point operation: NSCompositeSourceOver];
    
    [self placeBar: fProgressAdvanced width: widthFloat point: point];
    
    point.x += widthFloat;
    [fProgressEndAdvanced compositeToPoint: point operation: NSCompositeSourceOver];
}

- (void) toggleMinimalStatus
{
    [fDefaults setBool: ![fDefaults boolForKey: @"SmallStatusRegular"] forKey: @"SmallStatusRegular"];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) view
{
    BOOL highlighted = [self isHighlighted] && [[self highlightColorWithFrame: cellFrame inView: view]
                                                        isEqual: [NSColor alternateSelectedControlColor]];
    
    NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
    [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
    
    NSDictionary * nameAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor controlTextColor], NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                    paragraphStyle, NSParagraphStyleAttributeName, nil];
    NSDictionary * statusAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor darkGrayColor], NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                    paragraphStyle, NSParagraphStyleAttributeName, nil];
    [paragraphStyle release];

    NSPoint pen = cellFrame.origin;
    const float PADDING = 3.0, LINE_PADDING = 2.0, EXTRA_NAME_SHIFT = 1.0;
    
    NSDictionary * info = [self objectValue];
    
    if (![fDefaults boolForKey: @"SmallView"]) //regular size
    {
        //icon
        NSImage * icon = [info objectForKey: @"Icon"];
        NSSize iconSize = [icon size];
        
        pen.x += PADDING;
        pen.y += (cellFrame.size.height - iconSize.height) * 0.5;
        
        [icon drawAtPoint: pen fromRect: NSMakeRect(0, 0, iconSize.width, iconSize.height)
                operation: NSCompositeSourceOver fraction: 1.0];
        
        //error badge
        if ([[info objectForKey: @"Error"] boolValue])
        {
            NSSize errorIconSize = [fErrorImage size];
            [fErrorImage drawAtPoint: NSMakePoint(pen.x + iconSize.width - errorIconSize.width,
                                                    pen.y + iconSize.height  - errorIconSize.height)
                fromRect: NSMakeRect(0, 0, errorIconSize.width, errorIconSize.height)
                operation: NSCompositeSourceOver fraction: 1.0];
        }

        float mainWidth = cellFrame.size.width - iconSize.width - 3.0 * PADDING - EXTRA_NAME_SHIFT;

        //name string
        pen.x += iconSize.width + PADDING + EXTRA_NAME_SHIFT;
        pen.y = cellFrame.origin.y + PADDING;
        
        NSString * nameString = [info objectForKey: @"Name"];
        NSSize nameSize = [nameString sizeWithAttributes: nameAttributes];
        [nameString drawInRect: NSMakeRect(pen.x, pen.y, mainWidth, nameSize.height) withAttributes: nameAttributes];
        
        //progress string
        pen.y += nameSize.height + LINE_PADDING - 1.0;
        
        NSString * progressString = [info objectForKey: @"ProgressString"];
        NSSize progressSize = [progressString sizeWithAttributes: statusAttributes];
        [progressString drawInRect: NSMakeRect(pen.x, pen.y, mainWidth, progressSize.height) withAttributes: statusAttributes];

        //progress bar
        pen.x -= EXTRA_NAME_SHIFT;
        pen.y += progressSize.height + LINE_PADDING + BAR_HEIGHT;
        
        float barWidth = mainWidth + EXTRA_NAME_SHIFT - BUTTONS_TOTAL_WIDTH + PADDING;
        
        if ([fDefaults boolForKey: @"UseAdvancedBar"])
            [self buildAdvancedBar: barWidth point: pen];
        else
            [self buildSimpleBar: barWidth point: pen];

        //status string
        pen.x += EXTRA_NAME_SHIFT;
        pen.y += LINE_PADDING;
        
        NSString * statusString = [info objectForKey: @"StatusString"];
        NSSize statusSize = [statusString sizeWithAttributes: statusAttributes];
        [statusString drawInRect: NSMakeRect(pen.x, pen.y, mainWidth, statusSize.height) withAttributes: statusAttributes];
    }
    else //small size
    {
        //icon
        NSImage * icon = ![[info objectForKey: @"Error"] boolValue] ? [info objectForKey: @"Icon"] : fErrorImage;
        NSSize iconSize = [icon size];
        
        pen.x += PADDING;
        pen.y += (cellFrame.size.height - iconSize.height) * 0.5;
        
        [icon drawAtPoint: pen fromRect: NSMakeRect(0, 0, iconSize.width, iconSize.height)
                operation: NSCompositeSourceOver fraction: 1.0];

        //name and status string
        float mainWidth = cellFrame.size.width - iconSize.width - 3.0 * PADDING - EXTRA_NAME_SHIFT;
                     
        //place name string
        pen.x += iconSize.width + PADDING + EXTRA_NAME_SHIFT;
        pen.y = cellFrame.origin.y + LINE_PADDING;

        NSString * nameString = [info objectForKey: @"Name"];
        NSSize nameSize = [nameString sizeWithAttributes: nameAttributes];
        
        NSString * statusString = ![fDefaults boolForKey: @"SmallStatusRegular"] && [[info objectForKey: @"Active"] boolValue]
                                        ? [info objectForKey: @"RemainingTimeString"]
                                        : [info objectForKey: @"ShortStatusString"];
        NSSize statusSize = [statusString sizeWithAttributes: statusAttributes];
        
        [nameString drawInRect: NSMakeRect(pen.x, pen.y, mainWidth - statusSize.width - 2.0 * LINE_PADDING, nameSize.height)
                        withAttributes: nameAttributes];
        
        //place status string
        pen.x = NSMaxX(cellFrame) - PADDING - statusSize.width;
        pen.y += (nameSize.height - statusSize.height) * 0.5;
        
        [statusString drawInRect: NSMakeRect(pen.x, pen.y, statusSize.width, statusSize.height)
                        withAttributes: statusAttributes];
        
        //progress bar
        pen.x = cellFrame.origin.x + iconSize.width + 2.0 * PADDING;
        pen.y = cellFrame.origin.y + nameSize.height + LINE_PADDING + PADDING + BAR_HEIGHT;
        
        float barWidth = mainWidth + EXTRA_NAME_SHIFT - BUTTONS_TOTAL_WIDTH + PADDING;
        
        if ([fDefaults boolForKey: @"UseAdvancedBar"])
            [self buildAdvancedBar: barWidth point: pen];
        else
            [self buildSimpleBar: barWidth point: pen];
    }
    
    [nameAttributes release];
    [statusAttributes release];
}

@end
