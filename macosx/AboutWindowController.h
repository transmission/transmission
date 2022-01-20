// This file Copyright © 2007-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@interface AboutWindowController : NSWindowController
{
    IBOutlet NSTextView* fTextView;
    IBOutlet NSTextView* fLicenseView;
    IBOutlet NSTextField* fVersionField;
    IBOutlet NSTextField* fCopyrightField;
    IBOutlet NSButton* fLicenseButton;
    IBOutlet NSButton* fLicenseCloseButton;
    IBOutlet NSPanel* fLicenseSheet;
}

@property(nonatomic, class, readonly) AboutWindowController* aboutController;

- (IBAction)showLicense:(id)sender;
- (IBAction)hideLicense:(id)sender;

@end
