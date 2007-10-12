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
        fPieces = malloc(MAX_ACROSS * MAX_ACROSS);
    }
    
    return self;
}

- (void) awakeFromNib
{
        NSBezierPath * bp = [NSBezierPath bezierPathWithRect: [self bounds]];
        
        //back image
        fBack = [[NSImage alloc] initWithSize: [self bounds].size];
        
        [fBack lockFocus];
        [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.4] set];
        [bp fill];
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
    free(fPieces);
    
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
    
    [fTorrent release];
    [super dealloc];
}

- (void) setTorrent: (Torrent *) torrent
{
    [fTorrent release];
    
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
        fTorrent = nil;
        
        NSImage * newBack = [fBack copy];
        [self setImage: newBack];
        [newBack release];
        
        [self setNeedsDisplay];
    }
}

- (void) updateView: (BOOL) first
{
    if (!fTorrent)
        return;
    
    if (first)
    {
        NSImage * newBack = [fBack copy];
        [self setImage: newBack];
        [newBack release];
    }
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
    NSRect rect = NSMakeRect(0, 0, fWidth, fWidth);
    NSColor * pieceColor;
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
            
            pieceColor = nil;
            
            if (showAvailablity)
            {
                piece = pieces[index];
                if (piece < 0)
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
                piecePercent = piecesPercent[index];
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
                else if (piecePercent <= 0.0)
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
                
                [pieceColor set];
                
                rect.origin = NSMakePoint(j * (fWidth + BETWEEN) + BETWEEN + fExtraBorder,
                                    [image size].width - (i + 1) * (fWidth + BETWEEN) - fExtraBorder);
                NSRectFill(rect);
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
