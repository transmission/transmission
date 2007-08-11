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

#define BAR_HEIGHT 12.0

#define PADDING_HORIZONAL 2.0
#define IMAGE_SIZE_REG 32.0
#define IMAGE_SIZE_MIN 16.0
#define PADDING_BETWEEN_IMAGE_AND_TITLE 4.0
#define PADDING_ABOVE_TITLE 2.0
#define PADDING_ABOVE_MIN_STATUS 4.0
#define PADDING_BETWEEN_TITLE_AND_MIN_STATUS 2.0
#define PADDING_BETWEEN_TITLE_AND_PROGRESS 2.0
#define PADDING_LESS_BETWEEN_TITLE_AND_BAR 1.0
#define PADDING_BETWEEN_PROGRESS_AND_BAR 2.0
#define PADDING_BETWEEN_BAR_AND_STATUS 2.0

#define MAX_PIECES 324
#define BLANK_PIECE -99

@interface TorrentCell (Private)

- (NSImage *) simpleBar: (NSSize) barSize;
- (NSImage *) advancedBar: (NSSize) barSize;
- (NSImage *) advancedBarSimple;

- (NSRect) rectForTitleBasedOnMinimalStatusRect: (NSRect) statusRect inBounds: (NSRect) bounds;
- (NSRect) rectForProgressBasedOnTitleRect: (NSRect) titleRect inBounds: (NSRect) bounds;
- (NSRect) rectForBarBasedOnAboveRect: (NSRect) aboveRect inBounds: (NSRect) bounds;
- (NSRect) rectForStatusBasedOnProgressRect: (NSRect) progressRect inBounds: (NSRect) bounds;

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color;
- (NSAttributedString *) attributedProgressWithColor: (NSColor *) color;
- (NSAttributedString *) attributedStatusWithColor: (NSColor *) color;
- (NSAttributedString *) attributedMinimalStatusWithColor: (NSColor *) color;

@end

@implementation TorrentCell

// Used to optimize drawing. They contain packed RGBA pixels for every color needed.
#define BE OSSwapBigToHostConstInt32

static uint32_t kRed   = BE(0xFF6450FF), //255, 100, 80
                kBlue = BE(0x50A0FFFF), //80, 160, 255
                kBlue2 = BE(0x1E46B4FF), //30, 70, 180
                kGray  = BE(0x969696FF), //150, 150, 150
                kGreen1 = BE(0x99FFCCFF), //153, 255, 204
                kGreen2 = BE(0x66FF99FF), //102, 255, 153
                kGreen3 = BE(0x00FF66FF), //0, 255, 102
                kWhite = BE(0xFFFFFFFF); //255, 255, 255

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

    }
	return self;
}

- (id) copyWithZone: (NSZone *) zone
{
    TorrentCell * copy = [super copyWithZone: zone];
    
    copy->fBitmap = nil;
    copy->fPieces = NULL;
}

- (void) dealloc
{
    [fBitmap release];
    if (fPieces)
        free(fPieces);
    
    [super dealloc];
}

- (NSRect) imageRectForBounds: (NSRect) bounds
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
        result.origin.y += (result.size.height - (imageSize + ACTION_BUTTON_HEIGHT)) * 0.5;
    }
    
    result.size = NSMakeSize(imageSize, imageSize);
    
    return result;
}

- (NSRect) titleRectForBounds: (NSRect) bounds
{
    return [self rectForTitleBasedOnMinimalStatusRect: [self minimalStatusRectForBounds: bounds] inBounds: bounds];
}

