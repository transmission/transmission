// This file Copyright © 2007-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupPopUpButtonCell.h"

#define FRAME_INSET 2.0

@implementation GroupPopUpButtonCell

- (void)drawImageWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    NSRect imageFrame = cellFrame;
    imageFrame.origin.x -= FRAME_INSET;

    [super drawImageWithFrame:imageFrame inView:controlView];
}

- (void)drawTitleWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    NSRect textFrame = cellFrame;
    textFrame.origin.y += FRAME_INSET / 2;

    [super drawTitleWithFrame:textFrame inView:controlView];
}

@end
