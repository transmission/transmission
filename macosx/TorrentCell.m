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
#import "CTGradientAdditions.h"

//also defined in Torrent.m
#define BAR_HEIGHT 12.0

@interface TorrentCell (Private)

- (void) placeBar: (NSImage *) barImage width: (float) width point: (NSPoint) point;
- (void) buildSimpleBar: (float) width point: (NSPoint) point;
- (void) buildAdvancedBar: (float) widthFloat point: (NSPoint) point;

@end

@implementation TorrentCell

//only called one, so don't worry about release
- (id) init
{
    if ((self = [super init]))
	{
        fDefaults = [NSUserDefaults standardUserDefaults];
        
        fWhiteGradient = [[CTGradient progressWhiteGradient] retain];
        fGrayGradient = [[CTGradient progressGrayGradient] retain];
        fLightGrayGradient = [[CTGradient progressLightGrayGradient] retain];
        fBlueGradient = [[CTGradient progressBlueGradient] retain];
        fDarkBlueGradient = [[CTGradient progressDarkBlueGradient] retain];
        fGreenGradient = [[CTGradient progressGreenGradient] retain];
        fLightGreenGradient = [[CTGradient progressLightGreenGradient] retain];
        fDarkGreenGradient = [[CTGradient progressDarkGreenGradient] retain];
        fYellowGradient = [[CTGradient progressYellowGradient] retain];
        fTransparentGradient = [[CTGradient progressTransparentGradient] retain];
        
        NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
    
        nameAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                            [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                            paragraphStyle, NSParagraphStyleAttributeName, nil];
        statusAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                            [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                            paragraphStyle, NSParagraphStyleAttributeName, nil];
        [paragraphStyle release];

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
    
    NSRect barBounds, completeBounds;
    if([[self controlView] isFlipped])
        barBounds = NSMakeRect(point.x, point.y, width, BAR_HEIGHT);
    else
        barBounds = NSMakeRect(point.x, point.y - BAR_HEIGHT, width, BAR_HEIGHT);
    completeBounds = barBounds;
    
    float progress = [[info objectForKey: @"Progress"] floatValue];
    completeBounds.size.width = progress * width;
    
    float left = INVALID;
    if (progress < 1.0)
    {
        [fWhiteGradient fillRect: barBounds angle: -90];
        
        left = [[info objectForKey: @"Left"] floatValue];
        if ((progress + left) < 1.0)
        {
            NSRect blankBounds = barBounds;
            blankBounds.origin.x += width * (progress + left);
            blankBounds.size.width = width * ((1.0 - progress) - left);
            
            [fLightGrayGradient fillRect: blankBounds angle: -90];
        }
    }
    
    if ([[info objectForKey: @"Active"] boolValue])
    {
        if ([[info objectForKey: @"Checking"] boolValue])
            [fYellowGradient fillRect: completeBounds angle: -90];
        else if ([[info objectForKey: @"Seeding"] boolValue])
        {
            NSRect ratioBounds = completeBounds;
            ratioBounds.size.width *= [[info objectForKey: @"ProgressStopRatio"] floatValue];
            
            if (ratioBounds.size.width < completeBounds.size.width)
                [fLightGreenGradient fillRect: completeBounds angle: -90];
            [fGreenGradient fillRect: ratioBounds angle: -90]; 
        }
        else
            [fBlueGradient fillRect: completeBounds angle: -90];
    }
    else
    {
        if ([[info objectForKey: @"Waiting"] boolValue])
        {
            if (left == INVALID)
                left = [[info objectForKey: @"Left"] floatValue];
            
            if (left <= 0.0)
                [fDarkGreenGradient fillRect: completeBounds angle: -90];
            else
                [fDarkBlueGradient fillRect: completeBounds angle: -90];
        }
        else
            [fGrayGradient fillRect: completeBounds angle: -90];
    }
    
    [[NSColor colorWithDeviceWhite: 0.0 alpha: 0.2] set];
    [NSBezierPath strokeRect: NSInsetRect(barBounds, 0.5, 0.5)];
}

- (void) buildAdvancedBar: (float) width point: (NSPoint) point
{
    NSDictionary * info = [self objectValue];
    
    //place actual advanced bar
    NSImage * image = [info objectForKey: @"AdvancedBar"];
    [image setSize: NSMakeSize(width, BAR_HEIGHT)];
    [image compositeToPoint: point operation: NSCompositeSourceOver];
    
    NSRect barBounds;
    if ([[self controlView] isFlipped])
        barBounds = NSMakeRect(point.x, point.y, width, BAR_HEIGHT);
    else
        barBounds = NSMakeRect(point.x, point.y - BAR_HEIGHT, width, BAR_HEIGHT);
    [fTransparentGradient fillRect: barBounds angle: -90];
    [[NSColor colorWithDeviceWhite: 0.0 alpha: 0.2] set];
    [NSBezierPath strokeRect: NSInsetRect(barBounds, 0.5, 0.5)];
}

- (void) toggleMinimalStatus
{
    [fDefaults setBool: ![fDefaults boolForKey: @"SmallStatusRegular"] forKey: @"SmallStatusRegular"];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) view
{
    BOOL highlighted = [self isHighlighted] && [[self highlightColorWithFrame: cellFrame inView: view]
                                                        isEqual: [NSColor alternateSelectedControlColor]];
    
    [nameAttributes setObject: highlighted ? [NSColor whiteColor] : [NSColor controlTextColor]
                        forKey: NSForegroundColorAttributeName];
    [statusAttributes setObject: highlighted ? [NSColor whiteColor] : [NSColor darkGrayColor]
                        forKey: NSForegroundColorAttributeName];
    
    NSPoint pen = cellFrame.origin;
    const float LINE_PADDING = 2.0, EXTRA_NAME_SHIFT = 1.0; //standard padding is defined in TorrentCell.h
    
    NSDictionary * info = [self objectValue];
    
    if (![fDefaults boolForKey: @"SmallView"]) //regular size
    {
        //icon
        NSImage * icon = [info objectForKey: @"Icon"];
        NSSize iconSize = [icon size];
        
        pen.x += PADDING;
        pen.y += (cellFrame.size.height - (iconSize.height + ACTION_BUTTON_HEIGHT)) * 0.5;
        
        [icon drawAtPoint: pen fromRect: NSMakeRect(0, 0, iconSize.width, iconSize.height)
                operation: NSCompositeSourceOver fraction: 1.0];
        
        //error badge
        if ([[info objectForKey: @"Error"] boolValue])
        {
            if (!fErrorImage)
            {
                fErrorImage = [[NSImage imageNamed: @"Error.png"] copy];
                [fErrorImage setFlipped: YES];
            }
            
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
        NSImage * icon;
        if ([[info objectForKey: @"Error"] boolValue])
        {
            if (!fErrorImage)
            {
                fErrorImage = [[NSImage imageNamed: @"Error.png"] copy];
                [fErrorImage setFlipped: YES];
            }
            icon = fErrorImage;
        }
        else
            icon = [info objectForKey: @"Icon"];
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
}

@end
