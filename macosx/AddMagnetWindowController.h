// This file Copyright © 2010-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Controller;
@class Torrent;

@interface AddMagnetWindowController : NSWindowController

@property(nonatomic, readonly) Torrent* torrent;

- (instancetype)initWithTorrent:(Torrent*)torrent destination:(NSString*)path controller:(Controller*)controller;

- (IBAction)setDestination:(id)sender;

- (IBAction)add:(id)sender;
- (IBAction)cancelAdd:(id)sender;

- (IBAction)changePriority:(id)sender;

- (void)updateGroupMenu:(NSNotification*)notification;

@end
