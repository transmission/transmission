/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Transmission authors and contributors
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

@implementation DragOverlayView

- (id) initWithFrame: (NSRect) frame
{
    if ((self = [super initWithFrame: frame]))
    {
        //create badge
        NSRect badgeRect = NSMakeRect(0, 0, 325.0, 84.0);
        
        fBackBadge = [[NSImage alloc] initWithSize: badgeRect.size];
        [fBackBadge lockFocus];
        
        [NSBezierPath setDefaultLineWidth: 5.0];
        
        [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.75] set];
        [NSBezierPath fillRect: badgeRect];
        
        [[NSColor whiteColor] set];
        [NSBezierPath strokeRect: badgeRect];
        
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
    }
    return self;
}

- (void) dealloc
{
    [fBackBadge release];
    
    if (fBadge)
        [fBadge release];
    if (fAppIcon)
        [fAppIcon release];
    
    [fMainLineAttributes release];
    [fSubLineAttributes release];
    
    [super dealloc];
}

- (void) setOverlay: (NSImage *) icon mainLine: (NSString *) mainLine subLine: (NSString *) subLine
{
    if (fBadge)
        [fBadge release];
    fBadge = [fBackBadge copy];
    NSSize badgeSize = [fBadge size];
    
    //get icon
    NSSize iconSize = NSMakeSize(64.0, 64.0);
    if (!icon)
    {
        if (!fAppIcon)
        {
            fAppIcon = [[NSImage imageNamed: @"TransmissionDocument.icns"] copy];
            [fAppIcon setScalesWhenResized: YES];
            [fAppIcon setSize: iconSize];
        }
        icon = [fAppIcon retain];
    }
    else
    {
        icon = [icon copy];
        [icon setScalesWhenResized: YES];
        [icon setSize: iconSize];
    }
    
    float padding = 10.0;
    
    [fBadge lockFocus];
    
    //place icon
    [icon compositeToPoint: NSMakePoint(padding, (badgeSize.height - iconSize.height) * 0.5)
                operation: NSCompositeSourceOver];
    
    //place main text
    NSSize mainLineSize = [mainLine sizeWithAttributes: fMainLineAttributes];
    NSSize subLineSize = [subLine sizeWithAttributes: fSubLineAttributes];
    
    NSRect lineRect = NSMakeRect(padding + iconSize.width + 5.0,
                        (badgeSize.height + (subLineSize.height + 2.0 - mainLineSize.height)) * 0.5,
                        badgeSize.width - (padding + iconSize.width + 2.0) - padding, mainLineSize.height);
    [mainLine drawInRect: lineRect withAttributes: fMainLineAttributes];
    
    //place sub text
    lineRect.origin.y -= subLineSize.height + 2.0;
    lineRect.size.height = subLineSize.height;
    [subLine drawInRect: lineRect withAttributes: fSubLineAttributes];
    
    [fBadge unlockFocus];
    
    [icon release];
    
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
