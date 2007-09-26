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
#import "NSStringAdditions.h"
#import "CTGradientAdditions.h"

#define BAR_HEIGHT 12.0

#define IMAGE_SIZE_REG 32.0
#define IMAGE_SIZE_MIN 16.0

//end up being larger than font height
#define HEIGHT_TITLE 16.0
#define HEIGHT_STATUS 12.0

#define PADDING_HORIZONAL 2.0
#define PADDING_ABOVE_IMAGE_REG 9.0
#define PADDING_BETWEEN_IMAGE_AND_TITLE 5.0
#define PADDING_BETWEEN_IMAGE_AND_BAR 7.0
#define PADDING_ABOVE_TITLE 3.0
#define PADDING_ABOVE_MIN_STATUS 4.0
#define PADDING_BETWEEN_TITLE_AND_MIN_STATUS 2.0
#define PADDING_BETWEEN_TITLE_AND_PROGRESS 1.0
#define PADDING_BETWEEN_PROGRESS_AND_BAR 2.0
#define PADDING_BETWEEN_TITLE_AND_BAR_MIN 3.0
#define PADDING_BETWEEN_BAR_AND_STATUS 2.0

#define MAX_PIECES 324
#define BLANK_PIECE -99

@interface TorrentCell (Private)

- (void) drawBar: (NSRect) barRect;

- (NSRect) rectForMinimalStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;
- (NSRect) rectForTitleWithString: (NSAttributedString *) string basedOnMinimalStatusRect: (NSRect) statusRect
            inBounds: (NSRect) bounds;
- (NSRect) rectForProgressWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;
- (NSRect) rectForStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds;

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color;
- (NSAttributedString *) attributedStatusString: (NSString *) string withColor: (NSColor *) color;

@end

@implementation TorrentCell

// Used to optimize drawing. They contain packed RGBA pixels for every color needed.
#define BE OSSwapBigToHostConstInt32

static uint32_t kRed    = BE(0xFF6450FF), //255, 100, 80
                kBlue   = BE(0x50A0FFFF), //80, 160, 255
                kBlue2  = BE(0x1E46B4FF), //30, 70, 180
                kGray   = BE(0x969696FF), //150, 150, 150
                kGreen1 = BE(0x99FFCCFF), //153, 255, 204
                kGreen2 = BE(0x66FF99FF), //102, 255, 153
                kGreen3 = BE(0x00FF66FF), //0, 255, 102
                kWhite  = BE(0xFFFFFFFF); //255, 255, 255

//only called one, so don't worry about release
- (id) init
{
    if ((self = [super init]))
	{
        fDefaults = [NSUserDefaults standardUserDefaults];
        
        NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
    
        fTitleAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 12.0], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        fStatusAttributes = [[NSMutableDictionary alloc] initWithObjectsAndKeys:
                                [NSFont messageFontOfSize: 9.0], NSFontAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        [paragraphStyle release];
        
        fBarOverlayColor = [[NSColor colorWithDeviceWhite: 0.0 alpha: 0.2] retain];
    }
	return self;
}

- (id) copyWithZone: (NSZone *) zone
{
    TorrentCell * copy = [super copyWithZone: zone];
    
    copy->fBitmap = nil;
    copy->fPieces = NULL;
    
    return copy;
}

- (void) dealloc
{
    [fBitmap release];
    if (fPieces)
        free(fPieces);
    
    [super dealloc];
}

- (NSRect) iconRectForBounds: (NSRect) bounds
{
    NSRect result = bounds;
    
    result.origin.x += PADDING_HORIZONAL;
    
    float imageSize;
    if ([fDefaults boolForKey: @"SmallView"])
    {
        imageSize = IMAGE_SIZE_MIN;
        result.origin.y += (result.size.height - imageSize) * 0.5;
    }
    else
    {
        imageSize = IMAGE_SIZE_REG;
        result.origin.y += PADDING_ABOVE_IMAGE_REG;
    }
    
    result.size = NSMakeSize(imageSize, imageSize);
    
    return result;
}

- (NSRect) titleRectForBounds: (NSRect) bounds
{
    return [self rectForTitleWithString: [self attributedTitleWithColor: nil]
            basedOnMinimalStatusRect: [self minimalStatusRectForBounds: bounds] inBounds: bounds];
}

- (NSRect) minimalStatusRectForBounds: (NSRect) bounds
{
    Torrent * torrent = [self representedObject];
    NSString * string = [torrent isActive] && ![fDefaults boolForKey: @"DisplaySmallStatusRegular"]
                            ? [torrent remainingTimeString] : [torrent shortStatusString];
    return [self rectForMinimalStatusWithString: [self attributedStatusString: string withColor: nil] inBounds: bounds];
}

- (NSRect) progressRectForBounds: (NSRect) bounds
{
    return [self rectForProgressWithString: [self attributedStatusString: [[self representedObject] progressString] withColor: nil]
                    inBounds: bounds];
}

