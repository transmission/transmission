// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@class TorrentCell;
@interface TorrentCellControlButton : NSButton

@property(nonatomic) IBOutlet TorrentCell* torrentCell;
- (void)resetImage;

@end
