// This file Copyright © 2010-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@protocol InfoViewController

- (void)setInfoForTorrents:(NSArray*)torrents;
- (void)updateInfo;

@optional
- (void)clearView;
- (void)saveViewSize;

@end
