// This file Copyright © 2006-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface MessageWindowController : NSWindowController

- (void)updateLog:(NSTimer*)timer;

- (void)changeLevel:(id)sender;
- (void)changeFilter:(id)sender;
- (void)clearLog:(id)sender;

- (void)writeToFile:(id)sender;

@end
