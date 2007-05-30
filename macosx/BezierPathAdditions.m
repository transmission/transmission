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

#import "BezierPathAdditions.h"

@implementation NSBezierPath (BezierPathAdditions)

+ (NSBezierPath *) bezierPathWithRoundedRect: (NSRect) rect radius: (float) radius
{
    float minX = NSMinX(rect),
        minY = NSMinY(rect),
        maxX = NSMaxX(rect),
        maxY = NSMaxY(rect),
        midX = NSMidX(rect),
        midY = NSMidY(rect);
    
    NSBezierPath * bp = [NSBezierPath bezierPath];
    [bp moveToPoint: NSMakePoint(maxX, midY)];
    [bp appendBezierPathWithArcFromPoint: NSMakePoint(maxX, maxY) toPoint: NSMakePoint(midX, maxY) radius: radius];
    [bp appendBezierPathWithArcFromPoint: NSMakePoint(minX, maxY) toPoint: NSMakePoint(minX, midY) radius: radius];
    [bp appendBezierPathWithArcFromPoint: NSMakePoint(minX, minY) toPoint: NSMakePoint(midX, minY) radius: radius];
    [bp appendBezierPathWithArcFromPoint: NSMakePoint(maxX, minY) toPoint: NSMakePoint(maxX, midY) radius: radius];
    [bp closePath];
    
    return bp;
}

@end
