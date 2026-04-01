// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCell.h"
#import "ProgressBarView.h"
#import "ProgressGradients.h"
#import "Torrent.h"
#import "NSImageAdditions.h"
#import "TorrentCellControlButton.h"
#import "TorrentCellActionButton.h"
#import "TorrentCellRevealButton.h"

@implementation TorrentCell

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect])
    {
        [self configureViews];
        [self setupConstraints];
    }
    return self;
}

- (void)configureViews
{
    __auto_type groupIndicatorView = [[NSImageView alloc] init];

    __auto_type iconView = [[NSImageView alloc] init];
    __auto_type actionButton = [[TorrentCellActionButton alloc] init];

    __auto_type stackView = [[NSStackView alloc] init];
    stackView.distribution = NSStackViewDistributionFill;
    stackView.spacing = 4;

    __auto_type torrentTitleField = [[NSTextField alloc] init];
    torrentTitleField.textColor = NSColor.labelColor;
    torrentTitleField.backgroundColor = NSColor.textBackgroundColor;
    torrentTitleField.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightRegular];

    __auto_type torrentPriorityView = [[NSImageView alloc] init];

    [stackView addArrangedSubview:torrentTitleField];
    [stackView addArrangedSubview:torrentPriorityView];

    __auto_type torrentProgressField = [[NSTextField alloc] init];
    torrentProgressField.textColor = NSColor.secondaryLabelColor;
    torrentProgressField.backgroundColor = NSColor.textBackgroundColor;
    torrentProgressField.font = [NSFont systemFontOfSize:10.0 weight:NSFontWeightRegular];

    __auto_type torrentStatusField = [[NSTextField alloc] init];
    torrentStatusField.textColor = NSColor.secondaryLabelColor;
    torrentStatusField.backgroundColor = NSColor.textBackgroundColor;
    torrentStatusField.font = [NSFont systemFontOfSize:10.0 weight:NSFontWeightRegular];

    __auto_type torrentProgressBarView = [[NSView alloc] init];

    __auto_type controlButton = [[TorrentCellControlButton alloc] init];
    __auto_type revealButton = [[TorrentCellRevealButton alloc] init];

    for (NSImageView* imageView in @[ groupIndicatorView, iconView, torrentPriorityView ])
    {
        imageView.imageScaling = NSImageScaleProportionallyDown;
    }

    for (NSTextField* textField in @[ torrentTitleField, torrentProgressField, torrentStatusField ])
    {
        textField.editable = NO;
        textField.selectable = NO;
        textField.bordered = NO;
        textField.drawsBackground = NO;
    }

    for (NSButton* button in @[ actionButton, controlButton, revealButton ])
    {
        button.imagePosition = NSImageOnly;
        button.imageScaling = NSImageScaleProportionallyDown;
        [button setButtonType:NSButtonTypeMomentaryPushIn];
        [button setBezelStyle:NSBezelStyleRegularSquare];
        button.bordered = NO;
    }

    for (NSView* view in @[
             groupIndicatorView,
             iconView,
             actionButton,
             stackView,
             torrentProgressField,
             torrentStatusField,
             torrentProgressBarView,
             controlButton,
             revealButton,
         ])
    {
        view.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:view];
    }

    self.fGroupIndicatorView = groupIndicatorView;
    self.fIconView = iconView;
    self.fActionButton = actionButton;
    self.fStackView = stackView;
    self.fTorrentTitleField = torrentTitleField;
    self.fTorrentPriorityView = torrentPriorityView;
    self.fTorrentProgressField = torrentProgressField;
    self.fTorrentStatusField = torrentStatusField;
    self.fTorrentProgressBarView = torrentProgressBarView;
    self.fControlButton = controlButton;
    self.fRevealButton = revealButton;
}

