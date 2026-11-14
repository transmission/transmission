// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilterButton.h"
#import "NSStringAdditions.h"

@interface FilterButton ()

/// Native NSButtonTextField that holds the correct frame.
@property(nonatomic, strong) NSTextField* originalTextField;

/// Button on its left.
@property(nonatomic) IBOutlet FilterButton* previousButton;

@end

@implementation FilterButton

- (instancetype)initWithCoder:(NSCoder*)coder
{
    if ((self = [super initWithCoder:coder]))
    {
        _count = NSNotFound;
    }
    return self;
}

- (void)addSubview:(NSView *)view
{
    // The native NSButtonTextField is removed from the view hierarchy at unpredictable times by `[NSButtonAppearanceBasedVisualProvider removeTextField]`.
    // That removal is later followed by the re-addition of a new textfield.
    if ([view isKindOfClass:NSTextField.class])
    {
        _originalTextField = (NSTextField*)view;
    }
    [super addSubview:view];
}

- (void)setFrame:(NSRect)frame
{
    // The containing NSStackView is misframing the buttons, ignoring layout constraints.
    // Apple switched development to SwiftUI and will probably not fix this, so we override the frame ourselves.
    if (frame.size.width > self.superview.bounds.size.width / 2)
    {
        // We cap to the intrinsicContentSize
        frame.size.width = _originalTextField.intrinsicContentSize.width;
    }
    if (frame.size.width < 20)
    {
        // Prevent buttons from disappearing
        frame.size.width = 20;
    }
    if (self.previousButton)
    {
        // Prevents the NSStackView superposing the buttons
        frame.origin.x = NSMaxX(self.previousButton.frame) + 1;
    }
    [super setFrame:frame];
}

- (void)setCount:(NSUInteger)count
{
    if (count == _count)
    {
        return;
    }

    _count = count;

    self.toolTip = count == 1 ?
        NSLocalizedString(@"1 transfer", "Filter Button -> tool tip") :
        [NSString localizedStringWithFormat:NSLocalizedString(@"%lu transfers", "Filter Bar Button -> tool tip"), count];
}

@end
