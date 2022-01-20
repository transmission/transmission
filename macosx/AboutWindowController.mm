// This file Copyright (c) 2007-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/version.h>

#import "AboutWindowController.h"

@implementation AboutWindowController

AboutWindowController* fAboutBoxInstance = nil;

+ (AboutWindowController*)aboutController
{
    if (!fAboutBoxInstance)
    {
        fAboutBoxInstance = [[self alloc] initWithWindowNibName:@"AboutWindow"];
    }
    return fAboutBoxInstance;
}

- (void)awakeFromNib
{
    fVersionField.stringValue = @(LONG_VERSION_STRING);

    fCopyrightField.stringValue = [NSBundle.mainBundle localizedStringForKey:@"NSHumanReadableCopyright" value:nil
                                                                       table:@"InfoPlist"];

    [fTextView.textStorage setAttributedString:[[NSAttributedString alloc] initWithPath:[NSBundle.mainBundle pathForResource:@"Credits"
                                                                                                                      ofType:@"rtf"]
                                                                     documentAttributes:nil]];

    //size license button
    CGFloat const oldButtonWidth = NSWidth(fLicenseButton.frame);

    fLicenseButton.title = NSLocalizedString(@"License", "About window -> license button");
    [fLicenseButton sizeToFit];

    NSRect buttonFrame = fLicenseButton.frame;
    buttonFrame.size.width += 10.0;
    buttonFrame.origin.x -= NSWidth(buttonFrame) - oldButtonWidth;
    fLicenseButton.frame = buttonFrame;
}

- (void)windowDidLoad
{
    [self.window center];
}

- (void)windowWillClose:(id)sender
{
    fAboutBoxInstance = nil;
}

- (IBAction)showLicense:(id)sender
{
    NSString* licenseText = [NSString stringWithContentsOfFile:[NSBundle.mainBundle pathForResource:@"COPYING" ofType:nil]
                                                  usedEncoding:nil
                                                         error:NULL];
    fLicenseView.string = licenseText;
    fLicenseCloseButton.title = NSLocalizedString(@"OK", "About window -> license close button");

    [self.window beginSheet:fLicenseSheet completionHandler:nil];
}

- (IBAction)hideLicense:(id)sender
{
    [fLicenseSheet orderOut:nil];
    [NSApp endSheet:fLicenseSheet];
}

@end
