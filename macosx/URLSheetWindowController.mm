// This file Copyright © 2011-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "URLSheetWindowController.h"
#import "Controller.h"

@interface URLSheetWindowController ()

@property(nonatomic) IBOutlet NSTextField* fLabelField;
@property(nonatomic) IBOutlet NSTextField* fTextField;
@property(nonatomic) IBOutlet NSButton* fOpenButton;
@property(nonatomic) IBOutlet NSButton* fCancelButton;

@end

@implementation URLSheetWindowController

- (instancetype)init
{
    self = [self initWithWindowNibName:@"URLSheetWindow"];
    return self;
}

- (void)awakeFromNib
{
    self.fLabelField.stringValue = NSLocalizedString(@"Internet address of torrent file:", "URL sheet label");
    self.fOpenButton.title = NSLocalizedString(@"Open", "URL sheet button");
    self.fCancelButton.title = NSLocalizedString(@"Cancel", "URL sheet button");

    [self.fOpenButton sizeToFit];
    [self.fCancelButton sizeToFit];

    //size the two buttons the same
    NSRect openFrame = self.fOpenButton.frame;
    openFrame.size.width += 10.0;
    NSRect cancelFrame = self.fCancelButton.frame;
    cancelFrame.size.width += 10.0;

    if (NSWidth(openFrame) > NSWidth(cancelFrame))
    {
        cancelFrame.size.width = NSWidth(openFrame);
    }
    else
    {
        openFrame.size.width = NSWidth(cancelFrame);
    }

    openFrame.origin.x = NSWidth(self.window.frame) - NSWidth(openFrame) - 20.0 + 6.0; //I don't know why the extra 6.0 is needed
    self.fOpenButton.frame = openFrame;

    cancelFrame.origin.x = NSMinX(openFrame) - NSWidth(cancelFrame);
    self.fCancelButton.frame = cancelFrame;
}

- (void)openURLEndSheet:(id)sender
{
    [NSApp endSheet:self.window returnCode:1];
}

- (void)openURLCancelEndSheet:(id)sender
{
    [NSApp endSheet:self.window returnCode:0];
}

- (NSString*)urlString
{
    return self.fTextField.stringValue;
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    [self updateOpenButtonForURL:self.fTextField.stringValue];
}

#pragma mark - Private

- (void)updateOpenButtonForURL:(NSString*)string
{
    BOOL enable = YES;
    if ([string isEqualToString:@""])
    {
        enable = NO;
    }
    else
    {
        NSRange prefixRange = [string rangeOfString:@"://"];
        if (prefixRange.location != NSNotFound && string.length == NSMaxRange(prefixRange))
        {
            enable = NO;
        }
    }

    self.fOpenButton.enabled = enable;
}

@end
