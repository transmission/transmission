//
//  PiecesWindowController.h
//  Transmission
//
//  Created by Livingston on 9/23/06.
//  Copyright 2006 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Torrent.h"

@interface PiecesView : NSObject
{
    int8_t  * fPieces;
    
    NSImage * fExistingImage, * fBack, * fWhitePiece, * fGreenPiece,
            * fBlue1Piece, * fBlue2Piece, * fBlue3Piece;
    
    Torrent * fTorrent;
    int     fNumPieces, fAcross, fWidth, fExtraBorder;
    
    IBOutlet NSImageView * fImageView;
}

- (void) setTorrent: (Torrent *) torrent;
- (void) updateView: (BOOL) first;

@end
