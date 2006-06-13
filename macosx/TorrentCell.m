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
- (void) buildSimpleBar: (int) width point: (NSPoint) point;
- (void) buildAdvancedBar: (int) width point: (NSPoint) point;

@end

@implementation TorrentCell

static NSImage * fProgressWhite, * fProgressBlue, * fProgressGray, * fProgressGreen,
                * fProgressAdvanced, * fProgressEndWhite, * fProgressEndBlue,
                * fProgressEndGray, * fProgressEndGreen, * fProgressEndAdvanced;

// Used to optimize drawing. They contain packed RGBA pixels for every color needed.
static uint32_t kBorder[] =
    { 0x00000005, 0x00000010, 0x00000015, 0x00000015,
      0x00000015, 0x00000015, 0x00000015, 0x00000015,
      0x00000015, 0x00000015, 0x00000010, 0x00000005 };

static uint32_t kBack[] = { 0xB4B4B4FF, 0xE3E3E3FF };

static uint32_t kRed = 0xFF6450FF, //255, 100, 80
                kBlue1 = 0xA0DCFFFF, //160, 220, 255
                kBlue2 = 0x78BEFFFF, //120, 190, 255
                kBlue3 = 0x50A0FFFF, //80, 160, 255
                kBlue4 = 0x1E46B4FF, //30, 70, 180
                kGray = 0x828282FF, //130, 130, 130
                kGreen = 0x00FF00FF; //0, 255, 0

- (id) init
{
    if ((self = [super init]))
    {
        NSSize startSize = NSMakeSize(100.0, BAR_HEIGHT);
        if (!fProgressWhite)
        {
            fProgressWhite = [NSImage imageNamed: @"ProgressBarWhite.png"];
            [fProgressWhite setScalesWhenResized: YES];
            [fProgressWhite setSize: startSize];
        }
        if (!fProgressBlue)
        {
            fProgressBlue = [NSImage imageNamed: @"ProgressBarBlue.png"];
            [fProgressBlue setScalesWhenResized: YES];
            [fProgressBlue setSize: startSize];
        }
        if (!fProgressGray)
        {
            fProgressGray = [NSImage imageNamed: @"ProgressBarGray.png"];
            [fProgressGray setScalesWhenResized: YES];
            [fProgressGray setSize: startSize];
        }
        if (!fProgressGreen)
        {
            fProgressGreen = [NSImage imageNamed: @"ProgressBarGreen.png"];
            [fProgressGreen setScalesWhenResized: YES];
            [fProgressGreen setSize: startSize];
        }
        if (!fProgressAdvanced)
        {
            fProgressAdvanced = [NSImage imageNamed: @"ProgressBarAdvanced.png"];
            [fProgressAdvanced setScalesWhenResized: YES];
            [fProgressAdvanced setSize: startSize];
        }
        
        if (!fProgressEndWhite)
            fProgressEndWhite = [NSImage imageNamed: @"ProgressBarEndWhite.png"];
        if (!fProgressEndBlue)
            fProgressEndBlue = [NSImage imageNamed: @"ProgressBarEndBlue.png"];
        if (!fProgressEndGray)
            fProgressEndGray = [NSImage imageNamed: @"ProgressBarEndGray.png"];
        if (!fProgressEndGreen)
            fProgressEndGreen = [NSImage imageNamed: @"ProgressBarEndGreen.png"];
        if (!fProgressEndAdvanced)
            fProgressEndAdvanced = [NSImage imageNamed: @"ProgressBarEndAdvanced.png"];
    }
    return self;
}

- (void) setTorrent: (Torrent *) torrent
{
    fTorrent = torrent;
}

- (void) placeBar: (NSImage *) barImage width: (float) width point: (NSPoint) point
{
    if ([barImage size].width < width)
        [barImage setSize: NSMakeSize(width * 1.5, BAR_HEIGHT)];

    [barImage compositeToPoint: point fromRect: NSMakeRect(0, 0, width, BAR_HEIGHT)
                        operation: NSCompositeSourceOver];
}

- (void) buildSimpleBar: (int) width point: (NSPoint) point
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
    
        NSImage * barActiveEnd, * barActive;
        if ([fTorrent isActive])
        {
            barActiveEnd = fProgressEndBlue;
            barActive = fProgressBlue;
        }
        else
        {
            barActiveEnd = fProgressEndGray;
            barActive = fProgressGray;
        }
        if (completedWidth < 1.0)
            barActiveEnd = fProgressEndWhite;
    
        [barActiveEnd compositeToPoint: point operation: NSCompositeSourceOver];
        
        point.x += 1.0;
        [self placeBar: barActive width: completedWidth point: point];
        
        point.x += completedWidth;
        [self placeBar: fProgressWhite width: remainingWidth point: point];
        
        point.x += remainingWidth;
        [[fTorrent progress] < 1.0 ? fProgressEndWhite : fProgressEndGray
                    compositeToPoint: point operation: NSCompositeSourceOver];
    }
}

