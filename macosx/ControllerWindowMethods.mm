// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "ControllerWindowMethods.h"

#define STATUS_BAR_HEIGHT 21.0
#define FILTER_BAR_HEIGHT 23.0
#define BOTTOM_BAR_HEIGHT 24.0

@implementation Controller (ControllerWindowMethods)

- (void)updateMainWindow
{
    NSArray* subViews = self.fStackView.arrangedSubviews;
    NSUInteger idx = 0;

    //update layout
    if ([self.fDefaults boolForKey:@"StatusBar"])
    {
        if (self.fStatusBar == nil)
        {
            self.fStatusBar = [[StatusBarController alloc] initWithLib:self.fLib];
        }

        [self.fStackView insertArrangedSubview:self.fStatusBar.view atIndex:idx];

        NSDictionary* views = @{ @"fStatusBar" : self.fStatusBar.view };
        [self.fStackView addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:[fStatusBar(==21)]" options:0
                                                                                metrics:nil
                                                                                  views:views]];
        idx = 1;
    }
    else
    {
        if ([subViews containsObject:self.fStatusBar.view])
        {
            [self.fStackView removeView:self.fStatusBar.view];
            self.fStatusBar = nil;
        }
    }

    if ([self.fDefaults boolForKey:@"FilterBar"])
    {
        if (self.fFilterBar == nil)
        {
            self.fFilterBar = [[FilterBarController alloc] init];
        }

        [self.fStackView insertArrangedSubview:self.fFilterBar.view atIndex:idx];

        NSDictionary* views = @{ @"fFilterBar" : self.fFilterBar.view };
        [self.fStackView addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:[fFilterBar(==21)]" options:0
                                                                                metrics:nil
                                                                                  views:views]];

        [self focusFilterField];
    }
    else
    {
        if ([subViews containsObject:self.fFilterBar.view])
        {
            [self.fStackView removeView:self.fFilterBar.view];
            self.fFilterBar = nil;

            [self.fWindow makeFirstResponder:self.fTableView];
        }
    }

    [self fullUpdateUI];
    [self updateForAutoSize];
}

- (void)setWindowSizeToFit
{
    if (!self.isFullScreen)
    {
        if (![self.fDefaults boolForKey:@"AutoSize"])
        {
            [self removeWindowMinMax];
        }
        else
        {
            NSScrollView* scrollView = self.fTableView.enclosingScrollView;

            scrollView.hasVerticalScroller = NO;

            [self removeStackViewHeightConstraints];

            //update height constraints
            NSDictionary* views = @{ @"scrollView" : scrollView };
            CGFloat height = self.scrollViewHeight;
            NSString* constraintsString = [NSString stringWithFormat:@"V:[scrollView(==%f)]", height];

            //add height constraint
            self.fStackViewHeightConstraints = [NSLayoutConstraint constraintsWithVisualFormat:constraintsString options:0
                                                                                       metrics:nil
                                                                                         views:views];
            [self.fStackView addConstraints:self.fStackViewHeightConstraints];

            scrollView.hasVerticalScroller = YES;

            [self setWindowMinMaxToCurrent];
        }
    }
    else
    {
        [self removeStackViewHeightConstraints];
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
        [self removeWindowMinMax];
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

- (void)removeWindowMinMax
{
    [self setMinWindowContentSizeAllowed];
    [self setMaxWindowContentSizeAllowed];
    [self removeStackViewHeightConstraints];
}

- (void)setMinWindowContentSizeAllowed
{
    NSSize contentMinSize = self.fWindow.contentMinSize;
    contentMinSize.height = self.minWindowContentHeightAllowed;

    self.fWindow.contentMinSize = contentMinSize;
}

- (void)setMaxWindowContentSizeAllowed
{
    NSSize contentMaxSize = self.fWindow.contentMaxSize;
    contentMaxSize.height = FLT_MAX;
    self.fWindow.contentMaxSize = contentMaxSize;
}

- (void)removeStackViewHeightConstraints
{
    if (self.fStackViewHeightConstraints)
    {
        [self.fStackView removeConstraints:self.fStackViewHeightConstraints];
    }
}

- (CGFloat)minWindowContentHeightAllowed
{
    CGFloat contentMinHeight = self.fTableView.rowHeight + self.fTableView.intercellSpacing.height + self.mainWindowComponentHeight;
    return contentMinHeight;
}

- (CGFloat)toolbarHeight
{
    return self.fWindow.frame.size.height - [self.fWindow contentRectForFrameRect:self.fWindow.frame].size.height;
}

- (CGFloat)mainWindowComponentHeight
{
    CGFloat height = BOTTOM_BAR_HEIGHT;
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
    CGFloat height;
    CGFloat minHeight = self.fTableView.rowHeight + self.fTableView.intercellSpacing.height;

    if ([self.fDefaults boolForKey:@"AutoSize"])
    {
        NSUInteger groups = (self.fDisplayedTorrents.count > 0 && ![self.fDisplayedTorrents[0] isKindOfClass:[Torrent class]]) ?
            self.fDisplayedTorrents.count :
            0;

        height = (GROUP_SEPARATOR_HEIGHT + self.fTableView.intercellSpacing.height) * groups +
            (self.fTableView.rowHeight + self.fTableView.intercellSpacing.height) * (self.fTableView.numberOfRows - groups);
    }
    else
    {
        height = NSHeight(self.fTableView.enclosingScrollView.frame);
    }

    //make sure we dont go bigger that the screen height
    NSScreen* screen = self.fWindow.screen;
    if (screen)
    {
        NSSize maxSize = screen.frame.size;
        maxSize.height -= self.toolbarHeight;
        maxSize.height -= self.mainWindowComponentHeight;

        //add a small buffer
        maxSize.height -= 50;

        if (height > maxSize.height)
        {
            height = maxSize.height;
        }
    }

    //make sure we dont have zero height
    if (height < minHeight)
    {
        height = minHeight;
    }

    return height;
}

- (BOOL)isFullScreen
{
    return (self.fWindow.styleMask & NSFullScreenWindowMask);
}

- (void)windowWillEnterFullScreen:(NSNotification*)notification
{
    [self removeWindowMinMax];
}

- (void)windowDidExitFullScreen:(NSNotification*)notification
{
    [self updateForAutoSize];
}

@end
