// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#include <libtransmission/transmission.h>

@class Torrent;

#define GROUP_SEPARATOR_HEIGHT 18.0

@interface TorrentTableView : NSOutlineView<NSOutlineViewDelegate, NSAnimationDelegate, NSPopoverDelegate>

- (BOOL)isGroupCollapsed:(NSInteger)value;
- (void)removeCollapsedGroup:(NSInteger)value;
- (void)removeAllCollapsedGroups;
- (void)saveCollapsedGroups;

- (void)removeTrackingAreas;
@property(nonatomic) NSInteger hoverRow;
@property(nonatomic) NSInteger controlButtonHoverRow;
@property(nonatomic) NSInteger revealButtonHoverRow;
@property(nonatomic) NSInteger actionButtonHoverRow;

- (void)selectValues:(NSArray*)values;
@property(nonatomic, readonly) NSArray* selectedValues;
@property(nonatomic, readonly) NSArray<Torrent*>* selectedTorrents;

- (NSRect)iconRectForRow:(NSInteger)row;

- (void)copy:(id)sender;
- (void)paste:(id)sender;

- (void)toggleControlForTorrent:(Torrent*)torrent;

- (void)displayTorrentActionPopoverForEvent:(NSEvent*)event;

- (void)setQuickLimitMode:(id)sender;
- (void)setQuickLimit:(id)sender;
- (void)setGlobalLimit:(id)sender;

- (void)setQuickRatioMode:(id)sender;
- (void)setQuickRatio:(id)sender;

- (void)setPriority:(id)sender;

- (void)togglePiecesBar;
@property(nonatomic, readonly) CGFloat piecesBarPercent;

- (void)selectAndScrollToRow:(NSInteger)row;

@end
