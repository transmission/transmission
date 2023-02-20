// This file Copyright © 2022-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PriorityPopUpButtonCell.h"

static CGFloat const kFrameInset = 2.0;

@implementation PriorityPopUpButtonCell

- (void)drawTitleWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    NSRect textFrame = cellFrame;
    textFrame.origin.x += 2 * kFrameInset;

    [super drawTitleWithFrame:textFrame inView:controlView];
}

@end
