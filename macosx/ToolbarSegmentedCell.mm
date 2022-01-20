// This file Copyright © 2007-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "ToolbarSegmentedCell.h"

@implementation ToolbarSegmentedCell

//when the toolbar is set to small size, don't make the group items small
- (NSControlSize)controlSize
{
    return NSControlSizeRegular;
}

@end