- (void)setupConstraints
{
    __auto_type groupIndicatorView = self.fGroupIndicatorView;
    __auto_type iconView = self.fIconView;
    __auto_type actionButton = self.fActionButton;
    __auto_type stackView = self.fStackView;
    __auto_type torrentProgressField = self.fTorrentProgressField;
    __auto_type torrentStatusField = self.fTorrentStatusField;
    __auto_type torrentProgressBarView = self.fTorrentProgressBarView;
    __auto_type controlButton = self.fControlButton;
    __auto_type revealButton = self.fRevealButton;

    [NSLayoutConstraint activateConstraints:@[
        // groupIndicatorView
        [groupIndicatorView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [groupIndicatorView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [groupIndicatorView.widthAnchor constraintEqualToConstant:10],
        [groupIndicatorView.heightAnchor constraintEqualToConstant:10],

        // iconView
        [iconView.leadingAnchor constraintEqualToAnchor:groupIndicatorView.trailingAnchor constant:3],
        [iconView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [iconView.widthAnchor constraintEqualToConstant:36],
        [iconView.heightAnchor constraintEqualToConstant:36],

        // actionButton
        [actionButton.centerXAnchor constraintEqualToAnchor:iconView.centerXAnchor],
        [actionButton.centerYAnchor constraintEqualToAnchor:iconView.centerYAnchor],
        [actionButton.widthAnchor constraintEqualToConstant:16],
        [actionButton.heightAnchor constraintEqualToConstant:16],

        // stackView
        [stackView.leadingAnchor constraintEqualToAnchor:iconView.trailingAnchor constant:16],
        [stackView.trailingAnchor constraintLessThanOrEqualToAnchor:torrentProgressBarView.trailingAnchor],
        [stackView.topAnchor constraintEqualToAnchor:self.topAnchor constant:3],
        [stackView.leadingAnchor constraintEqualToAnchor:torrentProgressField.leadingAnchor],

        // torrentProgressField
        [torrentProgressField.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.leadingAnchor],
        [torrentProgressField.trailingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor],
        [torrentProgressField.topAnchor constraintEqualToAnchor:stackView.bottomAnchor constant:1],

        // torrentStatusField
        [torrentStatusField.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.leadingAnchor],
        [torrentStatusField.trailingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor],
        [torrentStatusField.topAnchor constraintEqualToAnchor:torrentProgressBarView.bottomAnchor constant:1],
        [torrentStatusField.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-1],

        // torrentProgressBarView
        [torrentProgressBarView.centerYAnchor constraintEqualToAnchor:torrentProgressBarView.centerYAnchor],
        [torrentProgressBarView.heightAnchor constraintEqualToConstant:14],

        // controlButton
        [controlButton.leadingAnchor constraintEqualToAnchor:torrentProgressBarView.trailingAnchor constant:14],
        [controlButton.centerYAnchor constraintEqualToAnchor:torrentProgressBarView.centerYAnchor],
        [controlButton.widthAnchor constraintEqualToConstant:14],
        [controlButton.heightAnchor constraintEqualToConstant:14],

        // revealButton
        [revealButton.leadingAnchor constraintEqualToAnchor:controlButton.trailingAnchor constant:3],
        [revealButton.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],
        [revealButton.centerYAnchor constraintEqualToAnchor:controlButton.centerYAnchor],
        [revealButton.widthAnchor constraintEqualToConstant:14],
        [revealButton.heightAnchor constraintEqualToConstant:14],
    ]];
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (self.fTorrentTableView)
    {
        Torrent* torrent = (Torrent*)self.objectValue;

        // draw progress bar
        NSRect barRect = self.fTorrentProgressBarView.frame;
        ProgressBarView* progressBar = [[ProgressBarView alloc] init];
        [progressBar drawBarInRect:barRect forTableView:self.fTorrentTableView withTorrent:torrent];

        // set priority icon
        if (torrent.priority != TR_PRI_NORMAL)
        {
            NSColor* priorityColor = self.backgroundStyle == NSBackgroundStyleEmphasized ? NSColor.whiteColor : NSColor.labelColor;
            NSImage* priorityImage = [[NSImage imageNamed:(torrent.priority == TR_PRI_HIGH ? @"PriorityHighTemplate" : @"PriorityLowTemplate")]
                imageWithColor:priorityColor];

            self.fTorrentPriorityView.image = priorityImage;

            [self.fStackView setVisibilityPriority:NSStackViewVisibilityPriorityMustHold forView:self.fTorrentPriorityView];
        }
        else
        {
            [self.fStackView setVisibilityPriority:NSStackViewVisibilityPriorityNotVisible forView:self.fTorrentPriorityView];
        }
    }

    [super drawRect:dirtyRect];
}

// otherwise progress bar is inverted
- (BOOL)isFlipped
{
    return YES;
}

@end
