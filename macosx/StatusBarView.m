/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

#import "StatusBarView.h"
#import "NSApplicationAdditions.h"
#import "CTGradient.h"

@implementation StatusBarView

- (id) initWithFrame: (NSRect) rect
{
    if ((self = [super initWithFrame: rect]))
    {
        fShow = [NSApp isOnLeopardOrBetter];
    }
    return self;
}

- (void) dealloc
{
    [super dealloc];
}

- (void) setShowOnTiger: (BOOL) show
{
    fShow = show || [NSApp isOnLeopardOrBetter];
}

- (BOOL) isOpaque
{
    return fShow;
}

- (void) drawRect: (NSRect) rect
{
    if (fShow)
    {
        [[NSColor controlColor] set];
        NSRectFill(rect);
        
        NSRect whiteBorderRect = NSMakeRect([self bounds].origin.x, NSMaxY([self bounds]) - 1, [self bounds].size.width, 1);
        if (NSIntersectsRect(whiteBorderRect, rect))
        {
            [[NSColor whiteColor] set];
            NSRectFill(whiteBorderRect);
        }
        
        NSRect grayBorderRect = NSMakeRect([self bounds].origin.x, 0, [self bounds].size.width, 1);
        if (NSIntersectsRect(grayBorderRect, rect))
        {
            [[NSColor colorWithCalibratedRed: 188.0/255.0 green: 188.0/255.0 blue: 188.0/255.0 alpha: 1.0] set];
            NSRectFill(grayBorderRect);
        }
    }
}

@end
