// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "TorrentCell.h"
#import "ProgressBarView.h"
#import "ProgressGradients.h"
#import "Torrent.h"

@implementation TorrentCell

//draw progress bar
- (void)drawRect:(NSRect)dirtyRect
{
    if (self.fTorrentTableView)
    {
        NSRect barRect = self.fTorrentProgressBarView.frame;
        ProgressBarView* progressBar = [[ProgressBarView alloc] init];
        Torrent* torrent = (Torrent*)self.objectValue;

        [progressBar drawBarInRect:barRect forTableView:self.fTorrentTableView withTorrent:torrent];
    }

    [super drawRect:dirtyRect];
}

//otherwise progress bar is inverted
- (BOOL)isFlipped
{
    return YES;
}

@end
