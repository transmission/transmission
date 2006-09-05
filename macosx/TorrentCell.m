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

#define BAR_HEIGHT 12.0

@interface TorrentCell (Private)

- (void) placeBar: (NSImage *) barImage width: (float) width point: (NSPoint) point;
- (void) buildSimpleBar: (float) width point: (NSPoint) point;
- (void) buildAdvancedBar: (float) widthFloat point: (NSPoint) point;

@end

@implementation TorrentCell

// Used to optimize drawing. They contain packed RGBA pixels for every color needed.
#define BE OSSwapBigToHostConstInt32
static uint32_t kBorder[] =
    { BE(0x00000005), BE(0x00000010), BE(0x00000015), BE(0x00000015),
      BE(0x00000015), BE(0x00000015), BE(0x00000015), BE(0x00000015),
      BE(0x00000015), BE(0x00000015), BE(0x00000010), BE(0x00000005) };

static uint32_t kBack[] = { BE(0xB4B4B4FF), BE(0xE3E3E3FF) };

static uint32_t kRed   = BE(0xFF6450FF), //255, 100, 80
                kBlue1 = BE(0xA0DCFFFF), //160, 220, 255
                kBlue2 = BE(0x78BEFFFF), //120, 190, 255
                kBlue3 = BE(0x50A0FFFF), //80, 160, 255
                kBlue4 = BE(0x1E46B4FF), //30, 70, 180
                kGray  = BE(0x828282FF), //130, 130, 130
                kGreen = BE(0x00FF00FF); //0, 255, 0

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

- (void) setTorrent: (Torrent *) torrent
{
    fTorrent = torrent;
}

- (void) placeBar: (NSImage *) barImage width: (float) width point: (NSPoint) point
{
    if ([barImage size].width < width)
        [barImage setSize: NSMakeSize(width * 2.0, BAR_HEIGHT)];

    [barImage compositeToPoint: point fromRect: NSMakeRect(0, 0, width, BAR_HEIGHT) operation: NSCompositeSourceOver];
}

