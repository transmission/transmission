// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCellControlButton.h"
#import "TorrentTableView.h"
#import "Torrent.h"

@interface TorrentCellControlButton ()
@property(nonatomic) NSTrackingArea* fTrackingArea;
@property(nonatomic, copy) NSString* controlImageSuffix;
@property(nonatomic) TorrentTableView* torrentTableView;
@end

@implementation TorrentCellControlButton

- (void)awakeFromNib
{
    self.controlImageSuffix = @"Off";
    [self updateImage];
}

- (void)resetImage
{
    self.controlImageSuffix = @"Off";
    [self updateImage];
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
    self.controlImageSuffix = @"Hover";
    [self updateImage];

    [self.torrentTableView hoverEventBeganForView:self];
}

- (void)mouseExited:(NSEvent*)event
{
    [super mouseExited:event];
    self.controlImageSuffix = @"Off";
    [self updateImage];

    [self.torrentTableView hoverEventEndedForView:self];
}

- (void)mouseDown:(NSEvent*)event
{
    //when filterbar is shown, we need to remove focus otherwise action fails
    [self.window makeFirstResponder:self.torrentTableView];

    [super mouseDown:event];
    self.controlImageSuffix = @"On";
    [self updateImage];

    [self.torrentTableView hoverEventEndedForView:self];
}

- (void)updateImage
{
    [self setupTorrentTableView];

    NSImage* controlImage;
    Torrent* torrent = [self.torrentTableView itemAtRow:[self.torrentTableView rowForView:self]];
    if (torrent.active)
    {
        controlImage = [NSImage imageNamed:[@"Pause" stringByAppendingString:self.controlImageSuffix]];
    }
    else
    {
        if (NSApp.currentEvent.modifierFlags & NSEventModifierFlagOption)
        {
            controlImage = [NSImage imageNamed:[@"ResumeNoWait" stringByAppendingString:self.controlImageSuffix]];
        }
        else if (torrent.waitingToStart)
        {
            controlImage = [NSImage imageNamed:[@"Pause" stringByAppendingString:self.controlImageSuffix]];
        }
        else
        {
            controlImage = [NSImage imageNamed:[@"Resume" stringByAppendingString:self.controlImageSuffix]];
        }
    }
    self.image = controlImage;
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
