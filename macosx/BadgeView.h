// This file Copyright © 2007-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#include <libtransmission/transmission.h>

@interface BadgeView : NSView

- (instancetype)initWithLib:(tr_session*)lib;

- (BOOL)setRatesWithDownload:(CGFloat)downloadRate upload:(CGFloat)uploadRate;

@end
