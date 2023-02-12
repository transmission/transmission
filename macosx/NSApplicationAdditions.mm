// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "NSApplicationAdditions.h"

@implementation NSApplication (NSApplicationAdditions)

- (BOOL)isDarkMode
{
    return [self.effectiveAppearance.name isEqualToString:NSAppearanceNameDarkAqua];
}

@end
