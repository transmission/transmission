// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#include <libtransmission/transmission.h>

@class Controller;
@class Torrent;
@class TorrentCell;

#define GROUP_SEPARATOR_HEIGHT 18.0

@interface TorrentTableView : NSOutlineView<NSOutlineViewDelegate, NSAnimationDelegate, NSPopoverDelegate>
{
    IBOutlet Controller* fController;

    TorrentCell* fTorrentCell;

    NSUserDefaults* fDefaults;

    NSMutableIndexSet* fCollapsedGroups;

    IBOutlet NSMenu* fContextRow;
    IBOutlet NSMenu* fContextNoRow;

    NSInteger fMouseRow;
    NSInteger fMouseControlRow;
    NSInteger fMouseRevealRow;
    NSInteger fMouseActionRow;
    NSArray* fSelectedValues;

    IBOutlet NSMenu* fActionMenu;
    IBOutlet NSMenu* fUploadMenu;
    IBOutlet NSMenu* fDownloadMenu;
    IBOutlet NSMenu* fRatioMenu;
    IBOutlet NSMenu* fPriorityMenu;
    IBOutlet NSMenuItem* fGlobalLimitItem;
    Torrent* fMenuTorrent;

    CGFloat fPiecesBarPercent;
    NSAnimation* fPiecesBarAnimation;

    BOOL fActionPopoverShown;
}

- (BOOL)isGroupCollapsed:(NSInteger)value;
- (void)removeCollapsedGroup:(NSInteger)value;
- (void)removeAllCollapsedGroups;
- (void)saveCollapsedGroups;

- (void)removeTrackingAreas;
- (void)setRowHover:(NSInteger)row;
- (void)setControlButtonHover:(NSInteger)row;
- (void)setRevealButtonHover:(NSInteger)row;
- (void)setActionButtonHover:(NSInteger)row;

- (void)selectValues:(NSArray*)values;
@property(nonatomic, readonly) NSArray* selectedValues;
@property(nonatomic, readonly) NSArray* selectedTorrents;

- (NSRect)iconRectForRow:(NSInteger)row;

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
