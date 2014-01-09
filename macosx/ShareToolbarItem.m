//
//  ShareToolbarItem.m
//  Transmission
//
//  Created by Mitchell Livingston on 1/8/14.
//  Copyright (c) 2014 The Transmission Project. All rights reserved.
//

#import "ShareToolbarItem.h"
#import "Controller.h"
#import "NSApplicationAdditions.h"
#import "Torrent.h"

@implementation ShareToolbarItem

// move somewhere else?
+ (NSArray *)shareTorrentURLs
{
    NSArray * torrents = [(Controller *)[NSApp delegate] selectedTorrents];
    NSMutableArray * fileURLs = [NSMutableArray arrayWithCapacity: [torrents count]];
    for (Torrent * torrent in torrents)
    {
        NSString * location = [torrent torrentLocation];
        if ([location length] > 0) {
            [fileURLs addObject: [NSURL fileURLWithPath:location]];
        }
    }
    return fileURLs;
}

- (NSMenuItem *) menuFormRepresentation
{
    NSMenuItem * menuItem = [[NSMenuItem alloc] initWithTitle: [self label] action: nil keyEquivalent: @""];
    [menuItem setEnabled: [[self target] validateToolbarItem: self]];
    
    NSArray * fileURLs = [[self class] shareTorrentURLs];
    if ([menuItem isEnabled] && [fileURLs count] > 0) {
        NSMenu *servicesMenu = [[NSMenu alloc] initWithTitle: @""];
        for (NSSharingService * service in [NSSharingService sharingServicesForItems: fileURLs])
        {
            NSMenuItem *item = [[NSMenuItem alloc] initWithTitle: service.title // 10.9: change to menuItemTitle
                                                          action: @selector(performShareAction:)
                                                   keyEquivalent: @""];
            item.image = service.image;
            item.representedObject = service;
            service.delegate = (Controller *)[NSApp delegate];
            item.target = self;
            [servicesMenu addItem:item];
            [item release];
        }
        
        [menuItem setSubmenu:servicesMenu];
        [servicesMenu release]; // can't believe we're not using ARC yet!
    }
    
    return menuItem;
}

- (void)performShareAction:(NSMenuItem *)item
{
    NSSharingService * service = item.representedObject;
    [service performWithItems: [[self class] shareTorrentURLs]]; // on 10.9, use attachmentFileURLs?
}

@end
