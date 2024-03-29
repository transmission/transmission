// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "ColorTextField.h"

@implementation ColorTextField

- (void)awakeFromNib
{
    [super awakeFromNib];

    self.enabled = self.enabled;
}

- (void)setEnabled:(BOOL)flag
{
    super.enabled = flag;

    NSColor* color = flag ? NSColor.controlTextColor : NSColor.disabledControlTextColor;
    self.textColor = color;
}

@end
