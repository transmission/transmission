// This file Copyright (c) 2010-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#import "InfoViewController.h"

@class PiecesView;
@class Torrent;

@interface InfoActivityViewController : NSViewController<InfoViewController>
{
    NSArray* fTorrents;

    BOOL fSet;

    IBOutlet NSTextField* fDateAddedField;
    IBOutlet NSTextField* fDateCompletedField;
    IBOutlet NSTextField* fDateActivityField;
    IBOutlet NSTextField* fStateField;
    IBOutlet NSTextField* fProgressField;
    IBOutlet NSTextField* fHaveField;
    IBOutlet NSTextField* fDownloadedTotalField;
    IBOutlet NSTextField* fUploadedTotalField;
    IBOutlet NSTextField* fFailedHashField;
    IBOutlet NSTextField* fRatioField;
    IBOutlet NSTextField* fDownloadTimeField;
    IBOutlet NSTextField* fSeedTimeField;
    IBOutlet NSTextView* fErrorMessageView;

    IBOutlet PiecesView* fPiecesView;
    IBOutlet NSSegmentedControl* fPiecesControl;

    //remove when we switch to auto layout on 10.7
    IBOutlet NSTextField* fTransferSectionLabel;
    IBOutlet NSTextField* fDatesSectionLabel;
    IBOutlet NSTextField* fTimeSectionLabel;
    IBOutlet NSTextField* fStateLabel;
    IBOutlet NSTextField* fProgressLabel;
    IBOutlet NSTextField* fHaveLabel;
    IBOutlet NSTextField* fDownloadedLabel;
    IBOutlet NSTextField* fUploadedLabel;
    IBOutlet NSTextField* fFailedDLLabel;
    IBOutlet NSTextField* fRatioLabel;
    IBOutlet NSTextField* fErrorLabel;
    IBOutlet NSTextField* fDateAddedLabel;
    IBOutlet NSTextField* fDateCompletedLabel;
    IBOutlet NSTextField* fDateActivityLabel;
    IBOutlet NSTextField* fDownloadTimeLabel;
    IBOutlet NSTextField* fSeedTimeLabel;
    IBOutlet NSScrollView* fErrorScrollView;
}

- (void)setInfoForTorrents:(NSArray*)torrents;
- (void)updateInfo;

- (IBAction)setPiecesView:(id)sender;
- (IBAction)updatePiecesView:(id)sender;
- (void)clearView;

@end
