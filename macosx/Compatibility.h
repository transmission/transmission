//
//  Compatibility.h
//  Transmission
//
//  Created by Antoine Coeur on 31/03/2022.
//  Copyright © 2022 The Transmission Project. All rights reserved.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Compatibility declarations to build `@available(macOS 11.0, *)` code with older Xcode 11.3.1 (the last 32-bit OS compatible Xcode)
#ifndef __MAC_11_0

typedef NS_ENUM(NSInteger, NSImageSymbolScale)
{
    NSImageSymbolScaleLarge = 3,
} API_AVAILABLE(macos(11.0));

API_AVAILABLE(macos(11.0))
@interface NSImageSymbolConfiguration : NSObject<NSCopying, NSSecureCoding>
+ (instancetype)configurationWithScale:(NSImageSymbolScale)scale;
@end

@interface NSImage ()
+ (nullable instancetype)imageWithSystemSymbolName:(NSString*)symbolName
                          accessibilityDescription:(nullable NSString*)description API_AVAILABLE(macos(11.0));
- (nullable NSImage*)imageWithSymbolConfiguration:(NSImageSymbolConfiguration*)configuration API_AVAILABLE(macos(11.0));
@end

typedef NS_ENUM(NSInteger, NSWindowToolbarStyle)
{
    NSWindowToolbarStylePreference = 2,
} API_AVAILABLE(macos(11.0));

@interface NSWindow ()
@property NSWindowToolbarStyle toolbarStyle API_AVAILABLE(macos(11.0));
@end

typedef NS_ENUM(NSInteger, NSTableViewStyle)
{
    NSTableViewStyleFullWidth = 1,
} API_AVAILABLE(macos(11.0));

@interface NSTableView ()
@property NSTableViewStyle style API_AVAILABLE(macos(11.0));
@end

#endif

NS_ASSUME_NONNULL_END
