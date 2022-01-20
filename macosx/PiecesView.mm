// This file Copyright (c) 2006-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#import "PiecesView.h"
#import "Torrent.h"
#import "InfoWindowController.h"

#define MAX_ACROSS 18
#define BETWEEN 1.0

#define HIGH_PEERS 30

enum
{
    PIECE_NONE,
    PIECE_SOME,
    PIECE_HIGH_PEERS,
    PIECE_FINISHED,
    PIECE_FLASHING
};

@implementation PiecesView

- (void)awakeFromNib
{
    //store box colors
    fGreenAvailabilityColor = [NSColor colorWithCalibratedRed:0.0 green:1.0 blue:0.4 alpha:1.0];
    fBluePieceColor = [NSColor colorWithCalibratedRed:0.0 green:0.4 blue:0.8 alpha:1.0];

    //actually draw the box
    [self setTorrent:nil];
}

- (void)dealloc
{
    tr_free(fPieces);
}

- (void)setTorrent:(Torrent*)torrent
{
    [self clearView];

    fTorrent = (torrent && !torrent.magnet) ? torrent : nil;
    if (fTorrent)
    {
        //determine relevant values
        fNumPieces = MIN(fTorrent.pieceCount, MAX_ACROSS * MAX_ACROSS);
        fAcross = ceil(sqrt(fNumPieces));

        CGFloat const width = self.bounds.size.width;
        fWidth = (width - (fAcross + 1) * BETWEEN) / fAcross;
        fExtraBorder = (width - ((fWidth + BETWEEN) * fAcross + BETWEEN)) / 2;
    }

    NSImage* back = [[NSImage alloc] initWithSize:self.bounds.size];
    [back lockFocus];

    NSGradient* gradient = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithCalibratedWhite:0.0 alpha:0.4]
                                                         endingColor:[NSColor colorWithCalibratedWhite:0.2 alpha:0.4]];
    [gradient drawInRect:self.bounds angle:90.0];
    [back unlockFocus];

    self.image = back;

    [self setNeedsDisplay];
}

- (void)clearView
{
    tr_free(fPieces);
    fPieces = NULL;
}

- (void)updateView
{
    if (!fTorrent)
    {
        return;
    }

    //determine if first time
    BOOL const first = fPieces == NULL;
    if (first)
    {
        fPieces = (int8_t*)tr_malloc(fNumPieces * sizeof(int8_t));
    }

    int8_t* pieces = NULL;
    float* piecesPercent = NULL;

    BOOL const showAvailablity = [NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];
    if (showAvailablity)
    {
        pieces = (int8_t*)tr_malloc(fNumPieces * sizeof(int8_t));
        [fTorrent getAvailability:pieces size:fNumPieces];
    }
    else
    {
        piecesPercent = (float*)tr_malloc(fNumPieces * sizeof(float));
        [fTorrent getAmountFinished:piecesPercent size:fNumPieces];
    }

    NSImage* image = self.image;

    NSRect fillRects[fNumPieces];
    NSColor* fillColors[fNumPieces];

    NSInteger usedCount = 0;

    for (NSInteger index = 0; index < fNumPieces; index++)
    {
        NSColor* pieceColor = nil;

        if (showAvailablity ? pieces[index] == -1 : piecesPercent[index] == 1.0)
        {
            if (first || fPieces[index] != PIECE_FINISHED)
            {
                if (!first && fPieces[index] != PIECE_FLASHING)
                {
                    pieceColor = NSColor.orangeColor;
                    fPieces[index] = PIECE_FLASHING;
                }
                else
                {
                    pieceColor = fBluePieceColor;
                    fPieces[index] = PIECE_FINISHED;
                }
            }
        }
        else if (showAvailablity ? pieces[index] == 0 : piecesPercent[index] == 0.0)
        {
            if (first || fPieces[index] != PIECE_NONE)
            {
                pieceColor = NSColor.whiteColor;
                fPieces[index] = PIECE_NONE;
            }
        }
        else if (showAvailablity && pieces[index] >= HIGH_PEERS)
        {
            if (first || fPieces[index] != PIECE_HIGH_PEERS)
            {
                pieceColor = fGreenAvailabilityColor;
                fPieces[index] = PIECE_HIGH_PEERS;
            }
        }
        else
        {
            //always redraw "mixed"
            CGFloat percent = showAvailablity ? (CGFloat)pieces[index] / HIGH_PEERS : piecesPercent[index];
            NSColor* fullColor = showAvailablity ? fGreenAvailabilityColor : fBluePieceColor;
            pieceColor = [NSColor.whiteColor blendedColorWithFraction:percent ofColor:fullColor];
            fPieces[index] = PIECE_SOME;
        }

        if (pieceColor)
        {
            NSInteger const across = index % fAcross;
            NSInteger const down = index / fAcross;
            fillRects[usedCount] = NSMakeRect(
                across * (fWidth + BETWEEN) + BETWEEN + fExtraBorder,
                image.size.width - (down + 1) * (fWidth + BETWEEN) - fExtraBorder,
                fWidth,
                fWidth);
            fillColors[usedCount] = pieceColor;

            usedCount++;
        }
    }

    if (usedCount > 0)
    {
        [image lockFocus];
        NSRectFillListWithColors(fillRects, fillColors, usedCount);
        [image unlockFocus];
        [self setNeedsDisplay];
    }

    tr_free(pieces);
    tr_free(piecesPercent);
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    return YES;
}

- (void)mouseDown:(NSEvent*)event
{
    if (fTorrent)
    {
        BOOL const availability = ![NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];
        [NSUserDefaults.standardUserDefaults setBool:availability forKey:@"PiecesViewShowAvailability"];

        [self sendAction:self.action to:self.target];
    }

    [super mouseDown:event];
}

@end
