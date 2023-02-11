// This file Copyright © 2010-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@class Torrent;

@protocol InfoViewController

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents;
- (void)updateInfo;

@optional
- (void)clearView;
- (void)saveViewSize;

@end
