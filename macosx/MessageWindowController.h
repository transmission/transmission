// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface MessageWindowController : NSWindowController

- (void)updateLog:(NSTimer*)timer;

- (IBAction)changeLevel:(id)sender;
- (IBAction)changeFilter:(id)sender;
- (IBAction)clearLog:(id)sender;

- (IBAction)writeToFile:(id)sender;

@end
