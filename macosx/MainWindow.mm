// This file Copyright © 2008-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "MainWindow.h"

#define BIG_SUR_TOOLBARHEIGHT 28.0

@implementation MainWindow

- (void)toggleToolbarShown:(id)sender
{
    NSRect frame = self.frame;

    //fix window sizing when toggling toolbar
    if (@available(macOS 10.11, *))
    {
        if (self.toolbar.isVisible == YES)
        {
            frame.size.height += BIG_SUR_TOOLBARHEIGHT;
        }
        else
        {
            frame.size.height -= BIG_SUR_TOOLBARHEIGHT;
        }
    }
    [self setFrame:frame display:YES animate:NO];

    [super toggleToolbarShown:sender];
}

@end
