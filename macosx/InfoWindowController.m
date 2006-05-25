//
//  InfoWindowController.m
//  Transmission
//
//  Created by Mitchell Livingston on 5/22/06.
//  Copyright 2006 __MyCompanyName__. All rights reserved.
//

#import "InfoWindowController.h"
#import "StringAdditions.h"

@implementation InfoWindowController

- (void) awakeFromNib
{
    fAppIcon = [[NSApp applicationIconImage] copy];
}

- (void) dealloc
{
    [fAppIcon release];
    [super dealloc];
}

- (void) updateInfoForTorrents: (NSArray *) torrents
{
    int numberSelected = [torrents count];
    if (numberSelected != 1)
    {
        [fImageView setImage: fAppIcon];

        [fTracker setStringValue: @""];
        [fAnnounce setStringValue: @""];
        [fPieceSize setStringValue: @""];
        [fPieces setStringValue: @""];
        [fHash setStringValue: @""];
        
        [fSeeders setStringValue: @""];
        [fLeechers setStringValue: @""];
        
        if (numberSelected > 0)
        {
            [fName setStringValue: [[NSString stringWithInt: numberSelected]
                                    stringByAppendingString: @" Torrents Selected"]];
        
            uint64_t size = 0;
            NSEnumerator * enumerator = [torrents objectEnumerator];
            Torrent * torrent;
            while ((torrent = [enumerator nextObject]))
                size += [torrent size];
            
            [fSize setStringValue: [[NSString
                stringForFileSize: size] stringByAppendingString: @" Total"]];
        }
        else
        {
            [fName setStringValue: @"No Torrents Selected"];

            [fSize setStringValue: @""];
            [fDownloaded setStringValue: @""];
            [fUploaded setStringValue: @""];
        }
    }
    else
    {    
        Torrent * torrent = [torrents objectAtIndex: 0];
        
        [fImageView setImage: [torrent iconNonFlipped]];
        
        [fName setStringValue: [torrent name]];
        [fSize setStringValue: [NSString stringForFileSize: [torrent size]]];
        [fTracker setStringValue: [torrent tracker]];
        [fAnnounce setStringValue: [torrent announce]];
        [fPieceSize setStringValue: [NSString stringForFileSize: [torrent pieceSize]]];
        [fPieces setIntValue: [torrent pieceCount]];
        [fHash setStringValue: [torrent hash]];
    }

    [self updateInfoStatsForTorrents: torrents];
}

- (void) updateInfoStatsForTorrents: (NSArray *) torrents
{
    int numberSelected = [torrents count];
    if (numberSelected > 0)
    {
        Torrent * torrent;
        if (numberSelected == 1)
        {    
            torrent = [torrents objectAtIndex: 0];
            
            int seeders = [torrent seeders], leechers = [torrent leechers];
            [fSeeders setStringValue: seeders < 0 ?
                @"?" : [NSString stringWithInt: seeders]];
            [fLeechers setStringValue: leechers < 0 ?
                @"?" : [NSString stringWithInt: leechers]];
        }
    
        uint64_t downloaded = 0, uploaded = 0;
        NSEnumerator * enumerator = [torrents objectEnumerator];
        while ((torrent = [enumerator nextObject]))
        {
            downloaded += [torrent downloaded];
            uploaded += [torrent uploaded];
        }
        
        [fDownloaded setStringValue: [NSString stringForFileSize: downloaded]];
        [fUploaded setStringValue: [NSString stringForFileSize: uploaded]];
    }
}

@end
