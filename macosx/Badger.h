// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@class Torrent;

@interface Badger : NSObject

- (void)updateBadgeWithDownload:(CGFloat)downloadRate upload:(CGFloat)uploadRate;
- (void)addCompletedTorrent:(Torrent*)torrent;
- (void)removeTorrent:(Torrent*)torrent;
- (void)clearCompleted;

@end
