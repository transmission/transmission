// This file Copyright (c) 2010-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#import "InfoViewController.h"

@interface InfoOptionsViewController : NSViewController<InfoViewController>
{
    NSArray* fTorrents;

    BOOL fSet;

    IBOutlet NSPopUpButton* fPriorityPopUp;
    IBOutlet NSPopUpButton* fRatioPopUp;
    IBOutlet NSPopUpButton* fIdlePopUp;
    IBOutlet NSButton* fUploadLimitCheck;
    IBOutlet NSButton* fDownloadLimitCheck;
    IBOutlet NSButton* fGlobalLimitCheck;
    IBOutlet NSButton* fRemoveSeedingCompleteCheck;
    IBOutlet NSTextField* fUploadLimitField;
    IBOutlet NSTextField* fDownloadLimitField;
    IBOutlet NSTextField* fRatioLimitField;
    IBOutlet NSTextField* fIdleLimitField;
    IBOutlet NSTextField* fUploadLimitLabel;
    IBOutlet NSTextField* fDownloadLimitLabel;
    IBOutlet NSTextField* fIdleLimitLabel;
    IBOutlet NSTextField* fRatioLimitGlobalLabel;
    IBOutlet NSTextField* fIdleLimitGlobalLabel;
    IBOutlet NSTextField* fPeersConnectLabel;
    IBOutlet NSTextField* fPeersConnectField;

    //remove when we switch to auto layout on 10.7
    IBOutlet NSTextField* fTransferBandwidthSectionLabel;
    IBOutlet NSTextField* fPrioritySectionLabel;
    IBOutlet NSTextField* fPriorityLabel;
    IBOutlet NSTextField* fSeedingLimitsSectionLabel;
    IBOutlet NSTextField* fRatioLabel;
    IBOutlet NSTextField* fInactivityLabel;
    IBOutlet NSTextField* fAdvancedSectionLabel;
    IBOutlet NSTextField* fMaxConnectionsLabel;

    NSString* fInitialString;
}

- (void)setInfoForTorrents:(NSArray*)torrents;
- (void)updateInfo;
- (void)updateOptions;

- (IBAction)setUseSpeedLimit:(id)sender;
- (IBAction)setSpeedLimit:(id)sender;
- (IBAction)setUseGlobalSpeedLimit:(id)sender;

- (IBAction)setRatioSetting:(id)sender;
- (IBAction)setRatioLimit:(id)sender;

- (IBAction)setIdleSetting:(id)sender;
- (IBAction)setIdleLimit:(id)sender;

- (IBAction)setRemoveWhenSeedingCompletes:(id)sender;

- (IBAction)setPriority:(id)sender;

- (IBAction)setPeersConnectLimit:(id)sender;

@end
