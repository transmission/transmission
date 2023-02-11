// This file Copyright © 2011-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

#include <libtransmission/transmission.h>

@interface GlobalOptionsPopoverViewController : NSViewController

- (instancetype)initWithHandle:(tr_session*)handle;

- (IBAction)updatedDisplayString:(id)sender;

- (IBAction)setDownSpeedSetting:(id)sender;
- (IBAction)setDownSpeedLimit:(id)sender;

- (IBAction)setUpSpeedSetting:(id)sender;
- (IBAction)setUpSpeedLimit:(id)sender;

- (IBAction)setRatioStopSetting:(id)sender;
- (IBAction)setRatioStopLimit:(id)sender;

- (IBAction)setIdleStopSetting:(id)sender;
- (IBAction)setIdleStopLimit:(id)sender;

@end
