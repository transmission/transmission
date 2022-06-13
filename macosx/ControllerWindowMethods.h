// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "Controller.h"
#import "StatusBarController.h"
#import "FilterBarController.h"
#import "Torrent.h"

@interface Controller (ControllerWindowMethods)

- (void)drawMainWindow;

- (void)setWindowSizeToFit;
- (void)updateForAutoSize;
- (void)setWindowMinMaxToCurrent;

@property(nonatomic, readonly) NSRect sizedWindowFrame;
@property(nonatomic, readonly) CGFloat titlebarHeight;
@property(nonatomic, readonly) CGFloat mainWindowComponentHeight;
@property(nonatomic, readonly) CGFloat scrollViewHeight;
@property(nonatomic, readonly) CGFloat minWindowContentSizeAllowed;
@property(nonatomic, readonly) BOOL isFullScreen;

@end
