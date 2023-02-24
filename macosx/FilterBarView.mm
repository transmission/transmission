// This file Copyright © 2011-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilterBarView.h"

@interface FilterBarView ()
@property(weak) IBOutlet NSLayoutConstraint* searchFieldVerticalConstraint;
@end

@implementation FilterBarView

- (void)awakeFromNib
{
    _searchFieldVerticalConstraint.constant = -.5;
}

- (BOOL)mouseDownCanMoveWindow
{
    return NO;
}

- (BOOL)isOpaque
{
    return YES;
}

- (void)drawRect:(NSRect)rect
{
    [NSColor.windowBackgroundColor setFill];
    NSRectFill(rect);

    NSRect const lineBorderRect = NSMakeRect(NSMinX(rect), 0.0, NSWidth(rect), 1.0);
    if (NSIntersectsRect(lineBorderRect, rect))
    {
        [NSColor.gridColor setFill];
        NSRectFill(lineBorderRect);
    }
}

@end
