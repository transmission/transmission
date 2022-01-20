// This file Copyright (c) 2007-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#include <libtransmission/transmission.h>

@interface StatsWindowController : NSWindowController
{
    IBOutlet NSTextField* fUploadedField;
    IBOutlet NSTextField* fUploadedAllField;
    IBOutlet NSTextField* fDownloadedField;
    IBOutlet NSTextField* fDownloadedAllField;
    IBOutlet NSTextField* fRatioField;
    IBOutlet NSTextField* fRatioAllField;
    IBOutlet NSTextField* fTimeField;
    IBOutlet NSTextField* fTimeAllField;
    IBOutlet NSTextField* fNumOpenedField;
    IBOutlet NSTextField* fUploadedLabelField;
    IBOutlet NSTextField* fDownloadedLabelField;
    IBOutlet NSTextField* fRatioLabelField;
    IBOutlet NSTextField* fTimeLabelField;
    IBOutlet NSTextField* fNumOpenedLabelField;
    IBOutlet NSButton* fResetButton;
    NSTimer* fTimer;
}

@property(nonatomic, class, readonly) StatsWindowController* statsWindow;

- (void)resetStats:(id)sender;

@end
