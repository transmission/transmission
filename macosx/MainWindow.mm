// This file Copyright © 2008-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "MainWindow.h"

#define TOOL_BAR_HEIGHT 28.0

@implementation MainWindow

- (void)toggleToolbarShown:(id)sender
{
    NSRect frame = self.frame;

    //fix window size and origin when toggling toolbar
    if (self.toolbar.isVisible == YES)
    {
        frame.size.height += TOOL_BAR_HEIGHT;
        frame.origin.y -= TOOL_BAR_HEIGHT;
    }
    else
    {
        frame.size.height -= TOOL_BAR_HEIGHT;
        frame.origin.y += TOOL_BAR_HEIGHT;
    }

    [self setFrame:frame display:YES animate:NO];
    [super toggleToolbarShown:sender];
}

@end
