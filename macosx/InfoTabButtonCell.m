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

#import "InfoTabButtonCell.h"

@implementation InfoTabButtonCell

- (void) dealloc
{
    [fRegularImage release];
    [fSelectedImage release];
    [super dealloc];
}

- (void) setIcon: (NSImage *) image
{
    //create regular back image
    [fRegularImage release];
    fRegularImage = [[NSImage imageNamed: @"InfoTabBack.tif"] copy];
    
    //create selected back image
    [fSelectedImage release];
    fSelectedImage = [[NSImage imageNamed: @"InfoTabBackAqua.tif"] copy];
    
    //composite image to back images
    if (image)
    {
        NSSize imageSize = [image size];
        NSRect imageRect = NSMakeRect(0, 0, [fRegularImage size].width, [fRegularImage size].height);
        NSRect rect = NSMakeRect(imageRect.origin.x + (imageRect.size.width - imageSize.width) * 0.5,
                        imageRect.origin.y + (imageRect.size.height - imageSize.height) * 0.5, imageSize.width, imageSize.height);
        
        [fRegularImage lockFocus];
        [image drawInRect: rect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
        [fRegularImage unlockFocus];
        
        [fSelectedImage lockFocus];
        [image drawInRect: rect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0];
        [fSelectedImage unlockFocus];
    }
    
    [self setImage: fRegularImage];
}

- (void) setSelectedTab: (BOOL) selected
{
    [self setImage: selected ? fSelectedImage : fRegularImage];
}

@end
