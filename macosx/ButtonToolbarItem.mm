// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "ButtonToolbarItem.h"

@implementation ButtonToolbarItem

- (void)validate
{
    self.enabled = [self.target validateToolbarItem:self];
}

- (NSMenuItem*)menuFormRepresentation
{
    NSMenuItem* menuItem = [[NSMenuItem alloc] initWithTitle:self.label action:self.action keyEquivalent:@""];
    menuItem.target = self.target;
    menuItem.enabled = [self.target validateToolbarItem:self];

    return menuItem;
}

@end
