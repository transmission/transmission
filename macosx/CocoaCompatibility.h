// This file Copyright © 2022-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

NS_ASSUME_NONNULL_BEGIN

// Compatibility declarations to build `@available(macOS 11.0, *)` code with older Xcode 11.3.1 (the last 32-bit OS compatible Xcode)
#ifndef __MAC_11_0

typedef NS_ENUM(NSInteger, NSImageSymbolScale) {
    NSImageSymbolScaleLarge = 3,
} API_AVAILABLE(macos(11.0));

@interface NSImage ()
+ (nullable instancetype)imageWithSystemSymbolName:(NSString*)symbolName
                          accessibilityDescription:(nullable NSString*)description API_AVAILABLE(macos(11.0));
@end

typedef NS_ENUM(NSInteger, NSWindowToolbarStyle) {
    NSWindowToolbarStylePreference = 2,
    NSWindowToolbarStyleUnified = 3,
} API_AVAILABLE(macos(11.0));

@interface NSWindow ()
@property NSWindowToolbarStyle toolbarStyle API_AVAILABLE(macos(11.0));
@end

typedef NS_ENUM(NSInteger, NSTableViewStyle) {
    NSTableViewStyleFullWidth = 1,
} API_AVAILABLE(macos(11.0));

@interface NSTableView ()
@property NSTableViewStyle style API_AVAILABLE(macos(11.0));
@end

#endif

// Compatibility declarations to build `@available(macOS 13.0, *)` code with older Xcode 11.3.1 (the last 32-bit OS compatible Xcode)
#ifndef __MAC_13_0

typedef NS_ENUM(NSInteger, NSColorWellStyle) {
    NSColorWellStyleMinimal = 1,
} API_AVAILABLE(macos(13.0));

@interface NSColorWell ()
@property(assign) NSColorWellStyle colorWellStyle API_AVAILABLE(macos(13.0));
@end

#endif

NS_ASSUME_NONNULL_END
