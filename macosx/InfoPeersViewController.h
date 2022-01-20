// This file Copyright (c) 2010-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#import "InfoViewController.h"

@class WebSeedTableView;

@interface InfoPeersViewController : NSViewController<InfoViewController>
{
    NSArray* fTorrents;

    BOOL fSet;

    NSMutableArray* fPeers;
    NSMutableArray* fWebSeeds;

    IBOutlet NSTableView* fPeerTable;
    IBOutlet WebSeedTableView* fWebSeedTable;

    IBOutlet NSTextField* fConnectedPeersField;

    CGFloat fViewTopMargin;
    IBOutlet NSLayoutConstraint* fWebSeedTableTopConstraint;
}

- (void)setInfoForTorrents:(NSArray*)torrents;
- (void)updateInfo;

- (void)saveViewSize;
- (void)clearView;

@end
