// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupCell.h"

@implementation GroupCell

- (void)setBackgroundStyle:(NSBackgroundStyle)backgroundStyle
{
    [super setBackgroundStyle:backgroundStyle];

    __auto_type isEmphasized = backgroundStyle == NSBackgroundStyleEmphasized;
    self.fGroupTitleField.textColor = isEmphasized ? NSColor.labelColor : NSColor.secondaryLabelColor;
}

@end
