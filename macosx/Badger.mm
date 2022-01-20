// This file Copyright © 2006-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "Badger.h"
#import "BadgeView.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

@implementation Badger

- (instancetype)initWithLib:(tr_session*)lib
{
    if ((self = [super init]))
    {
        fLib = lib;

        BadgeView* view = [[BadgeView alloc] initWithLib:lib];
        NSApp.dockTile.contentView = view;

        fHashes = [[NSMutableSet alloc] init];
    }

    return self;
}

- (void)updateBadgeWithDownload:(CGFloat)downloadRate upload:(CGFloat)uploadRate
{
    CGFloat const displayDlRate = [NSUserDefaults.standardUserDefaults boolForKey:@"BadgeDownloadRate"] ? downloadRate : 0.0;
    CGFloat const displayUlRate = [NSUserDefaults.standardUserDefaults boolForKey:@"BadgeUploadRate"] ? uploadRate : 0.0;

    //only update if the badged values change
    if ([(BadgeView*)NSApp.dockTile.contentView setRatesWithDownload:displayDlRate upload:displayUlRate])
    {
        [NSApp.dockTile display];
    }
}

- (void)addCompletedTorrent:(Torrent*)torrent
{
    NSParameterAssert(torrent != nil);

    [fHashes addObject:torrent.hashString];
    NSApp.dockTile.badgeLabel = [NSString formattedUInteger:fHashes.count];
}

- (void)removeTorrent:(Torrent*)torrent
{
    if ([fHashes member:torrent.hashString])
    {
        [fHashes removeObject:torrent.hashString];
        if (fHashes.count > 0)
        {
            NSApp.dockTile.badgeLabel = [NSString formattedUInteger:fHashes.count];
        }
        else
        {
            NSApp.dockTile.badgeLabel = @"";
        }
    }
}

- (void)clearCompleted
{
    if (fHashes.count > 0)
    {
        [fHashes removeAllObjects];
        NSApp.dockTile.badgeLabel = @"";
    }
}

- (void)setQuitting
{
    [self clearCompleted];
    [(BadgeView*)NSApp.dockTile.contentView setQuitting];
    [NSApp.dockTile display];
}

@end
