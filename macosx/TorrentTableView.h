/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2012 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import <Cocoa/Cocoa.h>
#import <transmission.h>

@class Controller;
@class Torrent;
@class TorrentCell;

#define GROUP_SEPARATOR_HEIGHT 18.0

@interface TorrentTableView : NSOutlineView <NSOutlineViewDelegate, NSAnimationDelegate, NSPopoverDelegate>
{
    IBOutlet Controller * fController;
    
    TorrentCell * fTorrentCell;
    
    NSUserDefaults * fDefaults;
    
    NSMutableIndexSet * fCollapsedGroups;
    
    IBOutlet NSMenu * fContextRow, * fContextNoRow;
    
    NSInteger fMouseRow, fMouseControlRow, fMouseRevealRow, fMouseActionRow;
    NSArray * fSelectedValues;
    
    IBOutlet NSMenu * fActionMenu, * fUploadMenu, * fDownloadMenu, * fRatioMenu, * fPriorityMenu;
    IBOutlet NSMenuItem * fGlobalLimitItem;
    Torrent * fMenuTorrent;
    
    CGFloat fPiecesBarPercent;
    NSAnimation * fPiecesBarAnimation;
    
    BOOL fActionPopoverShown;
}

- (BOOL) isGroupCollapsed: (NSInteger) value;
- (void) removeCollapsedGroup: (NSInteger) value;
- (void) removeAllCollapsedGroups;
- (void) saveCollapsedGroups;

- (void) removeTrackingAreas;
- (void) setRowHover: (NSInteger) row;
- (void) setControlButtonHover: (NSInteger) row;
- (void) setRevealButtonHover: (NSInteger) row;
- (void) setActionButtonHover: (NSInteger) row;

- (void) selectValues: (NSArray *) values;
- (NSArray *) selectedValues;
- (NSArray *) selectedTorrents;

- (NSRect) iconRectForRow: (NSInteger) row;

- (void) paste: (id) sender;

- (void) toggleControlForTorrent: (Torrent *) torrent;

- (void) displayTorrentActionPopoverForEvent: (NSEvent *) event;

- (void) setQuickLimitMode: (id) sender;
- (void) setQuickLimit: (id) sender;
- (void) setGlobalLimit: (id) sender;

- (void) setQuickRatioMode: (id) sender;
- (void) setQuickRatio: (id) sender;

- (void) setPriority: (id) sender;

- (void) togglePiecesBar;
- (CGFloat) piecesBarPercent;

- (void) selectAndScrollToRow: (NSInteger) row;

@end