- (void) buildSimpleBar: (float) width point: (NSPoint) point
{
    width -= 2.0;
    if ([fTorrent isSeeding])
    {
        [fProgressEndGreen compositeToPoint: point operation: NSCompositeSourceOver];
        
        point.x += 1.0;
        [self placeBar: fProgressGreen width: width point: point];
        
        point.x += width;
        [fProgressEndGreen compositeToPoint: point operation: NSCompositeSourceOver];
    }
    else
    {
        float completedWidth = [fTorrent progress] * width,
                remainingWidth = width - completedWidth;
        BOOL isActive = [fTorrent isActive];
        
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
    //if seeding, there's no need for the advanced bar
    if ([fTorrent isSeeding])
    {
        [self buildSimpleBar: widthFloat point: point];
        return;
    }

    int width = widthFloat; //integers for bars
    
    NSBitmapImageRep * bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: nil
        pixelsWide: width pixelsHigh: BAR_HEIGHT bitsPerSample: 8 samplesPerPixel: 4 hasAlpha: YES
        isPlanar: NO colorSpaceName: NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];

    int h, w;
    uint32_t * p;
    uint8_t * bitmapData = [bitmap bitmapData];
    int bytesPerRow = [bitmap bytesPerRow];

    //left and right borders
    p = (uint32_t *) bitmapData;
    for(h = 0; h < BAR_HEIGHT; h++)
    {
        p[0] = kBorder[h];
        p[width - 1] = kBorder[h];
        p += bytesPerRow / 4;
    }

    int8_t * pieces = malloc(width);
    [fTorrent getAvailability: pieces size: width];
    int avail = 0;
    for (w = 0; w < width; w++)
        if (pieces[w] != 0)
            avail++;

    //first two lines: dark blue to show progression, green to show available
    int end = lrintf(floor([fTorrent progress] * (width - 2)));
    p = (uint32_t *) (bitmapData) + 1;

    for (w = 0; w < end; w++)
    {
        p[w] = kBlue4;
        p[w + bytesPerRow / 4] = kBlue4;
    }
    for (; w < avail; w++)
    {
        p[w] = kGreen;
        p[w + bytesPerRow / 4] = kGreen;
    }
    for (; w < width - 2; w++)
    {
        p[w] = kBack[0];
        p[w + bytesPerRow / 4] = kBack[1];
    }
    
    //lines 2 to 14: blue or grey depending on whether we have the piece or not
    uint32_t color;
    for( w = 0; w < width - 2; w++ )
    {
        //point to pixel ( 2 + w, 2 ). We will then draw "vertically"
        p = (uint32_t *) ( bitmapData + 2 * bytesPerRow ) + 1 + w;

        if (pieces[w] < 0)
            color = kGray;
        else if (pieces[w] == 0)
            color = kRed;
        else if (pieces[w] == 1)
            color = kBlue1;
        else if (pieces[w] == 2)
            color = kBlue2;
        else
            color = kBlue3;

        for( h = 2; h < BAR_HEIGHT; h++ )
        {
            p[0] = color;
            p = (uint32_t *) ( (uint8_t *) p + bytesPerRow );
        }
    }

    free( pieces );
    
    //actually draw image
    NSImage * img = [[NSImage alloc] initWithSize: [bitmap size]];
    [img addRepresentation: bitmap];
    
    //bar size with float, not double, to match standard bar
    [img setScalesWhenResized: YES];
    [img setSize: NSMakeSize(widthFloat, BAR_HEIGHT)];
    
    [img compositeToPoint: point operation: NSCompositeSourceOver];
    [img release];
    [bitmap release];
    
    //draw overlay over advanced bar
    [fProgressEndAdvanced compositeToPoint: point operation: NSCompositeSourceOver];
    
    widthFloat -= 2.0;
    point.x += 1.0;
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

    if (![fDefaults boolForKey: @"SmallView"]) //regular size
    {
        //icon
        NSImage * icon = [fTorrent iconFlipped];
        NSSize iconSize = [icon size];
        
        pen.x += PADDING;
        pen.y += (cellFrame.size.height - iconSize.height) * 0.5;
        
        [icon drawAtPoint: pen fromRect: NSMakeRect(0, 0, iconSize.width, iconSize.height)
                operation: NSCompositeSourceOver fraction: 1.0];
        
        //error badge
        if ([fTorrent isError])
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
        NSAttributedString * nameString = [[fTorrent name] attributedStringFittingInWidth: mainWidth
                                                attributes: nameAttributes];
        [nameString drawAtPoint: pen];
        
        //progress string
        pen.y += [nameString size].height + LINE_PADDING - 1.0;
        
        NSAttributedString * progressString = [[fTorrent progressString]
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
        NSAttributedString * statusString = [[fTorrent statusString]
            attributedStringFittingInWidth: mainWidth attributes: statusAttributes];
        [statusString drawAtPoint: pen];
    }
    else //small size
    {
        //icon
        NSImage * icon = ![fTorrent isError] ? [fTorrent iconSmall] : fErrorImage;
        NSSize iconSize = [icon size];
        
        pen.x += PADDING;
        pen.y += (cellFrame.size.height - iconSize.height) * 0.5;
        
        [icon drawAtPoint: pen fromRect: NSMakeRect(0, 0, iconSize.width, iconSize.height)
                operation: NSCompositeSourceOver fraction: 1.0];

        //name and status string
        float mainWidth = cellFrame.size.width - iconSize.width - 3.0 * PADDING - EXTRA_NAME_SHIFT;
        
        NSString * realStatusString = !fStatusRegular && [fTorrent isActive] ? [fTorrent remainingTimeString]
                                                                            : [fTorrent shortStatusString];
        
        NSAttributedString * statusString = [[[NSAttributedString alloc] initWithString: realStatusString
                                                    attributes: statusAttributes] autorelease];
        NSAttributedString * nameString = [[fTorrent name] attributedStringFittingInWidth:
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
