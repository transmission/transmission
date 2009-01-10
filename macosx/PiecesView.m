/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2009 Transmission authors and contributors
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
#import "Torrent.h"
#import "InfoWindowController.h"
#import "utils.h"

#define MAX_ACROSS 18
#define BETWEEN 1.0f

#define HIGH_PEERS 30

#define PIECE_NONE 0
#define PIECE_SOME 1
#define PIECE_HIGH_PEERS 2
#define PIECE_FINISHED 3
#define PIECE_FLASHING 4

@implementation PiecesView

- (void) awakeFromNib
{
    //back image
    fBack = [[NSImage alloc] initWithSize: [self bounds].size];
    
    [fBack lockFocus];
    NSGradient * gradient = [[NSGradient alloc] initWithStartingColor: [NSColor colorWithCalibratedWhite: 0.0f alpha: 0.4f]
                                endingColor: [NSColor colorWithCalibratedWhite: 0.2f alpha: 0.4f]];
    [gradient drawInRect: [self bounds] angle: 90.0f];
    [gradient release];
    [fBack unlockFocus];
    
    //store box colors
    fGreenAvailabilityColor = [[NSColor colorWithCalibratedRed: 0.0f green: 1.0f blue: 0.4f alpha: 1.0f] retain];
    fBluePieceColor = [[NSColor colorWithCalibratedRed: 0.0f green: 0.4f blue: 0.8f alpha: 1.0f] retain];
            
    //actually draw the box
    [self setTorrent: nil];
}

- (void) dealloc
{
    tr_free(fPieces);
    
    [fBack release];
    
    [fGreenAvailabilityColor release];
    [fBluePieceColor release];
    
    [super dealloc];
}

- (void) setTorrent: (Torrent *) torrent
{
    [self clearView];
    
    fTorrent = torrent;
    if (fTorrent)
    {
        //determine relevant values
        fNumPieces = MIN([fTorrent pieceCount], MAX_ACROSS * MAX_ACROSS);
        fAcross = ceil(sqrt(fNumPieces));
        
        CGFloat width = [self bounds].size.width;
        fWidth = (width - (fAcross + 1) * BETWEEN) / fAcross;
        fExtraBorder = (width - ((fWidth + BETWEEN) * fAcross + BETWEEN)) / 2;
    }
    
    //reset the view to blank
    NSImage * newBack = [fBack copy];
    [self setImage: newBack];
    [newBack release];
    
    [self setNeedsDisplay];
}

- (void) clearView
{
    tr_free(fPieces);
    fPieces = NULL;
}

- (void) updateView
{
    if (!fTorrent)
        return;
    
    //determine if first time
    BOOL first = NO;
    if (!fPieces)
    {
        fPieces = (int8_t *)tr_malloc(fNumPieces * sizeof(int8_t));
        first = YES;
    }

    int8_t * pieces = NULL;
    float * piecesPercent = NULL;
    
    BOOL showAvailablity = [[NSUserDefaults standardUserDefaults] boolForKey: @"PiecesViewShowAvailability"];
    if (showAvailablity)
    {   
        pieces = (int8_t *)tr_malloc(fNumPieces * sizeof(int8_t));
        [fTorrent getAvailability: pieces size: fNumPieces];
    }
    else
    {   
        piecesPercent = (float *)tr_malloc(fNumPieces * sizeof(float));
        [fTorrent getAmountFinished: piecesPercent size: fNumPieces];
    }
    
    NSImage * image = [self image];
    
    NSInteger index = -1;
    NSRect rect = NSMakeRect(0, 0, fWidth, fWidth);
    BOOL change = NO;
    
    for (NSInteger i = 0; i < fAcross; i++)
        for (NSInteger j = 0; j < fAcross; j++)
        {
            index++;
            if (index >= fNumPieces)
            {
                i = fAcross;
                break;
            }
            
            NSColor * pieceColor = nil;
            
            if (showAvailablity ? pieces[index] == -1 : piecesPercent[index] == 1.0f)
            {
                if (first || fPieces[index] != PIECE_FINISHED)
                {
                    if (!first && fPieces[index] != PIECE_FLASHING)
                    {
                        pieceColor = [NSColor orangeColor];
                        fPieces[index] = PIECE_FLASHING;
                    }
                    else
                    {
                        pieceColor = fBluePieceColor;
                        fPieces[index] = PIECE_FINISHED;
                    }
                }
            }
            else if (showAvailablity ? pieces[index] == 0 : piecesPercent[index] == 0.0f)
            {
                if (first || fPieces[index] != PIECE_NONE)
                {
                    pieceColor = [NSColor whiteColor];
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
                CGFloat percent = showAvailablity ? (CGFloat)pieces[index]/HIGH_PEERS : piecesPercent[index];
                NSColor * fullColor = showAvailablity ? fGreenAvailabilityColor : fBluePieceColor;
                pieceColor = [[NSColor whiteColor] blendedColorWithFraction: percent ofColor: fullColor];
                fPieces[index] = PIECE_SOME;
            }
            
            if (pieceColor)
            {
                //avoid unneeded memory usage by only locking focus if drawing will occur
                if (!change)
                {
                    change = YES;
                    [image lockFocus];
                }
                
                rect.origin = NSMakePoint(j * (fWidth + BETWEEN) + BETWEEN + fExtraBorder,
                                        [image size].width - (i + 1) * (fWidth + BETWEEN) - fExtraBorder);
                
                [pieceColor set];
                NSRectFill(rect);
            }
        }
    
    if (change)
    {
        [image unlockFocus];
        [self setNeedsDisplay];
    }
    
    tr_free(pieces);
    tr_free(piecesPercent);
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
