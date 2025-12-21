// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Torrent;

@interface TrackerTableView : NSTableView<NSMenuItemValidation>

@property(nonatomic, weak) Torrent* torrent;
@property(nonatomic, weak) NSArray* trackers;

- (void)copy:(id)sender;
- (void)paste:(id)sender;

@end
