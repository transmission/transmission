/******************************************************************************
 * Copyright (c) 2007-2012 Transmission authors and contributors
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
#import "NSStringAdditions.h"

@implementation PeerProgressIndicatorCell

- (id) copyWithZone: (NSZone *) zone
{
    PeerProgressIndicatorCell * copy = [super copyWithZone: zone];
    copy->fAttributes = fAttributes;

    return copy;
}


- (void) setSeed: (BOOL) seed
{
    fSeed = seed;
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    if ([[NSUserDefaults standardUserDefaults] boolForKey: @"DisplayPeerProgressBarNumber"])
    {
        if (!fAttributes)
        {
            NSMutableParagraphStyle * paragraphStyle = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
            [paragraphStyle setAlignment: NSRightTextAlignment];

            fAttributes = @{NSFontAttributeName: [NSFont systemFontOfSize: 11.0],
                            NSForegroundColorAttributeName: NSColor.labelColor,
                            NSParagraphStyleAttributeName: paragraphStyle};
        }

        [[NSString percentString: [self floatValue] longDecimals: NO] drawInRect: cellFrame withAttributes: fAttributes];
    }
    else
    {
        //attributes not needed anymore
        if (fAttributes)
        {
            fAttributes = nil;
        }

        [super drawWithFrame: cellFrame inView: controlView];
        if (fSeed)
        {
            NSImage * checkImage = [NSImage imageNamed: @"CompleteCheck"];

            const NSSize imageSize = [checkImage size];
            const NSRect rect = NSMakeRect(floor(NSMidX(cellFrame) - imageSize.width * 0.5),
                                            floor(NSMidY(cellFrame) - imageSize.height * 0.5),
                                            imageSize.width, imageSize.height);

            [checkImage drawInRect: rect fromRect: NSZeroRect operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];
        }
    }
}

@end
