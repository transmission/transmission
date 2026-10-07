// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class TorrentTableView;
@class Torrent;

@interface ProgressBarView : NSView

@property(class, nonatomic, readonly) ProgressBarView* sharedInstance;

- (void)drawBarInRect:(NSRect)barRect forTableView:(TorrentTableView*)tableView withTorrent:(Torrent*)torrent;

@end
