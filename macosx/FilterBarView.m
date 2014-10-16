/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2011-2012 Transmission authors and contributors
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

#import "FilterBarView.h"
#import "NSApplicationAdditions.h"

@implementation FilterBarView

- (id) initWithFrame: (NSRect) rect
{
    if ((self = [super initWithFrame: rect]))
    {
        if (![NSApp isOnYosemiteOrBetter]) {
            NSColor * lightColor = [NSColor colorWithCalibratedRed: 235.0/255.0 green: 235.0/255.0 blue: 235.0/255.0 alpha: 1.0];
            NSColor * darkColor = [NSColor colorWithCalibratedRed: 205.0/255.0 green: 205.0/255.0 blue: 205.0/255.0 alpha: 1.0];
            fGradient = [[NSGradient alloc] initWithStartingColor: lightColor endingColor: darkColor];
        }
    }
    return self;
}

- (void) dealloc
{
    [fGradient release];
    [super dealloc];
}

- (BOOL) mouseDownCanMoveWindow
{
    return NO;
}

- (BOOL) isOpaque
{
    return YES;
}

- (void) drawRect: (NSRect) rect
{
    if ([NSApp isOnYosemiteOrBetter]) {
        [[NSColor windowBackgroundColor] setFill];
        NSRectFill(rect);
        
        const NSRect lineBorderRect = NSMakeRect(NSMinX(rect), 0.0, NSWidth(rect), 1.0);
        if (NSIntersectsRect(lineBorderRect, rect))
        {
            [[NSColor lightGrayColor] setFill];
            NSRectFill(lineBorderRect);
        }
    }
    else {
        NSInteger count = 0;
        NSRect gridRects[2];
        NSColor * colorRects[2];
        
        NSRect lineBorderRect = NSMakeRect(NSMinX(rect), NSHeight([self bounds]) - 1.0, NSWidth(rect), 1.0);
        if (NSIntersectsRect(lineBorderRect, rect))
        {
            gridRects[count] = lineBorderRect;
            colorRects[count] = [NSColor whiteColor];
            ++count;
            
            rect.size.height -= 1.0;
        }
        
        lineBorderRect.origin.y = 0.0;
        if (NSIntersectsRect(lineBorderRect, rect))
        {
            gridRects[count] = lineBorderRect;
            colorRects[count] = [NSColor colorWithCalibratedWhite: 0.65 alpha: 1.0];
            ++count;
            
            rect.origin.y += 1.0;
            rect.size.height -= 1.0;
        }
        
        if (!NSIsEmptyRect(rect))
        {
            const NSRect gradientRect = NSMakeRect(NSMinX(rect), 1.0, NSWidth(rect), NSHeight([self bounds]) - 1.0 - 1.0); //proper gradient requires the full height of the bar
            [fGradient drawInRect: gradientRect angle: 270.0];
        }
        
        NSRectFillListWithColors(gridRects, colorRects, count);
    }
}

@end
