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

@implementation StatusBarView

- (id) initWithFrame: (NSRect) rect
{
    if ((self = [super initWithFrame: rect]))
    {
        fGrayBorderColor = [[NSColor colorWithCalibratedRed: 171.0/255.0 green: 171.0/255.0 blue: 171.0/255.0 alpha: 1.0] retain];
    }
    return self;
}

- (void) dealloc
{
    [fGrayBorderColor release];
    [super dealloc];
}

- (void) drawRect: (NSRect) rect
{
    NSRect lineBorderRect = NSMakeRect(rect.origin.x, [self bounds].size.height - 1.0, rect.size.width, 1.0);
    if (NSIntersectsRect(lineBorderRect, rect))
    {
        [[NSColor whiteColor] set];
        NSRectFill(lineBorderRect);
        
        rect.size.height--;
    }
    
    lineBorderRect.origin.y = 0.0;
    if (NSIntersectsRect(lineBorderRect, rect))
    {
        [fGrayBorderColor set];
        NSRectFill(lineBorderRect);
        
        rect.origin.y++;
        rect.size.height--;
    }
    
    [[NSColor controlColor] set];
    NSRectFill(rect);
}

@end
