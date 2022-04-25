// This file Copyright © 2010-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#import "InfoViewController.h"

@interface InfoGeneralViewController : NSViewController<InfoViewController>

- (void)setInfoForTorrents:(NSArray<Torrent*>*)torrents;
- (void)updateInfo;

- (void)revealDataFile:(id)sender;

@end
