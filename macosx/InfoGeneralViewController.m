/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2010-2011 Transmission authors and contributors
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

#import "InfoGeneralViewController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

@interface InfoGeneralViewController (Private)

- (void) setupInfo;

@end

@implementation InfoGeneralViewController

- (id) init
{
    if ((self = [super initWithNibName: @"InfoGeneralView" bundle: nil]))
    {
        [self setTitle: NSLocalizedString(@"General Info", "Inspector view -> title")];
    }
    
    return self;
}

- (void) dealloc
{
    [fTorrents release];
    
    [super dealloc];
}

- (void) setInfoForTorrents: (NSArray *) torrents
{
    //don't check if it's the same in case the metadata changed
    [fTorrents release];
    fTorrents = [torrents retain];
    
    fSet = NO;
}

- (void) updateInfo
{
    if (!fSet)
        [self setupInfo];
    
    if ([fTorrents count] != 1)
        return;
    
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    
    NSString * location = [torrent dataLocation];
    [fDataLocationField setStringValue: location ? [location stringByAbbreviatingWithTildeInPath] : @""];
    [fDataLocationField setToolTip: location ? location : @""];
    
    [fRevealDataButton setHidden: !location];
}

- (void) revealDataFile: (id) sender
{
    Torrent * torrent = [fTorrents objectAtIndex: 0];
    NSString * location = [torrent dataLocation];
    if (!location)
        return;
    
    NSURL * file = [NSURL fileURLWithPath: location];
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs: [NSArray arrayWithObject: file]];
}

@end

@implementation InfoGeneralViewController (Private)

- (void) setupInfo
{
    if ([fTorrents count] == 1)
    {
        Torrent * torrent = [fTorrents objectAtIndex: 0];
        
        NSString * piecesString = ![torrent isMagnet] ? [NSString stringWithFormat: @"%d, %@", [torrent pieceCount],
                                        [NSString stringForFileSize: [torrent pieceSize]]] : @"";
        [fPiecesField setStringValue: piecesString];
                                        
        NSString * hashString = [torrent hashString];
        [fHashField setStringValue: hashString];
        [fHashField setToolTip: hashString];
        [fSecureField setStringValue: [torrent privateTorrent]
                        ? NSLocalizedString(@"Private Torrent, non-tracker peer discovery disabled", "Inspector -> private torrent")
                        : NSLocalizedString(@"Public Torrent", "Inspector -> private torrent")];
        
        NSString * commentString = [torrent comment];
        [fCommentView setString: commentString];
        
        NSString * creatorString = [torrent creator];
        [fCreatorField setStringValue: creatorString];
        [fDateCreatedField setObjectValue: [torrent dateCreated]];
    }
    else
    {
        [fPiecesField setStringValue: @""];
        [fHashField setStringValue: @""];
        [fHashField setToolTip: nil];
        [fSecureField setStringValue: @""];
        [fCommentView setString: @""];
        
        [fCreatorField setStringValue: @""];
        [fDateCreatedField setStringValue: @""];
        
        [fDataLocationField setStringValue: @""];
        [fDataLocationField setToolTip: nil];
        
        [fRevealDataButton setHidden: YES];
    }
    
    fSet = YES;
}

@end

