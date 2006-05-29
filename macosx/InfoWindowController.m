/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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
