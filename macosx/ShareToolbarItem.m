//
//  ShareToolbarItem.m
//  Transmission
//
//  Created by Mitchell Livingston on 1/8/14.
//  Copyright (c) 2014 The Transmission Project. All rights reserved.
//

#import "ShareToolbarItem.h"
#import "ShareTorrentFileHelper.h"
#import "NSApplicationAdditions.h"

@implementation ShareToolbarItem

- (NSMenuItem *) menuFormRepresentation
{
    NSMenuItem * menuItem = [[NSMenuItem alloc] initWithTitle: [self label] action: nil keyEquivalent: @""];
    [menuItem setEnabled: [[self target] validateToolbarItem: self]];

    if ([menuItem isEnabled]) {
        NSMenu *servicesMenu = [[NSMenu alloc] initWithTitle: @""];
        for (NSMenuItem * item in [[ShareTorrentFileHelper sharedHelper] menuItems])
        {
            [servicesMenu addItem:item];
        }

        [menuItem setSubmenu:servicesMenu];
    }

    return menuItem;
}

@end
