/******************************************************************************
 * Copyright (c) 2006-2012 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import "Badger.h"
#import "BadgeView.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

@implementation Badger

- (id) initWithLib: (tr_session *) lib
{
    if ((self = [super init]))
    {
        fLib = lib;

        BadgeView * view = [[BadgeView alloc] initWithLib: lib];
        [[NSApp dockTile] setContentView: view];

        fHashes = [[NSMutableSet alloc] init];
    }

    return self;
}


- (void) updateBadgeWithDownload: (CGFloat) downloadRate upload: (CGFloat) uploadRate
{
    const CGFloat displayDlRate = [[NSUserDefaults standardUserDefaults] boolForKey: @"BadgeDownloadRate"]
                                    ? downloadRate : 0.0;
    const CGFloat displayUlRate = [[NSUserDefaults standardUserDefaults] boolForKey: @"BadgeUploadRate"]
                                    ? uploadRate : 0.0;

    //only update if the badged values change
    if ([(BadgeView *)[[NSApp dockTile] contentView] setRatesWithDownload: displayDlRate upload: displayUlRate])
        [[NSApp dockTile] display];
}

- (void) addCompletedTorrent: (Torrent *) torrent
{
    NSParameterAssert(torrent != nil);

    [fHashes addObject: [torrent hashString]];
    [[NSApp dockTile] setBadgeLabel: [NSString formattedUInteger: [fHashes count]]];
}

- (void) removeTorrent: (Torrent *) torrent
{
    if ([fHashes member: [torrent hashString]])
    {
        [fHashes removeObject: [torrent hashString]];
        if ([fHashes count] > 0)
            [[NSApp dockTile] setBadgeLabel: [NSString formattedUInteger: [fHashes count]]];
        else
            [[NSApp dockTile] setBadgeLabel: @""];
    }
}

- (void) clearCompleted
{
    if ([fHashes count] > 0)
    {
        [fHashes removeAllObjects];
        [[NSApp dockTile] setBadgeLabel: @""];
    }
}

- (void) setQuitting
{
    [self clearCompleted];
    [(BadgeView *)[[NSApp dockTile] contentView] setQuitting];
    [[NSApp dockTile] display];
}

@end
