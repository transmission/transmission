// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCellActionButton.h"
#import "TorrentTableView.h"
#import "Torrent.h"

@interface TorrentCellActionButton ()
@property(nonatomic) NSTrackingArea* fTrackingArea;
@property(nonatomic) NSImage* fImg;
@property(nonatomic) NSImage* fAltImg;
@property(nonatomic) TorrentTableView* torrentTableView;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;
@end

@implementation TorrentCellActionButton

- (void)awakeFromNib
{
    _fDefaults = NSUserDefaults.standardUserDefaults;
    self.fImg = self.image;

    // hide image by default and show only on hover
    self.fAltImg = [[NSImage alloc] init];
    self.image = self.fAltImg;

    // disable button click highlighting
    [self.cell setHighlightsBy:NSNoCellMask];
}

- (void)display
{
    [super display];
}

- (void)initTorrentTableView
{
    if (!self.torrentTableView)
    {
        self.torrentTableView = (TorrentTableView*)[[[self superview] superview] superview];
    }
}

- (void)mouseEntered:(NSEvent*)event
{
    [super mouseEntered:event];

    self.image = self.fImg;

    [self initTorrentTableView];
    [self.torrentTableView hoverEventBeganForView:self];
}

- (void)mouseExited:(NSEvent*)event
{
    [super mouseExited:event];

    self.image = self.fAltImg;

    [self initTorrentTableView];
    [self.torrentTableView hoverEventEndedForView:self];
}

- (void)mouseDown:(NSEvent*)event
{
    //when filterbar is shown, we need to remove focus otherwise action fails
    [[self window] makeFirstResponder:self.torrentTableView];

    [super mouseDown:event];

    BOOL minimal = [self.fDefaults boolForKey:@"SmallView"];
    if (!minimal)
    {
        [self initTorrentTableView];
        [self.torrentTableView hoverEventEndedForView:self];
    }
}

- (void)updateTrackingAreas
{
    if (self.fTrackingArea != nil)
        [self removeTrackingArea:self.fTrackingArea];

    int opts = (NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways);
    self.fTrackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds] options:opts owner:self userInfo:nil];
    [self addTrackingArea:self.fTrackingArea];
}

@end
