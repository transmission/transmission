// This file Copyright © 2011-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface URLSheetWindowController : NSWindowController

@property(nonatomic, readonly) NSString* urlString;

- (instancetype)init;

- (IBAction)openURLEndSheet:(id)sender;
- (IBAction)openURLCancelEndSheet:(id)sender;

@end
