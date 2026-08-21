// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface TrackerCell : NSActionCell

@end

@class TrackerNode;
@interface TrackerRowView : NSTableCellView

// --- LEFT SIDE (4 levels of data) ---
@property(nonatomic, strong) NSImageView* statusImageView; // Status indicator icon
@property(nonatomic, strong) NSTextField* hostField; // Row 1: Tracker URL / Hostname
@property(nonatomic, strong) NSTextField* announceField; // Row 2: Announce status info
@property(nonatomic, strong) NSTextField* statusField; // Row 3: Current operational status
@property(nonatomic, strong) NSTextField* lastRequestField; // Row 4: Last scrape / request results

// --- RIGHT SIDE (3 levels of statistics) ---
@property(nonatomic, strong) NSTextField* seedersField; // Row 1: Seeders count
@property(nonatomic, strong) NSTextField* leechersField; // Row 2: Leechers count
@property(nonatomic, strong) NSTextField* downloadedField; // Row 3: Total downloaded data amount

- (void)configureWithTrackerNode:(TrackerNode*)node;

@end

@interface TrackerTierRowView : NSTableCellView

@property(nonatomic, strong) NSTextField* tierLabel;

@end