- (NSRect) barRectForBounds: (NSRect) bounds
{
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    NSRect result = bounds;
    result.size.height = BAR_HEIGHT;
    result.origin.x = PADDING_HORIZONAL + (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_BAR;
    
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE;
    if (minimal)
        result.origin.y += PADDING_BETWEEN_TITLE_AND_BAR_MIN;
    else
        result.origin.y += PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS + PADDING_BETWEEN_PROGRESS_AND_BAR;
    
    result.size.width = round(NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL - BUTTONS_TOTAL_WIDTH);
    
    return result;
}

- (NSRect) statusRectForBounds: (NSRect) bounds
{
    return [self rectForStatusWithString: [self attributedStatusString: [[self representedObject] statusString] withColor: nil]
                    inBounds: bounds];
}

- (void) drawInteriorWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    [super drawInteriorWithFrame: cellFrame inView: controlView];
    
    Torrent * torrent = [self representedObject];
    
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    //error image
    BOOL error = [torrent isError];
    if (error && !fErrorImage)
    {
        fErrorImage = [[NSImage imageNamed: @"Error.png"] copy];
        [fErrorImage setFlipped: YES];
    }
    
    //icon
    NSImage * icon = minimal && error ? fErrorImage : [torrent icon];
    NSRect iconRect = [self iconRectForBounds: cellFrame];
    [icon drawInRect: iconRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    
    if (error && !minimal)
    {
        NSRect errorRect = NSMakeRect(NSMaxX(iconRect) - IMAGE_SIZE_MIN, NSMaxY(iconRect) - IMAGE_SIZE_MIN,
                                        IMAGE_SIZE_MIN, IMAGE_SIZE_MIN);
        [fErrorImage drawInRect: errorRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    }
    
    //text color
    BOOL highlighted = [self isHighlighted] && [[self highlightColorWithFrame: cellFrame inView: controlView]
                                                isEqual: [NSColor alternateSelectedControlColor]];
    NSColor * titleColor, * statusColor;
    if ([self isHighlighted]
            && [[self highlightColorWithFrame: cellFrame inView: controlView] isEqual: [NSColor alternateSelectedControlColor]])
    {
        titleColor = [NSColor whiteColor];
        statusColor = [NSColor whiteColor];
    }
    else
    {
        titleColor = [NSColor controlTextColor];
        statusColor = [NSColor darkGrayColor];
    }
    
    //minimal status
    NSRect minimalStatusRect;
    if (minimal)
    {
        NSString * string = [torrent isActive] && ![fDefaults boolForKey: @"DisplaySmallStatusRegular"]
                                ? [torrent remainingTimeString] : [torrent shortStatusString];
        NSAttributedString * minimalString = [self attributedStatusString: string withColor: statusColor];
        minimalStatusRect = [self rectForMinimalStatusWithString: minimalString inBounds: cellFrame];
        
        [minimalString drawInRect: minimalStatusRect];
    }
    
    //title
    NSAttributedString * titleString = [self attributedTitleWithColor: titleColor];
    NSRect titleRect = [self rectForTitleWithString: titleString basedOnMinimalStatusRect: minimalStatusRect inBounds: cellFrame];
    [titleString drawInRect: titleRect];
    
    //progress
    if (!minimal)
    {
        NSAttributedString * progressString = [self attributedStatusString: [torrent progressString] withColor: statusColor];
        NSRect progressRect = [self rectForProgressWithString: progressString inBounds: cellFrame];
        [progressString drawInRect: progressRect];
    }
    
    //bar
    NSRect barRect = [self barRectForBounds: cellFrame];
    [self drawBar: barRect];
    
    //status
    if (!minimal)
    {
        NSAttributedString * statusString = [self attributedStatusString: [torrent statusString] withColor: statusColor];
        [statusString drawInRect: [self rectForStatusWithString: statusString inBounds: cellFrame]];
    }
}

@end

@implementation TorrentCell (Private)

- (void) drawBar: (NSRect) barRect
{
    Torrent * torrent = [self representedObject];
    
    int leftWidth = barRect.size.width;
    float progress = [torrent progress];
    
    if (progress < 1.0)
    {
        float rightProgress = 1.0 - progress, progressLeft = [torrent progressLeft];
        int rightWidth = leftWidth * rightProgress;
        leftWidth -= rightWidth;
        
        if (progressLeft < rightProgress)
        {
            int rightNoIncludeWidth = rightWidth * ((rightProgress - progressLeft) / rightProgress);
            rightWidth -= rightNoIncludeWidth;
            
            NSRect noIncludeRect = barRect;
            noIncludeRect.origin.x += barRect.size.width - rightNoIncludeWidth;
            noIncludeRect.size.width = rightNoIncludeWidth;
            
            if (!fLightGrayGradient)
                fLightGrayGradient = [[CTGradient progressLightGrayGradient] retain];
            [fLightGrayGradient fillRect: noIncludeRect angle: -90];
        }
        
        if (rightWidth > 0)
        {
            NSRect includeRect = barRect;
            includeRect.origin.x += leftWidth;
            includeRect.size.width = rightWidth;
            
            if (!fWhiteGradient)
                fWhiteGradient = [[CTGradient progressWhiteGradient] retain];
            [fWhiteGradient fillRect: includeRect angle: -90];
        }
    }
    
    if (leftWidth > 0)
    {
        NSRect completeRect = barRect;
        completeRect.size.width = leftWidth;
        
        if ([torrent isActive])
        {
            if ([torrent isChecking])
            {
                if (!fYellowGradient)
                    fYellowGradient = [[CTGradient progressYellowGradient] retain];
                [fYellowGradient fillRect: completeRect angle: -90];
            }
            else if ([torrent isSeeding])
            {
                int ratioLeftWidth = leftWidth * (1.0 - [torrent progressStopRatio]);
                leftWidth -= ratioLeftWidth;
                
                if (ratioLeftWidth > 0)
                {
                    NSRect ratioLeftRect = barRect;
                    ratioLeftRect.origin.x += leftWidth;
                    ratioLeftRect.size.width = ratioLeftWidth;
                    
                    if (!fLightGreenGradient)
                        fLightGreenGradient = [[CTGradient progressLightGreenGradient] retain];
                    [fLightGreenGradient fillRect: ratioLeftRect angle: -90];
                }
                
                if (leftWidth > 0)
                {
                    completeRect.size.width = leftWidth;
                    
                    if (!fGreenGradient)
                        fGreenGradient = [[CTGradient progressGreenGradient] retain];
                    [fGreenGradient fillRect: completeRect angle: -90];
                }
            }
            else
            {
                if (!fBlueGradient)
                    fBlueGradient = [[CTGradient progressBlueGradient] retain];
                [fBlueGradient fillRect: completeRect angle: -90];
            }
        }
        else
        {
            if ([torrent waitingToStart])
            {
                if ([torrent progressLeft] <= 0.0)
                {
                    if (!fDarkGreenGradient)
                        fDarkGreenGradient = [[CTGradient progressDarkGreenGradient] retain];
                    [fDarkGreenGradient fillRect: completeRect angle: -90];
                }
                else
                {
                    if (!fDarkBlueGradient)
                        fDarkBlueGradient = [[CTGradient progressDarkBlueGradient] retain];
                    [fDarkBlueGradient fillRect: completeRect angle: -90];
                }
            }
            else
            {
                if (!fGrayGradient)
                    fGrayGradient = [[CTGradient progressGrayGradient] retain];
                [fGrayGradient fillRect: completeRect angle: -90];
            }
        }
    }
    
    [fBarOverlayColor set];
    [NSBezierPath strokeRect: NSInsetRect(barRect, 0.5, 0.5)];
}

- (NSRect) rectForMinimalStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    if (![fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSRect result = bounds;
    result.size = [string size];
    
    result.origin.x += bounds.size.width - result.size.width - PADDING_HORIZONAL;
    result.origin.y += PADDING_ABOVE_MIN_STATUS;
    
    return result;
}

- (NSRect) rectForTitleWithString: (NSAttributedString *) string basedOnMinimalStatusRect: (NSRect) statusRect
            inBounds: (NSRect) bounds
{
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
    NSRect result = bounds;
    result.origin.y += PADDING_ABOVE_TITLE;
    result.origin.x += PADDING_HORIZONAL + (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL
                            - (minimal ? PADDING_BETWEEN_TITLE_AND_MIN_STATUS + statusRect.size.width : 0));
    
    return result;
}

- (NSRect) rectForProgressWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSRect result = bounds;
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS;
    result.origin.x += PADDING_HORIZONAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL);
    
    return result;
}

- (NSRect) rectForStatusWithString: (NSAttributedString *) string inBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSRect result = bounds;
    result.origin.y += PADDING_ABOVE_TITLE + HEIGHT_TITLE + PADDING_BETWEEN_TITLE_AND_PROGRESS + HEIGHT_STATUS
                        + PADDING_BETWEEN_PROGRESS_AND_BAR + BAR_HEIGHT + PADDING_BETWEEN_BAR_AND_STATUS;
    result.origin.x += PADDING_HORIZONAL + IMAGE_SIZE_REG + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [string size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL);
    
    return result;
}

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color
{
    if (color)
        [fTitleAttributes setObject: color forKey: NSForegroundColorAttributeName];
    
    NSString * title = [[self representedObject] name];
    return [[[NSAttributedString alloc] initWithString: title attributes: fTitleAttributes] autorelease];
}

- (NSAttributedString *) attributedStatusString: (NSString *) string withColor: (NSColor *) color
{
    if (color)
        [fStatusAttributes setObject: color forKey: NSForegroundColorAttributeName];
    
    return [[[NSAttributedString alloc] initWithString: string attributes: fStatusAttributes] autorelease];
}

@end
