// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "SmallTorrentCell.h"
#import "ProgressBarView.h"
#import "ProgressGradients.h"
#import "TorrentTableView.h"
#import "Torrent.h"

@interface SmallTorrentCell ()
@property(nonatomic) NSTrackingArea* fTrackingArea;
@end

@implementation SmallTorrentCell

// Layout
- (void)setupConstraints
{
    __auto_type groupIndicatorView = self.fGroupIndicatorView;
    __auto_type iconView = self.fIconView;
    __auto_type actionButton = self.fActionButton;
    __auto_type stackView = self.fStackView;
    __auto_type torrentStatusField = self.fTorrentStatusField;
    __auto_type torrentProgressBarView = self.fTorrentProgressBarView;
    __auto_type controlButton = self.fControlButton;
    __auto_type revealButton = self.fRevealButton;

    for (NSView* view in @[
             groupIndicatorView,
             iconView,
             actionButton,
             torrentProgressBarView,
             stackView,
             torrentStatusField,
             controlButton,
             revealButton,
         ])
    {
        [self addSubview:view];
    }

    [NSLayoutConstraint activateConstraints:@[
        // groupIndicatorView
        [groupIndicatorView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [groupIndicatorView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [groupIndicatorView.widthAnchor constraintEqualToConstant:6],
        [groupIndicatorView.heightAnchor constraintEqualToConstant:6],

        // iconView
        [iconView.leadingAnchor constraintEqualToAnchor:groupIndicatorView.trailingAnchor constant:8],
        [iconView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [iconView.widthAnchor constraintEqualToConstant:16],
        [iconView.heightAnchor constraintEqualToConstant:16],

        // actionButton
        [actionButton.centerXAnchor constraintEqualToAnchor:iconView.centerXAnchor],
        [actionButton.centerYAnchor constraintEqualToAnchor:iconView.centerYAnchor],
        [actionButton.widthAnchor constraintEqualToConstant:16],
        [actionButton.heightAnchor constraintEqualToConstant:16],

        // torrentProgressBarView
        [torrentProgressBarView.leadingAnchor constraintEqualToAnchor:iconView.trailingAnchor constant:15],
        [torrentProgressBarView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-5],
        [torrentProgressBarView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [torrentProgressBarView.heightAnchor constraintEqualToConstant:18],

        // stackView
        [stackView.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.leadingAnchor],
        [stackView.topAnchor constraintEqualToAnchor:torrentProgressBarView.topAnchor],
        [stackView.bottomAnchor constraintEqualToAnchor:torrentProgressBarView.bottomAnchor],

        // torrentStatusField
        [torrentStatusField.leadingAnchor constraintEqualToAnchor:stackView.trailingAnchor],
        [torrentStatusField.trailingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor constant:-3],
        [torrentStatusField.centerYAnchor constraintEqualToAnchor:torrentProgressBarView.centerYAnchor],

        // controlButton
        [controlButton.centerYAnchor constraintEqualToAnchor:torrentProgressBarView.centerYAnchor],
        [controlButton.widthAnchor constraintEqualToConstant:14],
        [controlButton.heightAnchor constraintEqualToConstant:14],

        // revealButton
        [revealButton.leadingAnchor constraintEqualToAnchor:controlButton.trailingAnchor constant:3],
        [revealButton.trailingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor constant:-3],
        [revealButton.centerYAnchor constraintEqualToAnchor:controlButton.centerYAnchor],
        [revealButton.widthAnchor constraintEqualToConstant:14],
        [revealButton.heightAnchor constraintEqualToConstant:14],
    ]];
}

// show fControlButton and fRevealButton
- (void)mouseEntered:(NSEvent*)event
{
    [super mouseEntered:event];

    NSPoint mouseLocation = [self convertPoint:[event locationInWindow] fromView:nil];
    if (NSPointInRect(mouseLocation, self.fTrackingArea.rect))
    {
        [self.fTorrentTableView hoverEventBeganForView:self];
    }
}

- (void)mouseExited:(NSEvent*)event
{
    [super mouseExited:event];

    NSPoint mouseLocation = [self convertPoint:[event locationInWindow] fromView:nil];
    if (!NSPointInRect(mouseLocation, self.fTrackingArea.rect))
    {
        [self.fTorrentTableView hoverEventEndedForView:self];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    [super mouseUp:event];
    [self updateTrackingAreas];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];

    if (self.fTrackingArea != nil)
    {
        [self removeTrackingArea:self.fTrackingArea];
    }

    NSRect rect = self.bounds;

    NSTrackingAreaOptions opts = (NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow);
    self.fTrackingArea = [[NSTrackingArea alloc] initWithRect:rect options:opts owner:self userInfo:nil];
    [self addTrackingArea:self.fTrackingArea];

    //check to see if mouse is already within rect
    NSPoint mouseLocation = [self.window mouseLocationOutsideOfEventStream];
    mouseLocation = [self.superview convertPoint:mouseLocation fromView:nil];

    if (NSPointInRect(mouseLocation, rect))
    {
        [self mouseEntered:[[NSEvent alloc] init]];
    }
    else
    {
        [self mouseExited:[[NSEvent alloc] init]];
    }
}

@end
