// This file Copyright (c) 2006-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@class Torrent;

@interface PiecesView : NSImageView
{
    int8_t* fPieces;

    NSColor* fGreenAvailabilityColor;
    NSColor* fBluePieceColor;

    Torrent* fTorrent;
    NSInteger fNumPieces;
    NSInteger fAcross;
    NSInteger fWidth;
    NSInteger fExtraBorder;
}

- (void)setTorrent:(Torrent*)torrent;

- (void)clearView;
- (void)updateView;

@end
