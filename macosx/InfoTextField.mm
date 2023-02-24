// This file Copyright © 2009-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "InfoTextField.h"

@implementation InfoTextField

- (void)setStringValue:(NSString*)string
{
    super.stringValue = string;

    self.selectable = ![self.stringValue isEqualToString:@""];
}

- (void)setObjectValue:(id<NSCopying>)object
{
    super.objectValue = object;

    self.selectable = ![self.stringValue isEqualToString:@""];
}

@end
