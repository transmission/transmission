// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCellActionButton.h"
#import "TorrentTableView.h"
#import "Torrent.h"

@interface TorrentCellActionButton ()
@property(nonatomic) NSTrackingArea* fTrackingArea;
@property(nonatomic) NSImage* fImage;
@property(nonatomic) NSImage* fAlternativeImage;
@property(nonatomic) TorrentTableView* torrentTableView;
@property(nonatomic) NSUserDefaults* fDefaults;
@end

@implementation TorrentCellActionButton

- (void)awakeFromNib
{
    self.fDefaults = NSUserDefaults.standardUserDefaults;
    self.fImage = self.image;

    // hide image by default and show only on hover
    self.fAlternativeImage = [[NSImage alloc] init];
    self.image = self.fAlternativeImage;

    // disable button click highlighting
    [self.cell setHighlightsBy:NSNoCellMask];
}

- (void)setupTorrentTableView
{
    if (!self.torrentTableView)
    {
        self.torrentTableView = (TorrentTableView*)[[[self superview] superview] superview];
    }
}

- (void)mouseEntered:(NSEvent*)event
{
    [super mouseEntered:event];

    self.image = self.fImage;

    [self setupTorrentTableView];
    [self.torrentTableView hoverEventBeganForView:self];
}

- (void)mouseExited:(NSEvent*)event
{
    [super mouseExited:event];

    self.image = self.fAlternativeImage;

    [self setupTorrentTableView];
    [self.torrentTableView hoverEventEndedForView:self];
}

- (void)mouseDown:(NSEvent*)event
{
    //when filterbar is shown, we need to remove focus otherwise action fails
    [self.window makeFirstResponder:self.torrentTableView];

    [super mouseDown:event];

    BOOL minimal = [self.fDefaults boolForKey:@"SmallView"];
    if (!minimal)
    {
        [self setupTorrentTableView];
        [self.torrentTableView hoverEventEndedForView:self];
    }
}

- (void)updateTrackingAreas
{
    if (self.fTrackingArea != nil)
    {
        [self removeTrackingArea:self.fTrackingArea];
    }

    NSTrackingAreaOptions opts = (NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways);
    self.fTrackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds options:opts owner:self userInfo:nil];
    [self addTrackingArea:self.fTrackingArea];
}

@end
