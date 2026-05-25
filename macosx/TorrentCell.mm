// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCell.h"
#import "ProgressBarView.h"
#import "ProgressGradients.h"
#import "Torrent.h"
#import "NSImageAdditions.h"

@interface TorrentCell ()
@property(nonatomic, strong) ProgressBarView2* secondProgress;
@end

@implementation TorrentCell

- (void)awakeFromNib
{
    [super awakeFromNib];

    self.wantsLayer = YES;
    self.layer.masksToBounds = YES;

    if (self.secondProgress == nil)
    {
        self.secondProgress = [[ProgressBarView2 alloc] init];
        self.secondProgress.translatesAutoresizingMaskIntoConstraints = false;
    }
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (void)updateLayer
{
    __auto_type was = CFAbsoluteTimeGetCurrent();
    [super updateLayer];

    if (self.fTorrentTableView)
    {
        Torrent* torrent = (Torrent*)self.objectValue;
        if (self.secondProgress.superview == nil)
        {
            [self.fTorrentProgressBarView addSubview:self.secondProgress];

            __auto_type view = self.secondProgress;
            [NSLayoutConstraint activateConstraints:@[
                [view.leadingAnchor constraintEqualToAnchor:self.fTorrentProgressBarView.leadingAnchor],
                [view.trailingAnchor constraintEqualToAnchor:self.fTorrentProgressBarView.trailingAnchor],
                [view.topAnchor constraintEqualToAnchor:self.fTorrentProgressBarView.topAnchor],
                [view.bottomAnchor constraintEqualToAnchor:self.fTorrentProgressBarView.bottomAnchor],
            ]];
        }

        // draw progress bar
        // NSRect barRect = self.fTorrentProgressBarView.frame;
        // ProgressBarView* progressBar = [[ProgressBarView alloc] init];
        [self.secondProgress drawBarInRect:CGRectZero forTableView:self.fTorrentTableView withTorrent:torrent];
        // [self.secondProgress drawBarIn]

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
    
    __auto_type spent = CFAbsoluteTimeGetCurrent() - was;
    NSLog(@"Update happened in %.4f", spent);
}

- (void)drawRect:(NSRect)dirtyRect
{
    return;
    if (self.fTorrentTableView)
    {
        Torrent* torrent = (Torrent*)self.objectValue;

        if (self.secondProgress.superview == nil)
        {
            [self.fTorrentTableView addSubview:self.secondProgress];

            __auto_type view = self.secondProgress;
            [NSLayoutConstraint activateConstraints:@[
                [view.leadingAnchor constraintEqualToAnchor:self.fTorrentProgressBarView.leadingAnchor],
                [view.trailingAnchor constraintEqualToAnchor:self.fTorrentProgressBarView.trailingAnchor],
                [view.topAnchor constraintEqualToAnchor:self.fTorrentProgressBarView.topAnchor],
                [view.bottomAnchor constraintEqualToAnchor:self.fTorrentProgressBarView.bottomAnchor],
            ]];
        }

        // draw progress bar
        // NSRect barRect = self.fTorrentProgressBarView.frame;
        // ProgressBarView* progressBar = [[ProgressBarView alloc] init];
        [self.secondProgress drawBarInRect:CGRectZero forTableView:self.fTorrentTableView withTorrent:torrent];
        // [self.secondProgress drawBarIn]

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
