// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilePriorityCellView.h"
#import "FileListNode.h"
#import "NSImageAdditions.h"
#import "Torrent.h"

static CGFloat const kImageOverlap = 1.0;

@interface FilePriorityCellView ()
@property(nonatomic, weak) NSSegmentedControl* segmentedControl;
@property(nonatomic, weak) NSView* iconsContainerView;
@property(nonatomic, strong) NSTrackingArea* trackingArea;
@end

@implementation FilePriorityCellView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if ((self = [super initWithFrame:frameRect]))
    {
        // Create segmented control for hover state
        NSSegmentedControl* segmentedControl = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
        segmentedControl.translatesAutoresizingMaskIntoConstraints = NO;
        segmentedControl.trackingMode = NSSegmentSwitchTrackingSelectAny;
        segmentedControl.controlSize = NSControlSizeMini;
        segmentedControl.segmentCount = 3;

        for (NSInteger i = 0; i < segmentedControl.segmentCount; i++)
        {
            [segmentedControl setLabel:@"" forSegment:i];
            [segmentedControl setWidth:9.0f forSegment:i];
        }

        [segmentedControl setImage:[NSImage imageNamed:@"PriorityControlLow"] forSegment:0];
        [segmentedControl setImage:[NSImage imageNamed:@"PriorityControlNormal"] forSegment:1];
        [segmentedControl setImage:[NSImage imageNamed:@"PriorityControlHigh"] forSegment:2];

        segmentedControl.target = self;
        segmentedControl.action = @selector(segmentedControlClicked:);
        segmentedControl.hidden = YES;

        [self addSubview:segmentedControl];
        _segmentedControl = segmentedControl;

        // Create container view for priority icons
        NSView* iconsContainerView = [[NSView alloc] initWithFrame:NSZeroRect];
        iconsContainerView.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:iconsContainerView];
        _iconsContainerView = iconsContainerView;

        // Setup constraints
        [NSLayoutConstraint activateConstraints:@[
            [segmentedControl.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [segmentedControl.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],

            [iconsContainerView.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [iconsContainerView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [iconsContainerView.widthAnchor constraintLessThanOrEqualToAnchor:self.widthAnchor],
            [iconsContainerView.heightAnchor constraintLessThanOrEqualToAnchor:self.heightAnchor],
        ]];

        _hovered = NO;
    }
    return self;
}

- (void)setNode:(FileListNode*)node
{
    _node = node;

    [self updateDisplay];
}

- (void)setHovered:(BOOL)hovered
{
    _hovered = hovered;

    [self updateDisplay];
}

- (void)updateDisplay
{
    if (!self.node)
    {
        return;
    }

    FileListNode* node = self.node;
    Torrent* torrent = node.torrent;
    NSSet* priorities = [torrent filePrioritiesForIndexes:node.indexes];

    NSUInteger const count = priorities.count;
    if (self.hovered && count > 0)
    {
        // Show segmented control
        self.segmentedControl.hidden = NO;
        self.iconsContainerView.hidden = YES;

        [self.segmentedControl setSelected:[priorities containsObject:@(TR_PRI_LOW)] forSegment:0];
        [self.segmentedControl setSelected:[priorities containsObject:@(TR_PRI_NORMAL)] forSegment:1];
        [self.segmentedControl setSelected:[priorities containsObject:@(TR_PRI_HIGH)] forSegment:2];
    }
    else
    {
        // Show static priority icons
        self.segmentedControl.hidden = YES;
        self.iconsContainerView.hidden = NO;

        [self updatePriorityIcons:priorities];
    }

    // Update tooltip
    [self updateTooltip];
}

- (void)updatePriorityIcons:(NSSet*)priorities
{
    // Remove all existing image views
    for (NSView* subview in self.iconsContainerView.subviews)
    {
        [subview removeFromSuperview];
    }

    NSUInteger const count = priorities.count;
    NSMutableArray* images = [NSMutableArray arrayWithCapacity:MAX(count, 1u)];

    if (count == 0)
    {
        NSImage* image = [[NSImage imageNamed:@"PriorityNormalTemplate"] imageWithColor:NSColor.lightGrayColor];
        [images addObject:image];
    }
    else
    {
        NSColor* priorityColor = self.backgroundStyle == NSBackgroundStyleEmphasized ? NSColor.whiteColor : NSColor.darkGrayColor;

        if ([priorities containsObject:@(TR_PRI_LOW)])
        {
            NSImage* image = [[NSImage imageNamed:@"PriorityLowTemplate"] imageWithColor:priorityColor];
            [images addObject:image];
        }
        if ([priorities containsObject:@(TR_PRI_NORMAL)])
        {
            NSImage* image = [[NSImage imageNamed:@"PriorityNormalTemplate"] imageWithColor:priorityColor];
            [images addObject:image];
        }
        if ([priorities containsObject:@(TR_PRI_HIGH)])
        {
            NSImage* image = [[NSImage imageNamed:@"PriorityHighTemplate"] imageWithColor:priorityColor];
            [images addObject:image];
        }
    }

    NSView* previousView = nil;

    for (NSImage* image in images)
    {
        NSImageView* imageView = [[NSImageView alloc] initWithFrame:NSZeroRect];
        imageView.translatesAutoresizingMaskIntoConstraints = NO;
        imageView.image = image;
        [self.iconsContainerView addSubview:imageView];

        NSSize const imageSize = image.size;

        [NSLayoutConstraint activateConstraints:@[
            [imageView.widthAnchor constraintEqualToConstant:imageSize.width],
            [imageView.heightAnchor constraintEqualToConstant:imageSize.height],
            [imageView.centerYAnchor constraintEqualToAnchor:self.iconsContainerView.centerYAnchor],
        ]];

        if (previousView == nil)
        {
            [imageView.leadingAnchor constraintEqualToAnchor:self.iconsContainerView.leadingAnchor].active = YES;
        }
        else
        {
            [imageView.leadingAnchor constraintEqualToAnchor:previousView.trailingAnchor constant:-kImageOverlap].active = YES;
        }

        previousView = imageView;
    }

    if (previousView)
    {
        [previousView.trailingAnchor constraintEqualToAnchor:self.iconsContainerView.trailingAnchor].active = YES;
    }
}

- (void)segmentedControlClicked:(NSSegmentedControl*)sender
{
    NSInteger segment = sender.selectedSegment;
    if (segment == -1)
    {
        return;
    }

    tr_priority_t priority;
    switch (segment)
    {
    case 0:
        priority = TR_PRI_LOW;
        break;
    case 1:
        priority = TR_PRI_NORMAL;
        break;
    case 2:
        priority = TR_PRI_HIGH;
        break;
    default:
        NSAssert1(NO, @"Unknown segment: %ld", segment);
        return;
    }

    FileListNode* node = self.node;
    Torrent* torrent = node.torrent;
    [torrent setFilePriority:priority forIndexes:node.indexes];

    // Notify that we need to refresh
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];
}

