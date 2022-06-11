// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "ControllerWindowMethods.h"

#define WINDOW_REGULAR_WIDTH 468.0

#define STATUS_BAR_HEIGHT 21.0
#define FILTER_BAR_HEIGHT 23.0
#define BOTTOM_BAR_HEIGHT 24.0

@implementation Controller (ControllerWindowMethods)

- (void)drawMainWindow
{
    NSView* contentView = self.fWindow.contentView;
    NSSize const windowSize = [contentView convertSize:self.fWindow.frame.size fromView:nil];
    CGFloat originY = NSMaxY(contentView.frame);

    //remove all subviews
    for (id view in contentView.subviews.copy)
    {
        [view removeFromSuperviewWithoutNeedingDisplay];
    }

    self.fStatusBar = nil;
    self.fFilterBar = nil;

    if ([self.fDefaults boolForKey:@"StatusBar"])
    {
        self.fStatusBar = [[StatusBarController alloc] initWithLib:self.fLib];

        NSRect statusBarFrame = self.fStatusBar.view.frame;
        statusBarFrame.size.width = windowSize.width;

        originY -= STATUS_BAR_HEIGHT;
        statusBarFrame.origin.y = originY;
        self.fStatusBar.view.frame = statusBarFrame;

        [contentView addSubview:self.fStatusBar.view];
    }

    if ([self.fDefaults boolForKey:@"FilterBar"])
    {
        self.fFilterBar = [[FilterBarController alloc] init];

        NSRect filterBarFrame = self.fFilterBar.view.frame;
        filterBarFrame.size.width = windowSize.width;

        originY -= FILTER_BAR_HEIGHT;
        filterBarFrame.origin.y = originY;
        self.fFilterBar.view.frame = filterBarFrame;

        [contentView addSubview:self.fFilterBar.view];
    }

    NSScrollView* scrollView = self.fTableView.enclosingScrollView;
    [contentView addSubview:scrollView];

    [contentView addSubview:self.fActionButton];
    [contentView addSubview:self.fSpeedLimitButton];
    [contentView addSubview:self.fClearCompletedButton];
    [contentView addSubview:self.fTotalTorrentsField];

    //window is updated and animated in fullUpdateUI --> applyFilter --> setWindowSizeToFit
    [self fullUpdateUI];
    [self updateForAutoSize];
}

- (void)setWindowSizeToFit
{
    NSScrollView* scrollView = self.fTableView.enclosingScrollView;

    scrollView.hasVerticalScroller = NO;
    [self.fWindow setFrame:self.sizedWindowFrame display:YES animate:YES];
    scrollView.hasVerticalScroller = YES;

    if ([self.fDefaults boolForKey:@"AutoSize"])
    {
        if (!self.isFullScreen)
        {
            [self setWindowMinMaxToCurrent];
        }
    }
}

- (void)updateForAutoSize
{
    if ([self.fDefaults boolForKey:@"AutoSize"] && !self.isFullScreen)
    {
        [self setWindowSizeToFit];
    }
    else
    {
        NSSize contentMinSize = self.fWindow.contentMinSize;
        contentMinSize.height = self.minWindowContentSizeAllowed;

        self.fWindow.contentMinSize = contentMinSize;

        NSSize contentMaxSize = self.fWindow.contentMaxSize;
        contentMaxSize.height = FLT_MAX;
        self.fWindow.contentMaxSize = contentMaxSize;
    }
}

- (void)setWindowMinMaxToCurrent
{
    CGFloat const height = NSHeight(self.fWindow.contentView.frame);

    NSSize minSize = self.fWindow.contentMinSize, maxSize = self.fWindow.contentMaxSize;
    minSize.height = height;
    maxSize.height = height;

    self.fWindow.contentMinSize = minSize;
    self.fWindow.contentMaxSize = maxSize;
}

