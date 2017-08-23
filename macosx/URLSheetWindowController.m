/******************************************************************************
 * Copyright (c) 2011-2012 Transmission authors and contributors
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

#import "URLSheetWindowController.h"
#import "Controller.h"

@interface URLSheetWindowController (Private)

- (void) updateOpenButtonForURL: (NSString *) string;

@end

@implementation URLSheetWindowController

NSString * urlString = nil;

- (id) initWithController: (Controller *) controller
{
    if ((self = [self initWithWindowNibName: @"URLSheetWindow"]))
    {
        fController = controller;
    }
    return self;
}

- (void) awakeFromNib
{
    [fLabelField setStringValue: NSLocalizedString(@"Internet address of torrent file:", "URL sheet label")];

    if (urlString)
    {
        [fTextField setStringValue: urlString];
        [fTextField selectText: self];

        [self updateOpenButtonForURL: urlString];
    }

    [fOpenButton setTitle: NSLocalizedString(@"Open", "URL sheet button")];
    [fCancelButton setTitle: NSLocalizedString(@"Cancel", "URL sheet button")];

    [fOpenButton sizeToFit];
    [fCancelButton sizeToFit];

    //size the two buttons the same
    NSRect openFrame = [fOpenButton frame];
    openFrame.size.width += 10.0;
    NSRect cancelFrame = [fCancelButton frame];
    cancelFrame.size.width += 10.0;

    if (NSWidth(openFrame) > NSWidth(cancelFrame))
        cancelFrame.size.width = NSWidth(openFrame);
    else
        openFrame.size.width = NSWidth(cancelFrame);

    openFrame.origin.x = NSWidth([[self window] frame]) - NSWidth(openFrame) - 20.0 + 6.0; //I don't know why the extra 6.0 is needed
    [fOpenButton setFrame: openFrame];

    cancelFrame.origin.x = NSMinX(openFrame) - NSWidth(cancelFrame);
    [fCancelButton setFrame: cancelFrame];
}

- (void) openURLEndSheet: (id) sender
{
    [[self window] orderOut: sender];
    [NSApp endSheet: [self window] returnCode: 1];
}

- (void) openURLCancelEndSheet: (id) sender
{
    [[self window] orderOut: sender];
    [NSApp endSheet: [self window] returnCode: 0];
}

- (NSString *) urlString
{
    return [fTextField stringValue];
}

- (void) controlTextDidChange: (NSNotification *) notification
{
    [self updateOpenButtonForURL: [fTextField stringValue]];
}

@end

@implementation URLSheetWindowController (Private)

- (void) updateOpenButtonForURL: (NSString *) string
{
    BOOL enable = YES;
    if ([string isEqualToString: @""])
        enable = NO;
    else
    {
        NSRange prefixRange = [string rangeOfString: @"://"];
        if (prefixRange.location != NSNotFound && [string length] == NSMaxRange(prefixRange))
            enable = NO;
    }

    [fOpenButton setEnabled: enable];
}

@end
