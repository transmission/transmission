//
//  ShareTorrentFileHelper.m
//  Transmission
//
//  Created by Mitchell Livingston on 1/10/14.
//  Copyright (c) 2014 The Transmission Project. All rights reserved.
//

#import "ShareTorrentFileHelper.h"
#import "Controller.h"
#import "Torrent.h"

@implementation ShareTorrentFileHelper

+ (ShareTorrentFileHelper *) sharedHelper
{
    static ShareTorrentFileHelper *helper;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        helper = [[ShareTorrentFileHelper alloc] init];
    });
    return helper;
}

- (NSArray *) shareTorrentURLs
{
    NSArray * torrents = [(Controller *)[NSApp delegate] selectedTorrents];
    NSMutableArray * fileURLs = [NSMutableArray arrayWithCapacity: [torrents count]];
    for (Torrent * torrent in torrents)
    {
        NSString * location = [torrent torrentLocation];
        if ([location length] > 0) {
            [fileURLs addObject: [NSURL fileURLWithPath: location]];
        }
    }
    return fileURLs;
}

- (NSArray *) menuItems
{
    NSArray * services = [NSSharingService sharingServicesForItems: [self shareTorrentURLs]];
    NSMutableArray * items = [NSMutableArray arrayWithCapacity: [services count]];
    for (NSSharingService * service in services)
    {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle: service.title // 10.9: change to menuItemTitle
                                                      action: @selector(performShareAction:)
                                               keyEquivalent: @""];
        item.image = service.image;
        item.representedObject = service;
        service.delegate = (Controller *)[NSApp delegate];
        item.target = self;
        [items addObject: item];
    }

    return items;
}

- (void) performShareAction: (NSMenuItem *) item
{
    NSSharingService * service = item.representedObject;
    [service performWithItems: [self shareTorrentURLs]]; // on 10.9, use attachmentFileURLs?
}

@end
