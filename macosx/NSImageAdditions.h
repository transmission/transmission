// This file Copyright © 2011-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface NSImage (NSImageAdditions)

+ (NSImage*)discIconWithColor:(NSColor*)color insetFactor:(CGFloat)insetFactor;
- (NSImage*)imageWithColor:(NSColor*)color;

/* macOS < 11 compatibility */
+ (NSImage*)systemSymbol:(NSString*)symbolName withFallback:(NSString*)fallbackName;
+ (NSImage*)largeSystemSymbol:(NSString*)symbolName withFallback:(NSString*)fallbackName;

@end
