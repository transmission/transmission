// This file Copyright (c) 2007-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FilterButton.h"
#import "NSStringAdditions.h"

@implementation FilterButton

- (instancetype)initWithCoder:(NSCoder*)coder
{
    if ((self = [super initWithCoder:coder]))
    {
        fCount = NSNotFound;
    }
    return self;
}

- (void)setCount:(NSUInteger)count
{
    if (count == fCount)
    {
        return;
    }

    fCount = count;

    self.toolTip = fCount == 1 ? NSLocalizedString(@"1 transfer", "Filter Button -> tool tip") :
                                 [NSString stringWithFormat:NSLocalizedString(@"%@ transfers", "Filter Bar Button -> tool tip"),
                                                            [NSString formattedUInteger:fCount]];
}

@end
