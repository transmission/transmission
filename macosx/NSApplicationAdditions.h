// This file Copyright © 2009-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface NSApplication (NSApplicationAdditions)

@property(nonatomic, getter=isDarkMode, readonly) BOOL darkMode;

@end