- (NSRect)sizedWindowFrame
{
    NSRect windowFrame = self.fWindow.frame;
    NSScrollView* scrollView = self.fTableView.enclosingScrollView;
    CGFloat titleBarHeight = self.titlebarHeight;
    CGFloat scrollViewHeight = self.scrollViewHeight;
    CGFloat componentHeight = [self mainWindowComponentHeight];

    //update window frame
    NSSize windowSize = [scrollView convertSize:windowFrame.size fromView:nil];
    windowSize.height = titleBarHeight + componentHeight + scrollViewHeight + BOTTOM_BAR_HEIGHT;

    //update scrollview
    NSRect scrollViewFrame = scrollView.frame;
    scrollViewFrame.size.height = scrollViewHeight;
    scrollViewFrame.origin.y = BOTTOM_BAR_HEIGHT;
    [scrollView setFrame:scrollViewFrame];

    //we can't call minSize, since it might be set to the current size (auto size)
    CGFloat const minHeight = self.minWindowContentSizeAllowed +
        (NSHeight(self.fWindow.frame) - NSHeight(self.fWindow.contentView.frame)); //contentView to window

    if (windowSize.height <= minHeight)
    {
        windowSize.height = minHeight;
    }
    else
    {
        NSScreen* screen = self.fWindow.screen;
        if (screen)
        {
            NSSize maxSize = [scrollView convertSize:screen.visibleFrame.size fromView:nil];
            maxSize.height += titleBarHeight;
            maxSize.height += BOTTOM_BAR_HEIGHT;

            if (self.fStatusBar)
            {
                maxSize.height += STATUS_BAR_HEIGHT;
            }
            if (self.fFilterBar)
            {
                maxSize.height += FILTER_BAR_HEIGHT;
            }
            if (windowSize.height > maxSize.height)
            {
                windowSize.height = maxSize.height;
            }
        }
    }

    windowFrame.origin.y -= (windowSize.height - windowFrame.size.height);
    windowFrame.size.height = windowSize.height;
    return windowFrame;
}

- (CGFloat)titlebarHeight
{
    return self.fWindow.frame.size.height - [self.fWindow contentRectForFrameRect:self.fWindow.frame].size.height;
}

- (CGFloat)mainWindowComponentHeight
{
    CGFloat height = 0;
    if (self.fStatusBar)
    {
        height += STATUS_BAR_HEIGHT;
    }

    if (self.fFilterBar)
    {
        height += FILTER_BAR_HEIGHT;
    }

    return height;
}

- (CGFloat)scrollViewHeight
{
    if (self.isFullScreen)
    {
        return self.fWindow.frame.size.height - self.titlebarHeight - self.mainWindowComponentHeight - BOTTOM_BAR_HEIGHT;
    }

    if ([self.fDefaults boolForKey:@"AutoSize"])
    {
        NSUInteger groups = (self.fDisplayedTorrents.count > 0 && ![self.fDisplayedTorrents[0] isKindOfClass:[Torrent class]]) ?
            self.fDisplayedTorrents.count :
            0;

        CGFloat height = (GROUP_SEPARATOR_HEIGHT + self.fTableView.intercellSpacing.height) * groups +
            (self.fTableView.rowHeight + self.fTableView.intercellSpacing.height) * (self.fTableView.numberOfRows - groups);

        if (height > 0)
        {
            return height;
        }
    }

    return NSHeight(self.fTableView.enclosingScrollView.frame);
}

- (CGFloat)minWindowContentSizeAllowed
{
    CGFloat contentMinHeight = NSHeight(self.fWindow.contentView.frame) - NSHeight(self.fTableView.enclosingScrollView.frame) +
        self.fTableView.rowHeight + self.fTableView.intercellSpacing.height;
    return contentMinHeight;
}

- (BOOL)isFullScreen
{
    return (self.fWindow.styleMask & NSFullScreenWindowMask);
}

- (void)windowWillEnterFullScreen:(NSNotification*)notification
{
    // temporarily disable AutoSize
    NSSize contentMinSize = self.fWindow.contentMinSize;
    contentMinSize.height = self.minWindowContentSizeAllowed;

    self.fWindow.contentMinSize = contentMinSize;

    NSSize contentMaxSize = self.fWindow.contentMaxSize;
    contentMaxSize.height = FLT_MAX;
    self.fWindow.contentMaxSize = contentMaxSize;
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification
{
    [self drawMainWindow];
}

- (void)windowWillExitFullScreen:(NSNotification*)notification
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [self drawMainWindow];
    });
}

- (void)windowDidExitFullScreen:(NSNotification*)notification
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [self drawMainWindow];
    });
}

@end
