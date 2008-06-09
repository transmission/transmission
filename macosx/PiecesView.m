/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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
#import "CTGradient.h"

#define MAX_ACROSS 18
#define BETWEEN 1.0

@implementation PiecesView

- (void) awakeFromNib
{
        //back image
        fBack = [[NSImage alloc] initWithSize: [self bounds].size];
        
        [fBack lockFocus];
        CTGradient * gradient = [CTGradient gradientWithBeginningColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.4]
                                    endingColor: [NSColor colorWithCalibratedWhite: 0.2 alpha: 0.4]];
        [gradient fillRect: [self bounds] angle: 90.0];
        [fBack unlockFocus];
        
        //store box colors
        fWhiteColor = [[NSColor whiteColor] retain];
        fOrangeColor = [[NSColor orangeColor] retain];
        fGreen1Color = [[NSColor colorWithCalibratedRed: 0.6 green: 1.0 blue: 0.8 alpha: 1.0] retain];
        fGreen2Color = [[NSColor colorWithCalibratedRed: 0.4 green: 1.0 blue: 0.6 alpha: 1.0] retain];
        fGreen3Color = [[NSColor colorWithCalibratedRed: 0.0 green: 1.0 blue: 0.4 alpha: 1.0] retain];
        fBlue1Color = [[NSColor colorWithCalibratedRed: 0.8 green: 1.0 blue: 1.0 alpha: 1.0] retain];
        fBlue2Color = [[NSColor colorWithCalibratedRed: 0.6 green: 1.0 blue: 1.0 alpha: 1.0] retain];
        fBlue3Color = [[NSColor colorWithCalibratedRed: 0.6 green: 0.8 blue: 1.0 alpha: 1.0] retain];
        fBlue4Color = [[NSColor colorWithCalibratedRed: 0.4 green: 0.6 blue: 1.0 alpha: 1.0] retain];
        fBlueColor = [[NSColor colorWithCalibratedRed: 0.0 green: 0.4 blue: 0.8 alpha: 1.0] retain];
        
        //actually draw the box
        [self setTorrent: nil];
}

- (void) dealloc
{
    tr_free(fPieces);
    
    [fBack release];
    
    [fWhiteColor release];
    [fOrangeColor release];
    [fGreen1Color release];
    [fGreen2Color release];
    [fGreen3Color release];
    [fBlue1Color release];
    [fBlue2Color release];
    [fBlue3Color release];
    [fBlue4Color release];
    [fBlueColor release];
    
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
        
        float width = [self bounds].size.width;
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
    
    NSImage * image = [self image];

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
    
    int i, j, index = -1;
    NSRect rect = NSMakeRect(0, 0, fWidth, fWidth);
    
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
            
            NSColor * pieceColor = nil;
            
            if (showAvailablity)
            {
                int piece = pieces[index];
                if (piece == -1)
                {
                    if (first || fPieces[index] == -2)
                    {
                        fPieces[index] = -1;
                        pieceColor = fBlueColor;
                    }
                    else if (fPieces[index] != -1)
                    {
                        fPieces[index] = -2;
                        pieceColor = fOrangeColor;
                    }
                    else;
                }
                else if (piece == 0)
                {
                    if (first || fPieces[index] != 0)
                    {
                        fPieces[index] = 0;
                        pieceColor = fWhiteColor;
                    }
                }
                else if (piece <= 4)
                {
                    if (first || fPieces[index] != 1)
                    {
                        fPieces[index] = 1;
                        pieceColor = fGreen1Color;
                    }
                }
                else if (piece <= 8)
                {
                    if (first || fPieces[index] != 2)
                    {
                        fPieces[index] = 2;
                        pieceColor = fGreen2Color;
                    }
                }
                else
                {
                    if (first || fPieces[index] != 3)
                    {
                        fPieces[index] = 3;
                        pieceColor = fGreen3Color;
                    }
                }
            }
            else
            {
                float piecePercent = piecesPercent[index];
                if (piecePercent >= 1.0)
                {
                    if (first || fPieces[index] == -2)
                    {
                        fPieces[index] = -1;
                        pieceColor = fBlueColor;
                    }
                    else if (fPieces[index] != -1)
                    {
                        fPieces[index] = -2;
                        pieceColor = fOrangeColor;
                    }
                    else;
                }
                else if (piecePercent == 0.0)
                {
                    if (first || fPieces[index] != 0)
                    {
                        fPieces[index] = 0;
                        pieceColor = fWhiteColor;
                    }
                }
                else if (piecePercent < 0.25)
                {
                    if (first || fPieces[index] != 1)
                    {
                        fPieces[index] = 1;
                        pieceColor = fBlue1Color;
                    }
                }
                else if (piecePercent < 0.5)
                {
                    if (first || fPieces[index] != 2)
                    {
                        fPieces[index] = 2;
                        pieceColor = fBlue2Color;
                    }
                }
                else if (piecePercent < 0.75)
                {
                    if (first || fPieces[index] != 3)
                    {
                        fPieces[index] = 3;
                        pieceColor = fBlue3Color;
                    }
                }
                else
                {
                    if (first || fPieces[index] != 4)
                    {
                        fPieces[index] = 4;
                        pieceColor = fBlue4Color;
                    }
                }
            }
            
            if (pieceColor)
            {
                //drawing actually will occur
                if (!change)
                {
                    [image lockFocus];
                    change = YES;
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
