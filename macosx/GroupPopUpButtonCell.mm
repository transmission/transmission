// This file Copyright © 2022-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupPopUpButtonCell.h"

static CGFloat const kFrameInset = 2.0;

@implementation GroupPopUpButtonCell

- (void)drawImageWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    NSRect imageFrame = cellFrame;
    imageFrame.origin.x -= kFrameInset;

    [super drawImageWithFrame:imageFrame inView:controlView];
}

- (void)drawTitleWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    NSRect textFrame = cellFrame;
    textFrame.origin.y += kFrameInset / 2;

    [super drawTitleWithFrame:textFrame inView:controlView];
}

@end
