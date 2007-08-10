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

#define MAX_PIECES 324
#define BLANK_PIECE -99

@interface TorrentCell (Private)

- (void) placeBar: (NSImage *) barImage width: (float) width point: (NSPoint) point;
- (void) buildSimpleBar: (float) width point: (NSPoint) point;
- (void) buildAdvancedBar: (float) widthFloat point: (NSPoint) point;
- (NSImage *) advancedBar;

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
    Torrent * torrent = [self representedObject];
    
    NSRect barBounds, completeBounds;
    if([[self controlView] isFlipped])
        barBounds = NSMakeRect(point.x, point.y, width, BAR_HEIGHT);
    else
        barBounds = NSMakeRect(point.x, point.y - BAR_HEIGHT, width, BAR_HEIGHT);
    completeBounds = barBounds;
    
    float progress = [torrent progress];
    completeBounds.size.width = progress * width;
    
    float left = INVALID;
    if (progress < 1.0)
    {
        if (!fWhiteGradient)
            fWhiteGradient = [[CTGradient progressWhiteGradient] retain];
        [fWhiteGradient fillRect: barBounds angle: -90];
        
        left = [torrent progressLeft];
        if ((progress + left) < 1.0)
        {
            NSRect blankBounds = barBounds;
            blankBounds.origin.x += width * (progress + left);
            blankBounds.size.width = width * ((1.0 - progress) - left);
            
            if (!fLightGrayGradient)
                fLightGrayGradient = [[CTGradient progressLightGrayGradient] retain];
            [fLightGrayGradient fillRect: blankBounds angle: -90];
        }
    }
    
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
}

- (void) buildAdvancedBar: (float) width point: (NSPoint) point
{
    //place actual advanced bar
    NSImage * image = [self advancedBar];
    [image setSize: NSMakeSize(width, BAR_HEIGHT)];
    [image compositeToPoint: point operation: NSCompositeSourceOver];
    
    NSRect barBounds;
    if ([[self controlView] isFlipped])
        barBounds = NSMakeRect(point.x, point.y, width, BAR_HEIGHT);
    else
        barBounds = NSMakeRect(point.x, point.y - BAR_HEIGHT, width, BAR_HEIGHT);
    
    if (!fTransparentGradient)
        fTransparentGradient = [[CTGradient progressTransparentGradient] retain];
    
    [fTransparentGradient fillRect: barBounds angle: -90];
    [[NSColor colorWithDeviceWhite: 0.0 alpha: 0.2] set];
    [NSBezierPath strokeRect: NSInsetRect(barBounds, 0.5, 0.5)];
}

- (NSImage *) advancedBar
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
    [bar addRepresentation: fBitmap];
    [bar setScalesWhenResized: YES];
    
    return [bar autorelease];
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
    
    Torrent * torrent = [self representedObject];
    
    if (![fDefaults boolForKey: @"SmallView"]) //regular size
    {
        //icon
        NSImage * icon = [torrent iconFlipped];
        NSSize iconSize = [icon size];
        
        pen.x += PADDING;
        pen.y += (cellFrame.size.height - (iconSize.height + ACTION_BUTTON_HEIGHT)) * 0.5;
        
        [icon drawAtPoint: pen fromRect: NSMakeRect(0, 0, iconSize.width, iconSize.height)
                operation: NSCompositeSourceOver fraction: 1.0];
        
        //error badge
        if ([torrent isError])
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
        
        NSString * nameString = [torrent name];
        NSSize nameSize = [nameString sizeWithAttributes: nameAttributes];
        [nameString drawInRect: NSMakeRect(pen.x, pen.y, mainWidth, nameSize.height) withAttributes: nameAttributes];
        
        //progress string
        pen.y += nameSize.height + LINE_PADDING - 1.0;
        
        NSString * progressString = [torrent progressString];
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
        
        NSString * statusString = [torrent statusString];
        NSSize statusSize = [statusString sizeWithAttributes: statusAttributes];
        [statusString drawInRect: NSMakeRect(pen.x, pen.y, mainWidth, statusSize.height) withAttributes: statusAttributes];
    }
    else //small size
    {
        //icon
        NSImage * icon;
        if ([torrent isError])
        {
            if (!fErrorImage)
            {
                fErrorImage = [[NSImage imageNamed: @"Error.png"] copy];
                [fErrorImage setFlipped: YES];
            }
            icon = fErrorImage;
        }
        else
            icon = [torrent iconSmall];
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

        NSString * nameString = [torrent name];
        NSSize nameSize = [nameString sizeWithAttributes: nameAttributes];
        
        NSString * statusString = ![fDefaults boolForKey: @"SmallStatusRegular"] && [torrent isActive]
                                    ? [torrent remainingTimeString] : [torrent shortStatusString];
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
