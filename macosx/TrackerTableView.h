// This file Copyright (c) 2008-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@class Torrent;

@interface TrackerTableView : NSTableView
{
    //weak references
    Torrent* fTorrent;
    NSArray* fTrackers;
}

- (void)setTorrent:(Torrent*)torrent;
- (void)setTrackers:(NSArray*)trackers;

- (void)copy:(id)sender;
- (void)paste:(id)sender;

@end
