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

#import "PeerProgressIndicatorCell.h"

@implementation PeerProgressIndicatorCell

- (BOOL) hidden
{
    return fIsHidden;
}

- (void) setHidden: (BOOL) isHidden
{
    fIsHidden = isHidden;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    if (!fIsHidden)
    {
        [super drawWithFrame: cellFrame inView: controlView];
        if ([self floatValue] >= 1.0)
        {
            if (!fCheckImage)
            {
                fCheckImage = [NSImage imageNamed: @"CompleteCheck.png"];
                [fCheckImage setFlipped: YES];
            }
            
            NSSize imageSize = [fCheckImage size];
            NSRect rect = NSMakeRect(cellFrame.origin.x + (cellFrame.size.width - imageSize.width) * 0.5,
                            cellFrame.origin.y + (cellFrame.size.height - imageSize.height) * 0.5, imageSize.width, imageSize.height);
            [fCheckImage drawInRect: rect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
        }
    }
}

@end
