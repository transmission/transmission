/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2011 Transmission authors and contributors
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

#import "InfoTabButtonBack.h"

@implementation InfoTabButtonBack

- (void) drawRect: (NSRect) rect
{
    NSColor * lightColor = [NSColor colorWithCalibratedRed: 255.0/255.0 green: 255.0/255.0 blue: 255.0/255.0 alpha: 1.0];
    NSColor * darkColor = [NSColor colorWithCalibratedRed: 225.0/255.0 green: 225.0/255.0 blue: 225.0/255.0 alpha: 1.0];
    NSGradient * gradient = [[NSGradient alloc] initWithStartingColor: darkColor endingColor: lightColor];
    [gradient drawInRect: rect angle: 90.0];
    [gradient release];
    
    [[NSColor grayColor] set];
    NSRectFill(NSMakeRect(0.0, 0.0, NSWidth(rect), 1.0));
    NSRectFill(NSMakeRect(0.0, NSHeight(rect) - 1.0, NSWidth(rect), 1.0));
}

@end
