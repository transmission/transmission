// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Torrent;

extern CGFloat const kGroupSeparatorHeight;

@interface TorrentTableView : NSOutlineView<NSOutlineViewDelegate, NSAnimationDelegate, NSPopoverDelegate>

- (void)reloadVisibleRows;

- (BOOL)isGroupCollapsed:(NSInteger)value;
- (void)removeCollapsedGroup:(NSInteger)value;
- (void)removeAllCollapsedGroups;
- (void)saveCollapsedGroups;

@property(nonatomic) NSArray<Torrent*>* selectedTorrents;

- (NSRect)iconRectForRow:(NSInteger)row;

- (void)copy:(id)sender;
- (void)paste:(id)sender;

- (void)hoverEventBeganForView:(id)view;
- (void)hoverEventEndedForView:(id)view;

- (void)toggleGroupRowRatio;

- (IBAction)toggleControlForTorrent:(id)sender;

- (IBAction)displayTorrentActionPopover:(id)sender;

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
