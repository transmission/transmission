//
//  PiecesWindowController.m
//  Transmission
//
//  Created by Livingston on 9/23/06.
//  Copyright 2006 __MyCompanyName__. All rights reserved.
//

#import "PiecesView.h"

#define MAX_ACROSS 18
#define BETWEEN 1.0

#define BLANK -99

@implementation PiecesView

- (id) init
{
    if ((self = [super init]))
    {
        fTorrent = nil;
        int numPieces = MAX_ACROSS * MAX_ACROSS;
        fPieces = malloc(numPieces);
        int i;
        for (i = 0; i < numPieces; i++)
            fPieces[i] = BLANK;
        
        fBack = [NSImage imageNamed: @"PiecesBack.tiff"];
        NSSize size = [fBack size];
        
        fWhitePiece = [NSImage imageNamed: @"BoxWhite.tiff"];
        [fWhitePiece setScalesWhenResized: YES];
        [fWhitePiece setSize: size];
        
        fGreenPiece = [NSImage imageNamed: @"BoxGreen.tiff"];
        [fGreenPiece setScalesWhenResized: YES];
        [fGreenPiece setSize: size];
        
        fBlue1Piece = [NSImage imageNamed: @"BoxBlue1.tiff"];
        [fBlue1Piece setScalesWhenResized: YES];
        [fBlue1Piece setSize: size];
        
        fBlue2Piece = [NSImage imageNamed: @"BoxBlue2.tiff"];
        [fBlue2Piece setScalesWhenResized: YES];
        [fBlue2Piece setSize: size];
        
        fBlue3Piece = [NSImage imageNamed: @"BoxBlue3.tiff"];
        [fBlue3Piece setScalesWhenResized: YES];
        [fBlue3Piece setSize: size];
        
        [self setTorrent: nil];
    }
    
    return self;
}

- (void) dealloc
{
    free(fPieces);
    
    if (fTorrent)
        [fTorrent release];
    [super dealloc];
}

- (void) setTorrent: (Torrent *) torrent
{
    if (fTorrent)
    {
        [fTorrent release];
        
        if (!torrent)
        {
            fTorrent = nil;
            [fImageView setImage: [[fBack copy] autorelease]];
        }
    }
    
    if (torrent)
    {
        fTorrent = [torrent retain];
        
        //determine relevant values
        fNumPieces = MAX_ACROSS * MAX_ACROSS;
        if ([fTorrent pieceCount] < fNumPieces)
        {
            fNumPieces = [fTorrent pieceCount];
            
            fAcross = sqrt(fNumPieces);
            if (fAcross * fAcross < fNumPieces)
                fAcross++;
        }
        else
            fAcross = MAX_ACROSS;
        
        fWidth = ([[fImageView image] size].width - (fAcross + 1) * BETWEEN) / fAcross;
        fExtraBorder = ([[fImageView image] size].width - ((fWidth + BETWEEN) * fAcross + BETWEEN)) / 2;
        
        [self updateView: YES];
    }
    
    [fImageView setHidden: torrent == nil];
}

- (void) updateView: (BOOL) first
{
    if (!fTorrent)
        return;
    
    if (first)
        [fImageView setImage: [[fBack copy] autorelease]];
    
    NSImage * image = [fImageView image];
    
    int8_t * pieces = malloc(fNumPieces);
    [fTorrent getAvailability: pieces size: fNumPieces];
    
    int i, j, piece, index = -1;
    NSPoint point;
    NSRect rect = NSMakeRect(0, 0, fWidth, fWidth);
    NSImage * pieceImage;
    BOOL change = NO;
        
    for (i = 0; i < fAcross; i++)
        for (j = 0; j < fAcross; j++)
        {
            index++;
            if (index >= fNumPieces)
                break;
            
            pieceImage = nil;
            
            piece = pieces[index];
            if (piece < 0)
            {
                if (first || fPieces[index] != -1)
                {
                    fPieces[index] = -1;
                    pieceImage = fGreenPiece;
                }
            }
            else if (piece == 0)
            {
                if (first || fPieces[index] != 0)
                {
                    fPieces[index] = 0;
                    pieceImage = fWhitePiece;
                }
            }
            else if (piece == 1)
            {
                if (first || fPieces[index] != 1)
                {
                    fPieces[index] = 1;
                    pieceImage = fBlue1Piece;
                }
            }
            else if (piece == 2)
            {
                if (first || fPieces[index] != 2)
                {
                    fPieces[index] = 2;
                    pieceImage = fBlue2Piece;
                }
            }
            else
            {
                if (first || fPieces[index] != 3)
                {
                    fPieces[index] = 3;
                    pieceImage = fBlue3Piece;
                }
            }
            
            if (pieceImage)
            {
                //drawing actually will occur, so figure out values
                if (!change)
                {
                    [image lockFocus];
                    change = YES;
                }
                
                point = NSMakePoint(j * (fWidth + BETWEEN) + BETWEEN + fExtraBorder,
                                    [[fImageView image] size].width - (i + 1) * (fWidth + BETWEEN) - fExtraBorder);
                
                [pieceImage compositeToPoint: point fromRect: rect operation: NSCompositeSourceOver];
            }
        }
    
    if (change)
    {
        [image unlockFocus];
        [fImageView setNeedsDisplay];
    }
    
    free(pieces);
}

@end
