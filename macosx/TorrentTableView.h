// This file Copyright © 2005-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Torrent;

extern const CGFloat kGroupSeparatorHeight;

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

- (IBAction)setQuickLimitMode:(id)sender;
- (void)setQuickLimit:(id)sender;
- (IBAction)setGlobalLimit:(id)sender;

- (IBAction)setQuickRatioMode:(id)sender;
- (void)setQuickRatio:(id)sender;

- (IBAction)setPriority:(id)sender;

- (void)togglePiecesBar;
@property(nonatomic, readonly) CGFloat piecesBarPercent;

- (void)selectAndScrollToRow:(NSInteger)row;

@end
