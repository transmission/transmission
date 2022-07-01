// This file Copyright © 2007-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "Toolbar.h"

@implementation Toolbar

- (void)setVisible:(BOOL)visible
{
    //we need to redraw the main window after each change
    //otherwise we get strange drawing issues, leading to a potential crash
    [NSNotificationCenter.defaultCenter postNotificationName:@"ToolbarDidChange" object:nil];

    super.visible = visible;
}

@end
