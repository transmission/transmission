// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@class TorrentCell;
@interface TorrentCellActionButton : NSButton
@property(nonatomic, weak) IBOutlet TorrentCell* torrentCell;
@end
