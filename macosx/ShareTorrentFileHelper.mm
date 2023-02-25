// This file Copyright © 2014-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.
// Created by Mitchell Livingston on 1/10/14.

#import "ShareTorrentFileHelper.h"
#import "Controller.h"
#import "Torrent.h"

@implementation ShareTorrentFileHelper

+ (ShareTorrentFileHelper*)sharedHelper
{
    static ShareTorrentFileHelper* helper;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        helper = [[ShareTorrentFileHelper alloc] init];
    });
    return helper;
}

- (NSArray<NSURL*>*)shareTorrentURLs
{
    NSArray* torrents = ((Controller*)NSApp.delegate).selectedTorrents;
    NSMutableArray* fileURLs = [NSMutableArray arrayWithCapacity:torrents.count];
    for (Torrent* torrent in torrents)
    {
        NSString* location = torrent.torrentLocation;
        if (location.length > 0)
        {
            [fileURLs addObject:[NSURL fileURLWithPath:location]];
        }
    }
    return fileURLs;
}

- (NSArray<NSMenuItem*>*)menuItems
{
    NSArray* services = [NSSharingService sharingServicesForItems:self.shareTorrentURLs];
    NSMutableArray* items = [NSMutableArray arrayWithCapacity:services.count];
    for (NSSharingService* service in services)
    {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:service.title // 10.9: change to menuItemTitle
                                                      action:@selector(performShareAction:)
                                               keyEquivalent:@""];
        item.image = service.image;
        item.representedObject = service;
        service.delegate = (Controller*)NSApp.delegate;
        item.target = self;
        [items addObject:item];
    }

    return items;
}

- (void)performShareAction:(NSMenuItem*)item
{
    NSSharingService* service = item.representedObject;
    [service performWithItems:self.shareTorrentURLs]; // on 10.9, use attachmentFileURLs?
}

@end
