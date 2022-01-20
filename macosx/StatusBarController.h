// This file Copyright © 2011-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#include <libtransmission/transmission.h>

@interface StatusBarController : NSViewController
{
    IBOutlet NSButton* fStatusButton;
    IBOutlet NSTextField* fTotalDLField;
    IBOutlet NSTextField* fTotalULField;
    IBOutlet NSImageView* fTotalDLImageView;
    IBOutlet NSImageView* fTotalULImageView;

    tr_session* fLib;

    CGFloat fPreviousDownloadRate;
    CGFloat fPreviousUploadRate;
}

- (instancetype)initWithLib:(tr_session*)lib;

- (void)updateWithDownload:(CGFloat)dlRate upload:(CGFloat)ulRate;
- (void)setStatusLabel:(id)sender;

- (void)updateSpeedFieldsToolTips;

@end
