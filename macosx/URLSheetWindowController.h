// This file Copyright © 2011-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface URLSheetWindowController : NSWindowController

@property(nonatomic, readonly) NSString* urlString;

- (instancetype)init;

- (void)openURLEndSheet:(id)sender;
- (void)openURLCancelEndSheet:(id)sender;

@end
