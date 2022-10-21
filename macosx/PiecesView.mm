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

static NSInteger const kMaxAcross = 18;
static CGFloat const kBetweenPadding = 1.0;

static int8_t const kHighPeers = 10;

enum class PieceMode
{
    None,
    Some,
    HighPeers,
    Finished,
    Flashing
};

@interface PiecesView ()

@property(nonatomic) std::vector<PieceMode> fPieces;

@property(nonatomic) NSInteger fNumPieces;

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
        _fNumPieces = MIN(_torrent.pieceCount, kMaxAcross * kMaxAcross);
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

    auto const n_pieces = self.fNumPieces;
    auto const full_width = self.bounds.size.width;
    auto const across = static_cast<NSInteger>(ceil(sqrt(_fNumPieces)));
    auto const cell_width = static_cast<NSInteger>((full_width - (across + 1) * kBetweenPadding) / across);
    auto const extra_border = static_cast<NSInteger>((full_width - ((cell_width + kBetweenPadding) * across + kBetweenPadding)) / 2);

    //determine if first time
    BOOL const first = std::empty(self.fPieces);
    if (first)
    {
        _fPieces.resize(n_pieces);
    }

    auto pieces = std::vector<int8_t>{};
    auto piecesPercent = std::vector<float>{};

    BOOL const showAvailability = [NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];
    if (showAvailability)
    {
        pieces.resize(n_pieces);
        [self.torrent getAvailability:std::data(pieces) size:std::size(pieces)];
    }
    else
    {
        piecesPercent.resize(n_pieces);
        [self.torrent getAmountFinished:std::data(piecesPercent) size:std::size(piecesPercent)];
    }

    NSMutableArray<NSValue*>* fillRects = [NSMutableArray arrayWithCapacity:n_pieces];
    NSMutableArray<NSColor*>* fillColors = [NSMutableArray arrayWithCapacity:n_pieces];

    NSColor* defaultColor = NSApp.darkMode ? NSColor.blackColor : NSColor.whiteColor;

    NSInteger usedCount = 0;

    for (NSInteger index = 0; index < n_pieces; index++)
    {
        NSColor* pieceColor = nil;

        if (showAvailability ? pieces[index] == -1 : piecesPercent[index] == 1.0)
        {
            if (first || self.fPieces[index] != PieceMode::Finished)
            {
                if (!first && self.fPieces[index] != PieceMode::Flashing)
                {
                    pieceColor = NSColor.systemOrangeColor;
                    self.fPieces[index] = PieceMode::Flashing;
                }
                else
                {
                    pieceColor = NSColor.systemBlueColor;
                    self.fPieces[index] = PieceMode::Finished;
                }
            }
        }
        else if (showAvailability ? pieces[index] == 0 : piecesPercent[index] == 0.0)
        {
            if (first || self.fPieces[index] != PieceMode::None)
            {
                pieceColor = defaultColor;
                self.fPieces[index] = PieceMode::None;
            }
        }
        else if (showAvailability && pieces[index] >= kHighPeers)
        {
            if (first || self.fPieces[index] != PieceMode::HighPeers)
            {
                pieceColor = NSColor.systemGreenColor;
                self.fPieces[index] = PieceMode::HighPeers;
            }
        }
        else
        {
            //always redraw "mixed"
            CGFloat percent = showAvailability ? (CGFloat)pieces[index] / kHighPeers : piecesPercent[index];
            NSColor* fullColor = showAvailability ? NSColor.systemGreenColor : NSColor.systemBlueColor;
            pieceColor = [defaultColor blendedColorWithFraction:percent ofColor:fullColor];
            self.fPieces[index] = PieceMode::Some;
        }

        if (pieceColor)
        {
            auto const row = index / across;
            auto const col = index % across;
            fillRects[usedCount] = [NSValue valueWithRect:NSMakeRect(
                                                              col * (cell_width + kBetweenPadding) + kBetweenPadding + extra_border,
                                                              full_width - (row + 1) * (cell_width + kBetweenPadding) - extra_border,
                                                              cell_width,
                                                              cell_width)];
            fillColors[usedCount] = pieceColor;

            usedCount++;
        }
    }

    if (usedCount > 0)
    {
        self.image = [NSImage imageWithSize:self.bounds.size flipped:NO drawingHandler:^BOOL(NSRect /*dstRect*/) {
            NSRect cFillRects[usedCount];
            for (NSInteger i = 0; i < usedCount; ++i)
            {
                cFillRects[i] = fillRects[i].rectValue;
            }
            NSColor* cFillColors[usedCount];
            for (NSInteger i = 0; i < usedCount; ++i)
            {
                cFillColors[i] = fillColors[i];
            }
            NSRectFillListWithColors(cFillRects, cFillColors, usedCount);
            return YES;
        }];
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
