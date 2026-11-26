// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Controller;

@interface MenuBarItem : NSObject

@property(nonatomic, getter=isVisible, readonly) BOOL visible;

- (void)showWithController:(Controller*)controller;
- (void)hide;
- (void)updateWithDownload:(CGFloat)downloadRate upload:(CGFloat)uploadRate;

@end
