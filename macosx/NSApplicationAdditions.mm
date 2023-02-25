// This file Copyright © 2009-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "NSApplicationAdditions.h"

@implementation NSApplication (NSApplicationAdditions)

- (BOOL)isDarkMode
{
    if (@available(macOS 10.14, *))
    {
        return [self.effectiveAppearance.name isEqualToString:NSAppearanceNameDarkAqua];
    }
    else
    {
        return NO;
    }
}

@end
