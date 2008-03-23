/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import "AboutWindowController.h"

@implementation AboutWindowController

AboutWindowController * fAboutBoxInstance = nil;
+ (AboutWindowController *) aboutController
{
    if (!fAboutBoxInstance)
        fAboutBoxInstance = [[self alloc] initWithWindowNibName: @"AboutWindow"];
    return fAboutBoxInstance;
}

- (void) awakeFromNib
{
    NSDictionary * info = [[NSBundle mainBundle] infoDictionary];
    [fVersionField setStringValue: [NSString stringWithFormat: @"%@ (%@)",
        [info objectForKey: @"CFBundleShortVersionString"], [info objectForKey: (NSString *)kCFBundleVersionKey]]];
    
    [fCopyrightField setStringValue: [[NSBundle mainBundle] localizedStringForKey: @"NSHumanReadableCopyright"
                                        value: nil table: @"InfoPlist"]];
    
    [[fTextView textStorage] setAttributedString: [[[NSAttributedString alloc] initWithPath:
            [[NSBundle mainBundle] pathForResource: @"Credits" ofType: @"rtf"] documentAttributes: nil] autorelease]];
    
    //size license button
    float oldButtonWidth = [fLicenseButton frame].size.width;
    
    [fLicenseButton setTitle: NSLocalizedString(@"License", "About window -> license button")];
    [fLicenseButton sizeToFit];
    
    NSRect buttonFrame = [fLicenseButton frame];
    buttonFrame.size.width += 10.0;
    buttonFrame.origin.x -= buttonFrame.size.width - oldButtonWidth;
    [fLicenseButton setFrame: buttonFrame];
}

- (void) windowDidLoad
{
    [[self window] center];
}

- (void) windowWillClose: (id) sender
{
	[fAboutBoxInstance release];
    fAboutBoxInstance = nil;
}

- (IBAction) showLicense: (id) sender
{
    [fLicenseView setString: [NSString stringWithContentsOfFile: [[NSBundle mainBundle] pathForResource: @"LICENSE" ofType: nil]]];
    [fLicenseCloseButton setTitle: NSLocalizedString(@"OK", "About window -> license close button")];
	
	[NSApp beginSheet: fLicenseSheet modalForWindow: [self window] modalDelegate: nil didEndSelector: nil contextInfo: nil];
}

- (IBAction) hideLicense: (id) sender
{
    [fLicenseSheet orderOut: nil];
    [NSApp endSheet: fLicenseSheet];
}

@end