- (NSRect) minimalStatusRectForBounds: (NSRect) bounds
{
    if (![fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSAttributedString * status = [self attributedMinimalStatusWithColor: nil];
    
    NSRect result = bounds;
    result.size = [status size];
    
    result.origin.x += bounds.size.width - result.size.width - PADDING_HORIZONAL;
    result.origin.y += PADDING_ABOVE_MIN_STATUS;
    
    return result;
}

- (NSRect) progressRectForBounds: (NSRect) bounds
{
    return [self rectForProgressBasedOnTitleRect: [self titleRectForBounds: bounds] inBounds: bounds];
}

- (NSRect) barRectForBounds: (NSRect) bounds
{
    NSRect aboveRect = [fDefaults boolForKey: @"SmallView"] ? [self titleRectForBounds: bounds]
                                                            : [self progressRectForBounds: bounds];
    return [self rectForBarBasedOnAboveRect: aboveRect inBounds: bounds];
}

- (NSRect) statusRectForBounds: (NSRect) bounds
{
    return [self rectForStatusBasedOnProgressRect: [self progressRectForBounds: bounds] inBounds: bounds];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
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
    NSImage * icon = minimal ? (error ? fErrorImage : [torrent iconSmall]) : [torrent iconFlipped];
    NSRect iconRect = [self imageRectForBounds: cellFrame];
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
    NSRect minimalStatusRect = [self minimalStatusRectForBounds: cellFrame];
    if (minimal)
        [[self attributedMinimalStatusWithColor: statusColor] drawInRect: minimalStatusRect];
    
    //title
    NSRect titleRect = [self rectForTitleBasedOnMinimalStatusRect: minimalStatusRect inBounds: cellFrame];
    [[self attributedTitleWithColor: titleColor] drawInRect: titleRect];
    
    //progress
    NSRect progressRect;
    if (!minimal)
    {
        progressRect = [self rectForProgressBasedOnTitleRect: titleRect inBounds: cellFrame];
        [[self attributedProgressWithColor: statusColor] drawInRect: progressRect];
    }
    
    //bar
    NSRect barRect = [self rectForBarBasedOnAboveRect: minimal ? titleRect : progressRect inBounds: cellFrame];
    NSImage * bar = [fDefaults boolForKey: @"UseAdvancedBar"] ? [self advancedBar: barRect.size] : [self simpleBar: barRect.size];
    [bar drawInRect: barRect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
    
    //status
    if (!minimal)
    {
        NSRect statusRect = [self rectForStatusBasedOnProgressRect: progressRect inBounds: cellFrame];
        [[self attributedStatusWithColor: statusColor] drawInRect: statusRect];
    }
}

@end

@implementation TorrentCell (Private)

- (NSImage *) simpleBar: (NSSize) barSize
{
    Torrent * torrent = [self representedObject];
    
    NSImage * bar = [[NSImage alloc] initWithSize: barSize];
    [bar lockFocus];
    
    NSRect barBounds = NSMakeRect(0, 0, barSize.width, barSize.height);
    
    float progress = [torrent progress];
    
    float left = INVALID;
    if (progress < 1.0)
    {
        if (!fWhiteGradient)
            fWhiteGradient = [[CTGradient progressWhiteGradient] retain];
        [fWhiteGradient fillRect: barBounds angle: -90];
        
        left = [torrent progressLeft];
        float include = progress + left;
        if (include < 1.0)
        {
            NSRect blankBounds = barBounds;
            blankBounds.origin.x += barBounds.size.width * include;
            blankBounds.size.width *= (1.0 - include);
            
            if (!fLightGrayGradient)
                fLightGrayGradient = [[CTGradient progressLightGrayGradient] retain];
            [fLightGrayGradient fillRect: blankBounds angle: -90];
        }
    }
    
    NSRect completeBounds = barBounds;
    completeBounds.size.width *= progress;
    
    if ([torrent isActive])
    {
        if ([torrent isChecking])
        {
            if (!fYellowGradient)
                fYellowGradient = [[CTGradient progressYellowGradient] retain];
            [fYellowGradient fillRect: completeBounds angle: -90];
        }
        else if ([torrent isSeeding])
        {
            NSRect ratioBounds = completeBounds;
            ratioBounds.size.width *= [torrent progressStopRatio];
            
            if (ratioBounds.size.width < completeBounds.size.width)
            {
                if (!fLightGreenGradient)
                    fLightGreenGradient = [[CTGradient progressLightGreenGradient] retain];
                [fLightGreenGradient fillRect: completeBounds angle: -90];
            }
            
            if (!fGreenGradient)
                fGreenGradient = [[CTGradient progressGreenGradient] retain];
            [fGreenGradient fillRect: ratioBounds angle: -90]; 
        }
        else
        {
            if (!fBlueGradient)
                fBlueGradient = [[CTGradient progressBlueGradient] retain];
            [fBlueGradient fillRect: completeBounds angle: -90];
        }
    }
    else
    {
        if ([torrent waitingToStart])
        {
            if (left == INVALID)
                left = [torrent progressLeft];
            
            if (left <= 0.0)
            {
                if (!fDarkGreenGradient)
                    fDarkGreenGradient = [[CTGradient progressDarkGreenGradient] retain];
                [fDarkGreenGradient fillRect: completeBounds angle: -90];
            }
            else
            {
                if (!fDarkBlueGradient)
                    fDarkBlueGradient = [[CTGradient progressDarkBlueGradient] retain];
                [fDarkBlueGradient fillRect: completeBounds angle: -90];
            }
        }
        else
        {
            if (!fGrayGradient)
                fGrayGradient = [[CTGradient progressGrayGradient] retain];
            [fGrayGradient fillRect: completeBounds angle: -90];
        }
    }
    
    [[NSColor colorWithDeviceWhite: 0.0 alpha: 0.2] set];
    [NSBezierPath strokeRect: NSInsetRect(barBounds, 0.5, 0.5)];
    
    [bar unlockFocus];
    
    return [bar autorelease];
}

- (NSImage *) advancedBar: (NSSize) barSize
{
    NSImage * image = [self advancedBarSimple];
    
    [image setScalesWhenResized: YES];
    [image setSize: barSize];
    
    [image lockFocus];
    
    NSRect barBounds = NSMakeRect(0, 0, barSize.width, barSize.height);
    
    if (!fTransparentGradient)
        fTransparentGradient = [[CTGradient progressTransparentGradient] retain];
    [fTransparentGradient fillRect: barBounds angle: 90];
    
    [[NSColor colorWithDeviceWhite: 0.0 alpha: 0.2] set];
    [NSBezierPath strokeRect: NSInsetRect(barBounds, 0.5, 0.5)];
    
    [image unlockFocus];
    return image;
}

- (NSImage *) advancedBarSimple
{
    if (!fBitmap)
        fBitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: nil
            pixelsWide: MAX_PIECES pixelsHigh: BAR_HEIGHT bitsPerSample: 8 samplesPerPixel: 4 hasAlpha: YES
            isPlanar: NO colorSpaceName: NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];
    
    uint32_t * p;
    uint8_t * bitmapData = [fBitmap bitmapData];
    int bytesPerRow = [fBitmap bytesPerRow];
    
    if (!fPieces)
    {
        fPieces = malloc(MAX_PIECES);
        int i;
        for (i = 0; i < MAX_PIECES; i++)
            fPieces[i] = BLANK_PIECE;
    }
    
    Torrent * torrent = [self representedObject];
    int pieceCount = [torrent pieceCount];
    int8_t * piecesAvailablity = malloc(pieceCount);
    [torrent getAvailability: piecesAvailablity size: pieceCount];
    
    //lines 2 to 14: blue, green, or gray depending on piece availability
    int i, h, index = 0;
    float increment = (float)pieceCount / MAX_PIECES, indexValue = 0;
    uint32_t color;
    BOOL change;
    for (i = 0; i < MAX_PIECES; i++)
    {
        change = NO;
        if (piecesAvailablity[index] < 0)
        {
            if (fPieces[i] != -1)
            {
                color = kBlue;
                fPieces[i] = -1;
                change = YES;
            }
        }
        else if (piecesAvailablity[index] == 0)
        {
            if (fPieces[i] != 0)
            {
                color = kGray;
                fPieces[i] = 0;
                change = YES;
            }
        }
        else if (piecesAvailablity[index] <= 4)
        {
            if (fPieces[i] != 1)
            {
                color = kGreen1;
                fPieces[i] = 1;
                change = YES;
            }
        }
        else if (piecesAvailablity[index] <= 8)
        {
            if (fPieces[i] != 2)
            {
                color = kGreen2;
                fPieces[i] = 2;
                change = YES;
            }
        }
        else
        {
            if (fPieces[i] != 3)
            {
                color = kGreen3;
                fPieces[i] = 3;
                change = YES;
            }
        }
        
        if (change)
        {
            //point to pixel (i, 2) and draw "vertically"
            p = (uint32_t *)(bitmapData + 2 * bytesPerRow) + i;
            for (h = 2; h < BAR_HEIGHT; h++)
            {
                p[0] = color;
                p = (uint32_t *)((uint8_t *)p + bytesPerRow);
            }
        }
        
        indexValue += increment;
        index = (int)indexValue;
    }
    
    //determine percentage finished and available
    int have = rintf((float)MAX_PIECES * [torrent progress]), avail;
    if (![torrent isActive] || [torrent progress] >= 1.0 || [torrent totalPeersConnected] <= 0)
        avail = 0;
    else
    {
        float * piecesFinished = malloc(pieceCount * sizeof(float));
        [torrent getAmountFinished: piecesFinished size: pieceCount];
        
        float available = 0;
        for (i = 0; i < pieceCount; i++)
            if (piecesAvailablity[i] > 0)
                available += 1.0 - piecesFinished[i];
        
        avail = rintf(MAX_PIECES * available / (float)pieceCount);
        if (have + avail > MAX_PIECES) //case if both end in .5 and all pieces are available
            avail--;
        
        free(piecesFinished);
    }
    
    free(piecesAvailablity);
    
    //first two lines: dark blue to show progression, green to show available
    p = (uint32_t *)bitmapData;
    for (i = 0; i < have; i++)
    {
        p[i] = kBlue2;
        p[i + bytesPerRow / 4] = kBlue2;
    }
    for (; i < avail + have; i++)
    {
        p[i] = kGreen3;
        p[i + bytesPerRow / 4] = kGreen3;
    }
    for (; i < MAX_PIECES; i++)
    {
        p[i] = kWhite;
        p[i + bytesPerRow / 4] = kWhite;
    }
    
    //actually draw image
    NSImage * bar = [[NSImage alloc] initWithSize: [fBitmap size]];
    [bar setFlipped: YES];
    [bar addRepresentation: fBitmap];
    
    return [bar autorelease];
}

- (NSRect) rectForTitleBasedOnMinimalStatusRect: (NSRect) statusRect inBounds: (NSRect) bounds
{
    BOOL minimal = [fDefaults boolForKey: @"SmallView"];
    
   NSRect result = bounds;
    
    result.origin.y += PADDING_ABOVE_TITLE;
    result.origin.x += PADDING_HORIZONAL + (minimal ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG) + PADDING_BETWEEN_IMAGE_AND_TITLE;
    
    result.size = [[self attributedTitleWithColor: nil] size];
    result.size.width = MIN(result.size.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL
                            - (minimal ? PADDING_BETWEEN_TITLE_AND_MIN_STATUS + statusRect.size.width : 0));
    
    return result;
}

- (NSRect) rectForProgressBasedOnTitleRect: (NSRect) titleRect inBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSSize progressSize = [[self attributedProgressWithColor: nil] size];
    
    NSRect result = titleRect;
    
    result.size.width = MIN(progressSize.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL);
    
    result.origin.y += titleRect.size.height + PADDING_BETWEEN_TITLE_AND_PROGRESS;
    result.size.height = progressSize.height;
    
    return result;
}

- (NSRect) rectForBarBasedOnAboveRect: (NSRect) aboveRect inBounds: (NSRect) bounds
{
    NSRect result;
    result.size.height = BAR_HEIGHT;
    result.origin.x = bounds.origin.x + 
                        PADDING_HORIZONAL + ([fDefaults boolForKey: @"SmallView"] ? IMAGE_SIZE_MIN : IMAGE_SIZE_REG)
                        + PADDING_BETWEEN_IMAGE_AND_TITLE - PADDING_LESS_BETWEEN_TITLE_AND_BAR;
    result.origin.y = NSMaxY(aboveRect) + PADDING_BETWEEN_PROGRESS_AND_BAR;
    result.size.width = NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL - BUTTONS_TOTAL_WIDTH;
    
    return result;
}

- (NSRect) rectForStatusBasedOnProgressRect: (NSRect) progressRect inBounds: (NSRect) bounds
{
    if ([fDefaults boolForKey: @"SmallView"])
        return NSZeroRect;
    
    NSSize statusSize = [[self attributedStatusWithColor: nil] size];
    
    NSRect result = progressRect;
    result.size.width = MIN(statusSize.width, NSMaxX(bounds) - result.origin.x - PADDING_HORIZONAL);
    
    result.origin.y += statusSize.height + PADDING_BETWEEN_PROGRESS_AND_BAR + BAR_HEIGHT + PADDING_BETWEEN_BAR_AND_STATUS;
    result.size.height = statusSize.height;
    
    return result;
}

- (NSAttributedString *) attributedTitleWithColor: (NSColor *) color
{
    if (color)
        [fTitleAttributes setObject: color forKey: NSForegroundColorAttributeName];
        
    NSString * title = [[self representedObject] name];
    return [[[NSAttributedString alloc] initWithString: title attributes: fTitleAttributes] autorelease];
}

- (NSAttributedString *) attributedProgressWithColor: (NSColor *) color
{
    if (color)
        [fStatusAttributes setObject: color forKey: NSForegroundColorAttributeName];
        
    NSString * progress = [[self representedObject] progressString];
    return [[[NSAttributedString alloc] initWithString: progress attributes: fStatusAttributes] autorelease];
}

- (NSAttributedString *) attributedStatusWithColor: (NSColor *) color
{
    if (color)
        [fStatusAttributes setObject: color forKey: NSForegroundColorAttributeName];
        
    NSString * status = [[self representedObject] statusString];
    return [[[NSAttributedString alloc] initWithString: status attributes: fStatusAttributes] autorelease];
}

#warning combine?
- (NSAttributedString *) attributedMinimalStatusWithColor: (NSColor *) color
{
    if (color)
        [fStatusAttributes setObject: color forKey: NSForegroundColorAttributeName];
    
    Torrent * torrent = [self representedObject];
    NSString * status = ![fDefaults boolForKey: @"SmallStatusRegular"] && [torrent isActive]
                                    ? [torrent remainingTimeString] : [torrent shortStatusString];
    
    return [[[NSAttributedString alloc] initWithString: status attributes: fStatusAttributes] autorelease];
}

@end
