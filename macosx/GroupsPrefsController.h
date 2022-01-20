// This file Copyright (c) 2007-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@interface GroupsPrefsController : NSObject
{
    IBOutlet NSTableView* fTableView;
    IBOutlet NSSegmentedControl* fAddRemoveControl;

    IBOutlet NSColorWell* fSelectedColorView;
    IBOutlet NSTextField* fSelectedColorNameField;
    IBOutlet NSButton* fCustomLocationEnableCheck;
    IBOutlet NSPopUpButton* fCustomLocationPopUp;

    IBOutlet NSButton* fAutoAssignRulesEnableCheck;
    IBOutlet NSButton* fAutoAssignRulesEditButton;
}

- (void)addRemoveGroup:(id)sender;

- (IBAction)toggleUseCustomDownloadLocation:(id)sender;
- (IBAction)customDownloadLocationSheetShow:(id)sender;

- (IBAction)toggleUseAutoAssignRules:(id)sender;
- (IBAction)orderFrontRulesSheet:(id)sender;
- (IBAction)cancelRules:(id)sender;
- (IBAction)saveRules:(id)sender;
@end
