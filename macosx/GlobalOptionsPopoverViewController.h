// This file Copyright (c) 2011-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#include <libtransmission/transmission.h>

@interface GlobalOptionsPopoverViewController : NSViewController
{
    tr_session* fHandle;
    NSUserDefaults* fDefaults;

    IBOutlet NSTextField* fUploadLimitField;
    IBOutlet NSTextField* fDownloadLimitField;

    IBOutlet NSTextField* fRatioStopField;
    IBOutlet NSTextField* fIdleStopField;

    NSString* fInitialString;
}

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
