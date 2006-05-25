//
//  InfoWindowController.h
//  Transmission
//
//  Created by Mitchell Livingston on 5/22/06.
//  Copyright 2006 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Torrent.h"

@interface InfoWindowController : NSWindowController
{
    IBOutlet NSImageView        * fImageView;
    IBOutlet NSTextField        * fName;
    IBOutlet NSTextField        * fSize;
    IBOutlet NSTextField        * fTracker;
    IBOutlet NSTextField        * fAnnounce;
    IBOutlet NSTextField        * fPieceSize;
    IBOutlet NSTextField        * fPieces;
    IBOutlet NSTextField        * fHash;
    IBOutlet NSTextField        * fSeeders;
    IBOutlet NSTextField        * fLeechers;
    IBOutlet NSTextField        * fDownloaded;
    IBOutlet NSTextField        * fUploaded;
    
    NSImage                     * fAppIcon;
}

- (void) updateInfoForTorrents: (NSArray *) torrents;
- (void) updateInfoStatsForTorrents: (NSArray *) torrents;

@end
