// This file Copyright © 2006-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#include <libtransmission/transmission.h>

@class Torrent;

@interface Badger : NSObject

- (instancetype)initWithLib:(tr_session*)lib;

- (void)updateBadgeWithDownload:(CGFloat)downloadRate upload:(CGFloat)uploadRate;
- (void)addCompletedTorrent:(Torrent*)torrent;
- (void)removeTorrent:(Torrent*)torrent;
- (void)clearCompleted;

@end