- (void) buildAdvancedBar: (int) width point: (NSPoint) point
{
    //if seeding, there's no need for the advanced bar
    if ([fTorrent isSeeding])
    {
        [self buildSimpleBar: width point: point];
        return;
    }

    NSBitmapImageRep * bitmap = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes: nil pixelsWide: width
        pixelsHigh: BAR_HEIGHT bitsPerSample: 8 samplesPerPixel: 4
        hasAlpha: YES isPlanar: NO colorSpaceName:
        NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];

    int h, w;
    uint32_t * p;
    uint8_t * bitmapData = [bitmap bitmapData];
    int bytesPerRow = [bitmap bytesPerRow];

    /* Left and right borders */
    p = (uint32_t *) bitmapData;
    for( h = 0; h < BAR_HEIGHT; h++ )
    {
        p[0] = htonl( kBorder[h] );
        p[width - 1] = htonl( kBorder[h] );
        p += bytesPerRow / 4;
    }

    int8_t * pieces = malloc( width );
    [fTorrent getAvailability: pieces size: width];

    /* First two lines: dark blue to show progression */
    int end  = lrintf( floor( [fTorrent progress] * ( width - 2 ) ) );
    for( h = 0; h < 2; h++ )
    {
        p = (uint32_t *) ( bitmapData + h * bytesPerRow ) + 1;
        for( w = 0; w < end; w++ )
            p[w] = htonl( kBlue4 );
        for( w = end; w < width - 2; w++ )
            p[w] = htonl( kBack[h] );
    }

    /* Lines 2 to 14: blue or grey depending on whether
       we have the piece or not */
    uint32_t color;
    for( w = 0; w < width - 2; w++ )
    {
        /* Point to pixel ( 2 + w, 2 ). We will then draw
           "vertically" */
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
            p[0] = htonl( color );
            p = (uint32_t *) ( (uint8_t *) p + bytesPerRow );
        }
    }

    free( pieces );
    
    //actually draw image
    NSImage * img = [[NSImage alloc] initWithSize: [bitmap size]];
    [img addRepresentation: bitmap];
    [img compositeToPoint: point operation: NSCompositeSourceOver];
    [img release];
    [bitmap release];
    
    //draw overlay over advanced bar
    width -= 2.0;
    
    [fProgressEndAdvanced compositeToPoint: point operation: NSCompositeSourceOver];
    
    point.x += 1.0;
    [self placeBar: fProgressAdvanced width: width point: point];
    
    point.x += width;
    [fProgressEndAdvanced compositeToPoint: point operation: NSCompositeSourceOver];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) view
{
    BOOL highlighted = [self isHighlighted] && [[view window] isKeyWindow];
    NSDictionary * nameAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor blackColor],
                    NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 12.0], NSFontAttributeName, nil];
    NSDictionary * statusAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                    highlighted ? [NSColor whiteColor] : [NSColor darkGrayColor],
                    NSForegroundColorAttributeName,
                    [NSFont messageFontOfSize: 9.0], NSFontAttributeName, nil];

    NSPoint pen = cellFrame.origin;
    float padding = 3.0, linePadding = 2.0;

    //icon
    NSImage * icon = [fTorrent iconFlipped];
    NSSize iconSize = [icon size];
    
    pen.x += padding;
    pen.y += (cellFrame.size.height - iconSize.height) * 0.5;
    
    [icon drawAtPoint: pen fromRect: NSMakeRect( 0, 0, iconSize.width, iconSize.height )
            operation: NSCompositeSourceOver fraction: 1.0];

    float extraNameShift = 1.0,
        mainWidth = cellFrame.size.width - iconSize.width - 3.0 * padding - extraNameShift;

    //name string
    pen.x += iconSize.width + padding + extraNameShift;
    pen.y = cellFrame.origin.y + padding;
    NSAttributedString * nameString = [[fTorrent name] attributedStringFittingInWidth: mainWidth
                                attributes: nameAttributes];
    [nameString drawAtPoint: pen];
    
    //progress string
    pen.y += [nameString size].height + linePadding - 1.0;
    
    NSAttributedString * progressString = [[fTorrent progressString]
        attributedStringFittingInWidth: mainWidth attributes: statusAttributes];
    [progressString drawAtPoint: pen];

    //progress bar
    pen.x -= extraNameShift;
    pen.y += [progressString size].height + linePadding + BAR_HEIGHT;
    
    float barWidth = mainWidth + extraNameShift - BUTTONS_TOTAL_WIDTH + padding;
    
    if ([[NSUserDefaults standardUserDefaults] boolForKey: @"UseAdvancedBar"])
        [self buildAdvancedBar: barWidth point: pen];
    else
        [self buildSimpleBar: barWidth point: pen];

    //status strings
    pen.x += extraNameShift;
    pen.y += linePadding;
    NSAttributedString * statusString = [[fTorrent statusString]
        attributedStringFittingInWidth: mainWidth attributes: statusAttributes];
    [statusString drawAtPoint: pen];
    
    [nameAttributes release];
    [statusAttributes release];
}

@end
