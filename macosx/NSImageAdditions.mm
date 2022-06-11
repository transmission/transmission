// This file Copyright © 2011-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "CocoaCompatibility.h"

#import "NSImageAdditions.h"

@implementation NSImage (NSImageAdditions)

- (NSImage*)imageWithColor:(NSColor*)color
{
    NSImage* coloredImage = [self copy];

    [coloredImage lockFocus];

    [color set];

    NSSize const size = coloredImage.size;
    NSRectFillUsingOperation(NSMakeRect(0.0, 0.0, size.width, size.height), NSCompositingOperationSourceAtop);

    [coloredImage unlockFocus];

    return coloredImage;
}

+ (NSImage*)systemSymbol:(NSString*)symbolName withFallback:(NSString*)fallbackName
{
    if (@available(macOS 11.0, *))
    {
        return [NSImage imageWithSystemSymbolName:symbolName accessibilityDescription:nil];
    }

    return [NSImage imageNamed:fallbackName];
}

+ (NSImage*)largeSystemSymbol:(NSString*)symbolName withFallback:(NSString*)fallbackName
{
#ifdef __MAC_11_0
    if (@available(macOS 11.0, *))
    {
        return [[NSImage imageWithSystemSymbolName:symbolName accessibilityDescription:nil]
            imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithScale:NSImageSymbolScaleLarge]];
    }
#endif

    return [NSImage imageNamed:fallbackName];
}

@end
