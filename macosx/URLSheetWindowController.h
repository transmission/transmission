// This file Copyright © 2011-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@class Controller;

@interface URLSheetWindowController : NSWindowController
{
    IBOutlet NSTextField* fLabelField;
    IBOutlet NSTextField* fTextField;
    IBOutlet NSButton* fOpenButton;
    IBOutlet NSButton* fCancelButton;

    Controller* fController;
}

- (instancetype)initWithController:(Controller*)controller;

- (void)openURLEndSheet:(id)sender;
- (void)openURLCancelEndSheet:(id)sender;

@property(nonatomic, readonly) NSString* urlString;

@end
