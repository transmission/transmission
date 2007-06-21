/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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
#import "InfoWindowController.h"

#define MAX_ACROSS 18
#define BETWEEN 1.0

@implementation PiecesView

- (id) initWithCoder: (NSCoder *) decoder
{
    if ((self = [super initWithCoder: decoder]))
    {
        fTorrent = nil;
        int numPieces = MAX_ACROSS * MAX_ACROSS;
        fPieces = malloc(numPieces);
    }
    
    return self;
}

- (void) awakeFromNib
{
        NSSize size = [self bounds].size;
        NSBezierPath * bp = [NSBezierPath bezierPathWithRect: [self bounds]];
        
        //back image
        fBack = [[NSImage alloc] initWithSize: size];
        
        [fBack lockFocus];
        [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.4] set];
        [bp fill];
        [fBack unlockFocus];
        
        //white box image
        fWhitePiece = [[NSImage alloc] initWithSize: size];
        
        [fWhitePiece lockFocus];
        [[NSColor whiteColor] set];
        [bp fill];
        [fWhitePiece unlockFocus];
        
        //orange box image
        fOrangePiece = [[NSImage alloc] initWithSize: size];
        
        [fOrangePiece lockFocus];
        [[NSColor orangeColor] set];
        [bp fill];
        [fOrangePiece unlockFocus];
        
        //green 1 box image
        fGreen1Piece = [[NSImage alloc] initWithSize: size];
        
        [fGreen1Piece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.6 green: 1.0 blue: 0.8 alpha: 1.0] set];
        [bp fill];
        [fGreen1Piece unlockFocus];
        
        //green 2 box image
        fGreen2Piece = [[NSImage alloc] initWithSize: size];
        
        [fGreen2Piece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.4 green: 1.0 blue: 0.6 alpha: 1.0] set];
        [bp fill];
        [fGreen2Piece unlockFocus];
        
        //green 3 box image
        fGreen3Piece = [[NSImage alloc] initWithSize: size];
        
        [fGreen3Piece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.0 green: 1.0 blue: 0.4 alpha: 1.0] set];
        [bp fill];
        [fGreen3Piece unlockFocus];
        
        //blue 1 box image
        fBlue1Piece = [[NSImage alloc] initWithSize: size];
        
        [fBlue1Piece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.8 green: 1.0 blue: 1.0 alpha: 1.0] set];
        [bp fill];
        [fBlue1Piece unlockFocus];
        
        //blue 2 box image
        fBlue2Piece = [[NSImage alloc] initWithSize: size];
        
        [fBlue2Piece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.6 green: 1.0 blue: 1.0 alpha: 1.0] set];
        [bp fill];
        [fBlue2Piece unlockFocus];
        
        //blue 3 box image
        fBlue3Piece = [[NSImage alloc] initWithSize: size];
        
        [fBlue3Piece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.6 green: 0.8 blue: 1.0 alpha: 1.0] set];
        [bp fill];
        [fBlue3Piece unlockFocus];
        
        //blue 4 box image
        fBlue4Piece = [[NSImage alloc] initWithSize: size];
        
        [fBlue4Piece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.4 green: 0.6 blue: 1.0 alpha: 1.0] set];
        [bp fill];
        [fBlue4Piece unlockFocus];
        
        //blue box image
        fBluePiece = [[NSImage alloc] initWithSize: size];
        
        [fBluePiece lockFocus];
        [[NSColor colorWithCalibratedRed: 0.0 green: 0.4 blue: 0.8 alpha: 1.0] set];
        [bp fill];
        [fBluePiece unlockFocus];
        
        //actually draw the box
        [self setTorrent: nil];
}

- (void) dealloc
{
    free(fPieces);
    
    [fBack release];
    [fWhitePiece release];
    [fOrangePiece release];
    [fBluePiece release];
    [fGreen1Piece release];
    [fGreen2Piece release];
    [fGreen3Piece release];
    
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
        int pieceCount = [fTorrent pieceCount];
        if (pieceCount < fNumPieces)
        {
            fNumPieces = pieceCount;
            
            fAcross = sqrt(fNumPieces);
            if (fAcross * fAcross < fNumPieces)
                fAcross++;
        }
        else
            fAcross = MAX_ACROSS;
        
        float width = [self bounds].size.width;
        fWidth = (width - (fAcross + 1) * BETWEEN) / fAcross;
        fExtraBorder = (width - ((fWidth + BETWEEN) * fAcross + BETWEEN)) / 2;
        
        [self updateView: YES];
    }
    else
    {
        [self setImage: [[fBack copy] autorelease]];
        [self setNeedsDisplay];
    }
}

