// This file Copyright © 2010-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#import "InfoViewController.h"

@class Torrent;
@class TrackerTableView;
@class TrackerCell;

@interface InfoTrackersViewController : NSViewController<InfoViewController>
{
    NSArray* fTorrents;

    BOOL fSet;

    NSMutableArray* fTrackers;

    IBOutlet TrackerTableView* fTrackerTable;
    TrackerCell* fTrackerCell;

    IBOutlet NSSegmentedControl* fTrackerAddRemoveControl;
}

- (void)setInfoForTorrents:(NSArray*)torrents;
- (void)updateInfo;

- (void)saveViewSize;
- (void)clearView;

- (void)addRemoveTracker:(id)sender;

@end
