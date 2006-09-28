/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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
    }
    
    return self;
}

- (void) awakeFromNib
{
        NSSize size = [fImageView frame].size;
        NSBezierPath * bp = [NSBezierPath bezierPathWithRect: [fImageView bounds]];
        
        //back image
        fBack = [[NSImage alloc] initWithSize: size];
        
        [fBack lockFocus];
        [[NSColor blackColor] set];
        [bp fill];
        [fBack unlockFocus];
        
        //white box image
        fWhitePiece = [[NSImage alloc] initWithSize: size];
        
        [fWhitePiece lockFocus];
        [[NSColor whiteColor] set];
        [bp fill];
        [fWhitePiece unlockFocus];
        
        //green box image
        fGreenPiece = [[NSImage alloc] initWithSize: size];
        
        [fGreenPiece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.557 green: 0.992 blue: 0.639 alpha: 1.0] set];
        [bp fill];
        [fGreenPiece unlockFocus];
        
        //blue 1 box image
        fBlue1Piece = [[NSImage alloc] initWithSize: size];
        
        [fBlue1Piece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.777 green: 0.906 blue: 1.0 alpha: 1.0] set];
        [bp fill];
        [fBlue1Piece unlockFocus];
        
        //blue 2 box image
        fBlue2Piece = [[NSImage alloc] initWithSize: size];
        
        [fBlue2Piece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.682 green: 0.839 blue: 1.0 alpha: 1.0] set];
        [bp fill];
        [fBlue2Piece unlockFocus];
        
        //blue 3 box image
        fBlue3Piece = [[NSImage alloc] initWithSize: size];
        
        [fBlue3Piece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.506 green: 0.745 blue: 1.0 alpha: 1.0] set];
        [bp fill];
        [fBlue3Piece unlockFocus];
        
        //actually draw the box
        [self setTorrent: nil];
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
            fTorrent = nil;
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