- (void)setBackgroundStyle:(NSBackgroundStyle)backgroundStyle
{
    [super setBackgroundStyle:backgroundStyle];
    [self updateDisplay];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];

    if (self.trackingArea)
    {
        [self removeTrackingArea:self.trackingArea];
    }

    NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp;

    // Check if mouse is currently inside the bounds
    NSPoint mouseLocation = [self.window mouseLocationOutsideOfEventStream];
    NSPoint localPoint = [self convertPoint:mouseLocation fromView:nil];
    if (NSPointInRect(localPoint, self.bounds))
    {
        options |= NSTrackingAssumeInside;
        if (!self.hovered)
        {
            self.hovered = YES;
        }
    }
    else
    {
        // Mouse is not inside, reset hovered state
        if (self.hovered)
        {
            self.hovered = NO;
        }
    }

    self.trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds options:options owner:self userInfo:nil];
    [self addTrackingArea:self.trackingArea];
}

- (void)mouseEntered:(NSEvent*)event
{
    self.hovered = YES;
}

- (void)mouseExited:(NSEvent*)event
{
    self.hovered = NO;
}

- (void)updateTooltip
{
    if (!self.node)
    {
        return;
    }

    FileListNode* node = self.node;
    Torrent* torrent = node.torrent;
    NSSet* priorities = [torrent filePrioritiesForIndexes:node.indexes];

    NSString* tooltip = nil;
    switch (priorities.count)
    {
    case 0:
        tooltip = NSLocalizedString(@"Priority Not Available", "files tab -> tooltip");
        break;
    case 1:
        switch ([[priorities anyObject] intValue])
        {
        case TR_PRI_LOW:
            tooltip = NSLocalizedString(@"Low Priority", "files tab -> tooltip");
            break;
        case TR_PRI_HIGH:
            tooltip = NSLocalizedString(@"High Priority", "files tab -> tooltip");
            break;
        case TR_PRI_NORMAL:
            tooltip = NSLocalizedString(@"Normal Priority", "files tab -> tooltip");
            break;
        }
        break;
    default:
        tooltip = NSLocalizedString(@"Multiple Priorities", "files tab -> tooltip");
        break;
    }
    self.toolTip = tooltip;
}

@end
