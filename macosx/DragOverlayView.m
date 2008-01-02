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

#import "DragOverlayView.h"
#import "NSBezierPathAdditions.h"

#define PADDING 10.0
#define ICON_WIDTH 64.0

@implementation DragOverlayView

- (id) initWithFrame: (NSRect) frame
{
    if ((self = [super initWithFrame: frame]))
    {
        //create badge
        NSRect badgeRect = NSMakeRect(0, 0, 325.0, 84.0);
        NSBezierPath * bp = [NSBezierPath bezierPathWithRoundedRect: badgeRect radius: 15.0];
        
        fBackBadge = [[NSImage alloc] initWithSize: badgeRect.size];
        [fBackBadge lockFocus];
        
        [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.75] set];
        [bp fill];
        
        [fBackBadge unlockFocus];
        
        //create attributes
        NSShadow * stringShadow = [[NSShadow alloc] init];
        [stringShadow setShadowOffset: NSMakeSize(2.0, -2.0)];
        [stringShadow setShadowBlurRadius: 4.0];
        
        NSFont * bigFont = [[NSFontManager sharedFontManager] convertFont:
                                [NSFont fontWithName: @"Lucida Grande" size: 18.0] toHaveTrait: NSBoldFontMask],
                * smallFont = [NSFont fontWithName: @"Lucida Grande" size: 14.0];
        
        NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
        [paragraphStyle setLineBreakMode: NSLineBreakByTruncatingTail];
        
        fMainLineAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                                [NSColor whiteColor], NSForegroundColorAttributeName,
                                bigFont, NSFontAttributeName, stringShadow, NSShadowAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        fSubLineAttributes = [[NSDictionary alloc] initWithObjectsAndKeys:
                                [NSColor whiteColor], NSForegroundColorAttributeName,
                                smallFont, NSFontAttributeName, stringShadow, NSShadowAttributeName,
                                paragraphStyle, NSParagraphStyleAttributeName, nil];
        
        [stringShadow release];
        [paragraphStyle release];
    }
    return self;
}

- (void) dealloc
{
    [fBackBadge release];
    [fBadge release];
    
    [fMainLineAttributes release];
    [fSubLineAttributes release];
    
    [super dealloc];
}

- (void) setOverlay: (NSImage *) icon mainLine: (NSString *) mainLine subLine: (NSString *) subLine
{
    [fBadge release];
    fBadge = [fBackBadge copy];
    NSSize badgeSize = [fBadge size];
    
    [fBadge lockFocus];
    
    //place icon
    [icon drawInRect: NSMakeRect(PADDING, (badgeSize.height - ICON_WIDTH) * 0.5, ICON_WIDTH, ICON_WIDTH) fromRect: NSZeroRect
            operation: NSCompositeSourceOver fraction: 1.0];
    
    //place main text
    NSSize mainLineSize = [mainLine sizeWithAttributes: fMainLineAttributes];
    NSSize subLineSize = [subLine sizeWithAttributes: fSubLineAttributes];
    
    NSRect lineRect = NSMakeRect(PADDING + ICON_WIDTH + 5.0,
                        (badgeSize.height + (subLineSize.height + 2.0 - mainLineSize.height)) * 0.5,
                        badgeSize.width - (PADDING + ICON_WIDTH + 2.0) - PADDING, mainLineSize.height);
    [mainLine drawInRect: lineRect withAttributes: fMainLineAttributes];
    
    //place sub text
    lineRect.origin.y -= subLineSize.height + 2.0;
    lineRect.size.height = subLineSize.height;
    [subLine drawInRect: lineRect withAttributes: fSubLineAttributes];
    
    [fBadge unlockFocus];
    
    [self setNeedsDisplay: YES];
}

-(void) drawRect: (NSRect) rect
{
    if (fBadge)
    {
        NSRect frame = [self frame];
        NSSize imageSize = [fBadge size];
        [fBadge compositeToPoint: NSMakePoint((frame.size.width - imageSize.width) * 0.5,
                    (frame.size.height - imageSize.height) * 0.5) operation: NSCompositeSourceOver];
    }
}

@end
