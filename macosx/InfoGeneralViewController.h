// This file Copyright (c) 2010-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#import "InfoViewController.h"

@interface InfoGeneralViewController : NSViewController<InfoViewController>
{
    NSArray* fTorrents;

    BOOL fSet;

    IBOutlet NSTextField* fPiecesField;
    IBOutlet NSTextField* fHashField;
    IBOutlet NSTextField* fSecureField;
    IBOutlet NSTextField* fDataLocationField;
    IBOutlet NSTextField* fCreatorField;
    IBOutlet NSTextField* fDateCreatedField;

    IBOutlet NSTextView* fCommentView;

    IBOutlet NSButton* fRevealDataButton;

    //remove when we switch to auto layout on 10.7
    IBOutlet NSTextField* fPiecesLabel;
    IBOutlet NSTextField* fHashLabel;
    IBOutlet NSTextField* fSecureLabel;
    IBOutlet NSTextField* fCreatorLabel;
    IBOutlet NSTextField* fDateCreatedLabel;
    IBOutlet NSTextField* fCommentLabel;
    IBOutlet NSTextField* fDataLocationLabel;
    IBOutlet NSTextField* fInfoSectionLabel;
    IBOutlet NSTextField* fWhereSectionLabel;
    IBOutlet NSScrollView* fCommentScrollView;
}

- (void)setInfoForTorrents:(NSArray*)torrents;
- (void)updateInfo;

- (void)revealDataFile:(id)sender;

@end
