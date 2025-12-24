// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

#include <libtransmission/transmission.h>

@interface StatusBarController : NSTitlebarAccessoryViewController<NSMenuItemValidation>

- (instancetype)initWithLib:(tr_session*)lib;

- (void)updateWithDownload:(CGFloat)dlRate upload:(CGFloat)ulRate;

- (void)updateSpeedFieldsToolTips;

@end
