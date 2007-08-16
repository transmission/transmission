/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Transmission authors and contributors
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

#warning make completely localized
- (void) windowDidLoad
{
    NSDictionary * info = [[NSBundle mainBundle] infoDictionary];
    [fVersionField setStringValue: [NSString stringWithFormat: @"%@ (%@)",
        [info objectForKey: @"CFBundleShortVersionString"], [info objectForKey: (NSString *)kCFBundleVersionKey]]];
    
    [fCopyrightField setStringValue: [[NSBundle mainBundle] localizedStringForKey: @"NSHumanReadableCopyright"
                                        value: nil table: @"InfoPlist"]];
    
    [[fTextView textStorage] setAttributedString: [[[NSAttributedString alloc] initWithPath:
            [[NSBundle mainBundle] pathForResource: @"Credits" ofType: @"rtf"] documentAttributes: nil] autorelease]];

    [[self window] center];
}

- (void) windowWillClose: (id)sender
{
	[fAboutBoxInstance release];
    fAboutBoxInstance = nil;
}

- (IBAction) showLicense: (id) sender
{
	[fLicenseView setString: [NSString stringWithContentsOfFile: [[NSBundle mainBundle] pathForResource: @"LICENSE" ofType: nil]]];
	
	[NSApp beginSheet: fLicenseSheet modalForWindow: [self window] modalDelegate: nil didEndSelector: nil contextInfo: nil];
}

- (IBAction) hideLicense: (id) sender
{
    [fLicenseSheet orderOut: nil];
    [NSApp endSheet: fLicenseSheet];
}

@end
