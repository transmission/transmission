// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>
#import "TorrentTableView.h"
#import "Torrent.h"

@interface ProgressBarView : NSView

- (void)drawBarInRect:(NSRect)barRect forTableView:(TorrentTableView*)tableView withTorrent:(Torrent*)torrent;

@end
