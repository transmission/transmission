// This file Copyright © 2006-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <vector>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#import "PiecesView.h"
#import "Torrent.h"
#import "InfoWindowController.h"
#import "NSApplicationAdditions.h"

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

@interface PiecesView ()

@property(nonatomic) std::vector<int8_t> fPieces;

@property(nonatomic) NSInteger fNumPieces;
@property(nonatomic) NSInteger fAcross;
@property(nonatomic) NSInteger fWidth;
@property(nonatomic) NSInteger fExtraBorder;

@end

@implementation PiecesView

- (void)drawRect:(NSRect)dirtyRect
{
    [[NSColor.controlTextColor colorWithAlphaComponent:0.2] setFill];
    NSRectFill(dirtyRect);
    [super drawRect:dirtyRect];
}

- (void)awakeFromNib
{
    self.torrent = nil;
}

- (void)viewDidChangeEffectiveAppearance
{
    self.torrent = _torrent;
    [self updateView];
}

- (void)dealloc
{
}

- (void)setTorrent:(Torrent*)torrent
{
    [self clearView];

    _torrent = (torrent && !torrent.magnet) ? torrent : nil;
    if (_torrent)
    {
        //determine relevant values
        _fNumPieces = MIN(_torrent.pieceCount, MAX_ACROSS * MAX_ACROSS);
        _fAcross = ceil(sqrt(_fNumPieces));

        CGFloat const width = self.bounds.size.width;
        _fWidth = (width - (_fAcross + 1) * BETWEEN) / _fAcross;
        _fExtraBorder = (width - ((_fWidth + BETWEEN) * _fAcross + BETWEEN)) / 2;
    }

    NSImage* back = [[NSImage alloc] initWithSize:self.bounds.size];
    self.image = back;

    [self setNeedsDisplay];
}

- (void)clearView
{
    self.fPieces.clear();
}

- (void)updateView
{
    if (!self.torrent)
    {
        return;
    }

    //determine if first time
    BOOL const first = std::empty(self.fPieces);
    if (first)
    {
        _fPieces.resize(self.fNumPieces);
    }

    auto pieces = std::vector<int8_t>{};
    auto piecesPercent = std::vector<float>{};

    BOOL const showAvailability = [NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];
    if (showAvailability)
    {
        pieces.resize(self.fNumPieces);
        [self.torrent getAvailability:std::data(pieces) size:std::size(pieces)];
    }
    else
    {
        piecesPercent.resize(self.fNumPieces);
        [self.torrent getAmountFinished:std::data(piecesPercent) size:std::size(piecesPercent)];
    }

    NSImage* image = self.image;

    NSRect fillRects[self.fNumPieces];
    NSColor* fillColors[self.fNumPieces];

    NSColor* defaultColor = NSApp.darkMode ? NSColor.blackColor : NSColor.whiteColor;

    NSInteger usedCount = 0;

    for (NSInteger index = 0; index < self.fNumPieces; index++)
    {
        NSColor* pieceColor = nil;

        if (showAvailability ? pieces[index] == -1 : piecesPercent[index] == 1.0)
        {
            if (first || self.fPieces[index] != PIECE_FINISHED)
            {
                if (!first && self.fPieces[index] != PIECE_FLASHING)
                {
                    pieceColor = NSColor.systemOrangeColor;
                    self.fPieces[index] = PIECE_FLASHING;
                }
                else
                {
                    pieceColor = NSColor.systemBlueColor;
                    self.fPieces[index] = PIECE_FINISHED;
                }
            }
        }
        else if (showAvailability ? pieces[index] == 0 : piecesPercent[index] == 0.0)
        {
            if (first || self.fPieces[index] != PIECE_NONE)
            {
                pieceColor = defaultColor;
                self.fPieces[index] = PIECE_NONE;
            }
        }
        else if (showAvailability && pieces[index] >= HIGH_PEERS)
        {
            if (first || self.fPieces[index] != PIECE_HIGH_PEERS)
            {
                pieceColor = NSColor.systemGreenColor;
                self.fPieces[index] = PIECE_HIGH_PEERS;
            }
        }
        else
        {
            //always redraw "mixed"
            CGFloat percent = showAvailability ? (CGFloat)pieces[index] / HIGH_PEERS : piecesPercent[index];
            NSColor* fullColor = showAvailability ? NSColor.systemGreenColor : NSColor.systemBlueColor;
            pieceColor = [defaultColor blendedColorWithFraction:percent ofColor:fullColor];
            self.fPieces[index] = PIECE_SOME;
        }

        if (pieceColor)
        {
            NSInteger const across = index % self.fAcross;
            NSInteger const down = index / self.fAcross;
            fillRects[usedCount] = NSMakeRect(
                across * (self.fWidth + BETWEEN) + BETWEEN + self.fExtraBorder,
                image.size.width - (down + 1) * (self.fWidth + BETWEEN) - self.fExtraBorder,
                self.fWidth,
                self.fWidth);
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
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    return YES;
}

- (void)mouseDown:(NSEvent*)event
{
    if (self.torrent)
    {
        BOOL const availability = ![NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];
        [NSUserDefaults.standardUserDefaults setBool:availability forKey:@"PiecesViewShowAvailability"];

        [self sendAction:self.action to:self.target];
    }

    [super mouseDown:event];
}

@end