- (void) updateView: (BOOL) first
{
    if (!fTorrent)
        return;
    
    if (first)
        [self setImage: [[fBack copy] autorelease]];
    NSImage * image = [self image];

    int8_t * pieces;
    float * piecesPercent;
    
    BOOL showAvailablity = [[NSUserDefaults standardUserDefaults] boolForKey: @"PiecesViewShowAvailability"];
    if (showAvailablity)
    {   
        pieces = malloc(fNumPieces);
        [fTorrent getAvailability: pieces size: fNumPieces];
    }
    else
    {   
        piecesPercent = malloc(fNumPieces * sizeof(float));
        [fTorrent getAmountFinished: piecesPercent size: fNumPieces];
    }
    
    int i, j, piece, index = -1;
    float piecePercent;
    NSPoint point;
    NSRect rect = NSMakeRect(0, 0, fWidth, fWidth);
    NSImage * pieceImage;
    BOOL change = NO;
        
    for (i = 0; i < fAcross; i++)
        for (j = 0; j < fAcross; j++)
        {
            index++;
            if (index >= fNumPieces)
            {
                i = fAcross;
                break;
            }
            
            pieceImage = nil;
            
            if (showAvailablity)
            {
                piece = pieces[index];
                if (piece < 0)
                {
                    if (first || fPieces[index] == -2)
                    {
                        fPieces[index] = -1;
                        pieceImage = fBluePiece;
                    }
                    else if (fPieces[index] != -1)
                    {
                        fPieces[index] = -2;
                        pieceImage = fOrangePiece;
                    }
                    else;
                }
                else if (piece == 0)
                {
                    if (first || fPieces[index] != 0)
                    {
                        fPieces[index] = 0;
                        pieceImage = fWhitePiece;
                    }
                }
                else if (piece <= 4)
                {
                    if (first || fPieces[index] != 1)
                    {
                        fPieces[index] = 1;
                        pieceImage = fGreen1Piece;
                    }
                }
                else if (piece <= 8)
                {
                    if (first || fPieces[index] != 2)
                    {
                        fPieces[index] = 2;
                        pieceImage = fGreen2Piece;
                    }
                }
                else
                {
                    if (first || fPieces[index] != 3)
                    {
                        fPieces[index] = 3;
                        pieceImage = fGreen3Piece;
                    }
                }
            }
            else
            {
                piecePercent = piecesPercent[index];
                if (piecePercent >= 1.0)
                {
                    if (first || fPieces[index] == -2)
                    {
                        fPieces[index] = -1;
                        pieceImage = fBluePiece;
                    }
                    else if (fPieces[index] != -1)
                    {
                        fPieces[index] = -2;
                        pieceImage = fOrangePiece;
                    }
                    else;
                }
                else if (piecePercent <= 0.0)
                {
                    if (first || fPieces[index] != 0)
                    {
                        fPieces[index] = 0;
                        pieceImage = fWhitePiece;
                    }
                }
                else if (piecePercent < 0.25)
                {
                    if (first || fPieces[index] != 1)
                    {
                        fPieces[index] = 1;
                        pieceImage = fBlue1Piece;
                    }
                }
                else if (piecePercent < 0.5)
                {
                    if (first || fPieces[index] != 2)
                    {
                        fPieces[index] = 2;
                        pieceImage = fBlue2Piece;
                    }
                }
                else if (piecePercent < 0.75)
                {
                    if (first || fPieces[index] != 3)
                    {
                        fPieces[index] = 3;
                        pieceImage = fBlue3Piece;
                    }
                }
                else
                {
                    if (first || fPieces[index] != 4)
                    {
                        fPieces[index] = 4;
                        pieceImage = fBlue4Piece;
                    }
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
                                    [image size].width - (i + 1) * (fWidth + BETWEEN) - fExtraBorder);
                [pieceImage compositeToPoint: point fromRect: rect operation: NSCompositeSourceOver];
            }
        }
    
    if (change)
    {
        [image unlockFocus];
        [self setNeedsDisplay];
    }
    
    if (showAvailablity)
        free(pieces);
    else
        free(piecesPercent);
}

- (BOOL) acceptsFirstMouse: (NSEvent *) event
{
    return YES;
}

- (void) mouseDown: (NSEvent *) event
{
    if (fTorrent)
        [[[self window] windowController] setPiecesViewForAvailable:
            ![[NSUserDefaults standardUserDefaults] boolForKey: @"PiecesViewShowAvailability"]];
    [super mouseDown: event];
}

@end
