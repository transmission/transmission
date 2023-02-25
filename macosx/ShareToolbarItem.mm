// This file Copyright © 2014-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.
// Created by Mitchell Livingston on 1/8/14.

#import "ShareToolbarItem.h"
#import "ShareTorrentFileHelper.h"

@implementation ShareToolbarItem

- (NSMenuItem*)menuFormRepresentation
{
    NSMenuItem* menuItem = [[NSMenuItem alloc] initWithTitle:self.label action:nil keyEquivalent:@""];
    menuItem.enabled = [self.target validateToolbarItem:self];

    if (menuItem.enabled)
    {
        NSMenu* servicesMenu = [[NSMenu alloc] initWithTitle:@""];
        for (NSMenuItem* item in ShareTorrentFileHelper.sharedHelper.menuItems)
        {
            [servicesMenu addItem:item];
        }

        menuItem.submenu = servicesMenu;
    }

    return menuItem;
}

@end
