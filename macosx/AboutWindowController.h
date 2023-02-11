// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface AboutWindowController : NSWindowController

@property(nonatomic, class, readonly) AboutWindowController* aboutController;

- (IBAction)showLicense:(id)sender;
- (IBAction)hideLicense:(id)sender;

@end
