/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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
//- (void) buildAdvancedBar: (float) widthFloat point: (NSPoint) point;

@end

@implementation TorrentCell

- (id) init
{
    if ((self = [super init]))
    {
        fDefaults = [NSUserDefaults standardUserDefaults];
        
        fStatusRegular = [fDefaults boolForKey: @"SmallStatusRegular"];
    
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
        
        fProgressAdvanced = [NSImage imageNamed: @"ProgressBarAdvanced.png"];
        [fProgressAdvanced setScalesWhenResized: YES];
        
        fProgressEndWhite = [NSImage imageNamed: @"ProgressBarEndWhite.png"];
        fProgressEndBlue = [NSImage imageNamed: @"ProgressBarEndBlue.png"];
        fProgressEndGray = [NSImage imageNamed: @"ProgressBarEndGray.png"];
        fProgressEndGreen = [NSImage imageNamed: @"ProgressBarEndGreen.png"];
        fProgressEndAdvanced = [NSImage imageNamed: @"ProgressBarEndAdvanced.png"];
        
        fErrorImage = [[NSImage imageNamed: @"Error.tiff"] copy];
        [fErrorImage setFlipped: YES];
    }
    return self;
}

- (void) dealloc
{
    #warning should work?
    //[fErrorImage release];
    [super dealloc];
}

- (void) placeBar: (NSImage *) barImage width: (float) width point: (NSPoint) point
{
    if ([barImage size].width < width)
        [barImage setSize: NSMakeSize(width * 2.0, BAR_HEIGHT)];

    [barImage compositeToPoint: point fromRect: NSMakeRect(0, 0, width, BAR_HEIGHT) operation: NSCompositeSourceOver];
}

- (void) buildSimpleBar: (float) width point: (NSPoint) point
{
    NSDictionary * info = [self objectValue];

    width -= 2.0;
    if ([[info objectForKey: @"Seeding"] boolValue])
    {
        [fProgressEndGreen compositeToPoint: point operation: NSCompositeSourceOver];
        
        point.x += 1.0;
        [self placeBar: fProgressGreen width: width point: point];
        
        point.x += width;
        [fProgressEndGreen compositeToPoint: point operation: NSCompositeSourceOver];
    }
    else
    {
        float completedWidth = [[info objectForKey: @"Progress"] floatValue] * width,
                remainingWidth = width - completedWidth;
        BOOL isActive = [[info objectForKey: @"Active"] boolValue];
        
        //left end
        NSImage * barLeftEnd;
        if (remainingWidth == width)
            barLeftEnd = fProgressEndWhite;
        else if (isActive)
            barLeftEnd = fProgressEndBlue;
        else
            barLeftEnd = fProgressEndGray;
        
        [barLeftEnd compositeToPoint: point operation: NSCompositeSourceOver];
        
        //active bar
        point.x += 1.0;
        [self placeBar: isActive ? fProgressBlue : fProgressGray width: completedWidth point: point];
        
        //remaining bar
        point.x += completedWidth;
        [self placeBar: fProgressWhite width: remainingWidth point: point];
        
        //right end
        NSImage * barRightEnd;
        if (completedWidth < width)
            barRightEnd = fProgressEndWhite;
        else if (isActive)
            barRightEnd = fProgressEndBlue;
        else
            barRightEnd = fProgressEndGray;
        
        point.x += remainingWidth;
        [barRightEnd compositeToPoint: point operation: NSCompositeSourceOver];
    }
}

- (void) buildAdvancedBar: (float) widthFloat point: (NSPoint) point
{
    NSDictionary * info = [self objectValue];
    
    //if seeding, there's no need for the advanced bar
    if ([[info objectForKey: @"Seeding"] boolValue])
    {
        [self buildSimpleBar: widthFloat point: point];
        return;
    }
    
    //draw overlay over advanced bar
    [fProgressEndAdvanced compositeToPoint: point operation: NSCompositeSourceOver];
    
    widthFloat -= 2.0;
    point.x += 1.0;
    
    //place actual advanced bar
    NSImage * img = [info objectForKey: @"AdvancedBar"];
    [img setSize: NSMakeSize(widthFloat, BAR_HEIGHT)];
    [img compositeToPoint: point operation: NSCompositeSourceOver];
    
    [self placeBar: fProgressAdvanced width: widthFloat point: point];
    
    point.x += widthFloat;
    [fProgressEndAdvanced compositeToPoint: point operation: NSCompositeSourceOver];
}

- (void) toggleMinimalStatus
{
    fStatusRegular = !fStatusRegular;
    [fDefaults setBool: fStatusRegular forKey: @"SmallStatusRegular"];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) view
{
    BOOL highlighted = [self isHighlighted] && [[self highlightColorWithFrame: cellFrame inView: view]
                                                        isEqual: [NSColor alternateSelectedControlColor]];
    NSDictionary * nameAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor controlTextColor], NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 12.0], NSFontAttributeName, nil];
    NSDictionary * statusAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor darkGrayColor], NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 9.0], NSFontAttributeName, nil];

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
        NSAttributedString * nameString = [[info objectForKey: @"Name"] attributedStringFittingInWidth: mainWidth
                                                attributes: nameAttributes];
        [nameString drawAtPoint: pen];
        
        //progress string
        pen.y += [nameString size].height + LINE_PADDING - 1.0;
        
        NSAttributedString * progressString = [[info objectForKey: @"ProgressString"]
            attributedStringFittingInWidth: mainWidth attributes: statusAttributes];
        [progressString drawAtPoint: pen];

        //progress bar
        pen.x -= EXTRA_NAME_SHIFT;
        pen.y += [progressString size].height + LINE_PADDING + BAR_HEIGHT;
        
        float barWidth = mainWidth + EXTRA_NAME_SHIFT - BUTTONS_TOTAL_WIDTH + PADDING;
        
        if ([fDefaults boolForKey: @"UseAdvancedBar"])
            [self buildAdvancedBar: barWidth point: pen];
        else
            [self buildSimpleBar: barWidth point: pen];

        //status string
        pen.x += EXTRA_NAME_SHIFT;
        pen.y += LINE_PADDING;
        NSAttributedString * statusString = [[info objectForKey: @"StatusString"]
            attributedStringFittingInWidth: mainWidth attributes: statusAttributes];
        [statusString drawAtPoint: pen];
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
        
        NSString * realStatusString = !fStatusRegular && [[info objectForKey: @"Active"] boolValue]
                                        ? [info objectForKey: @"RemainingTimeString"]
                                        : [info objectForKey: @"ShortStatusString"];
        
        NSAttributedString * statusString = [[[NSAttributedString alloc] initWithString: realStatusString
                                                    attributes: statusAttributes] autorelease];
        NSAttributedString * nameString = [[info objectForKey: @"Name"] attributedStringFittingInWidth:
                                mainWidth - [statusString size].width - LINE_PADDING attributes: nameAttributes];
                     
        //place name string
        pen.x += iconSize.width + PADDING + EXTRA_NAME_SHIFT;
        pen.y = cellFrame.origin.y + LINE_PADDING;

        [nameString drawAtPoint: pen];
        
        //place status string
        pen.x = NSMaxX(cellFrame) - PADDING - [statusString size].width;
        pen.y += ([nameString size].height - [statusString size].height) * 0.5;
        
        [statusString drawAtPoint: pen];
        
        //progress bar
        pen.x = cellFrame.origin.x + iconSize.width + 2.0 * PADDING;
        pen.y = cellFrame.origin.y + [nameString size].height + LINE_PADDING + PADDING + BAR_HEIGHT;
        
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
