// This file Copyright © 2007-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupToolbarItem.h"

@implementation GroupToolbarItem

- (void)validate
{
    NSSegmentedControl* control = (NSSegmentedControl*)self.view;

    for (NSInteger i = 0; i < control.segmentCount; i++)
    {
        [control setEnabled:[self.target validateToolbarItem:[[NSToolbarItem alloc] initWithItemIdentifier:self.identifiers[i]]]
                 forSegment:i];
    }
}

- (void)createMenu:(NSArray*)labels
{
    NSMenuItem* menuItem = [[NSMenuItem alloc] initWithTitle:self.label action:NULL keyEquivalent:@""];
    NSMenu* menu = [[NSMenu alloc] initWithTitle:self.label];
    menuItem.submenu = menu;

    menu.autoenablesItems = NO;

    NSInteger const count = ((NSSegmentedControl*)self.view).segmentCount;
    for (NSInteger i = 0; i < count; i++)
    {
        NSMenuItem* addItem = [[NSMenuItem alloc] initWithTitle:labels[i] action:self.action keyEquivalent:@""];
        addItem.target = self.target;
        addItem.tag = i;

        [menu addItem:addItem];
    }

    self.menuFormRepresentation = menuItem;
}

- (NSMenuItem*)menuFormRepresentation
{
    NSMenuItem* menuItem = super.menuFormRepresentation;

    NSInteger const count = ((NSSegmentedControl*)self.view).segmentCount;
    for (NSInteger i = 0; i < count; i++)
    {
        [menuItem.submenu itemAtIndex:i].enabled = [self.target
            validateToolbarItem:[[NSToolbarItem alloc] initWithItemIdentifier:self.identifiers[i]]];
    }

    return menuItem;
}

@end
