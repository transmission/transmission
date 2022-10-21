// This file Copyright © 2006-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <optional>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#import "PiecesView.h"
#import "Torrent.h"
#import "InfoWindowController.h"
#import "NSApplicationAdditions.h"

namespace
{
constexpr auto kMaxAcross = NSInteger{ 18 };
constexpr auto kMaxCells = kMaxAcross * kMaxAcross;

constexpr auto kBetweenPadding = CGFloat{ 1.0 };

constexpr auto kHighPeers = int8_t{ 10 };

enum class PieceMode
{
    None,
    Some,
    HighPeers,
    Finished,
    Flashing
};
} // namespace

@interface PiecesView ()

@property(nonatomic) std::optional<NSInteger> fNumPieces;

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
    _fNumPieces.reset();
    self.image = [[NSImage alloc] initWithSize:self.bounds.size];

    [self setNeedsDisplay];
}

- (void)clearView
{
    self.fNumPieces.reset();
}

- (void)updateView
{
    if (!self.torrent)
    {
        return;
    }

    auto const n_pieces = std::min(_torrent.pieceCount, kMaxCells);
    auto const full_width = self.bounds.size.width;
    auto const across = static_cast<NSInteger>(ceil(sqrt(n_pieces)));
    auto const cell_width = static_cast<NSInteger>((full_width - (across + 1) * kBetweenPadding) / across);
    auto const extra_border = static_cast<NSInteger>((full_width - ((cell_width + kBetweenPadding) * across + kBetweenPadding)) / 2);

    // determine if first time
    auto const first = !_fNumPieces.has_value();
    _fNumPieces = n_pieces;

    // get the data that we're going to render
    auto pieces = std::array<int8_t, kMaxCells>{};
    auto pieces_percent = std::array<float, kMaxCells>{};
    auto const show_availability = [NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];
    if (show_availability)
    {
        [self.torrent getAvailability:std::data(pieces) size:n_pieces];
    }
    else
    {
        [self.torrent getAmountFinished:std::data(pieces_percent) size:n_pieces];
    }

    // get the rect, color info for each cell
    auto fill_colors = std::array<NSColor*, kMaxCells>{};
    auto fill_rects = std::array<NSRect, kMaxCells>{};
    auto piece_modes = std::array<PieceMode, kMaxCells>{};
    auto* const default_color = NSApp.darkMode ? NSColor.blackColor : NSColor.whiteColor;
    auto used_count = NSInteger{};
    for (NSInteger index = 0; index < n_pieces; ++index)
    {
        NSColor* pieceColor = nil;

        if (show_availability ? pieces[index] == -1 : pieces_percent[index] == 1.0)
        {
            if (first || piece_modes[index] != PieceMode::Finished)
            {
                if (!first && piece_modes[index] != PieceMode::Flashing)
                {
                    pieceColor = NSColor.systemOrangeColor;
                    piece_modes[index] = PieceMode::Flashing;
                }
                else
                {
                    pieceColor = NSColor.systemBlueColor;
                    piece_modes[index] = PieceMode::Finished;
                }
            }
        }
        else if (show_availability ? pieces[index] == 0 : pieces_percent[index] == 0.0)
        {
            if (first || piece_modes[index] != PieceMode::None)
            {
                pieceColor = default_color;
                piece_modes[index] = PieceMode::None;
            }
        }
        else if (show_availability && pieces[index] >= kHighPeers)
        {
            if (first || piece_modes[index] != PieceMode::HighPeers)
            {
                pieceColor = NSColor.systemGreenColor;
                piece_modes[index] = PieceMode::HighPeers;
            }
        }
        else
        {
            //always redraw "mixed"
            CGFloat percent = show_availability ? (CGFloat)pieces[index] / kHighPeers : pieces_percent[index];
            NSColor* fullColor = show_availability ? NSColor.systemGreenColor : NSColor.systemBlueColor;
            pieceColor = [default_color blendedColorWithFraction:percent ofColor:fullColor];
            piece_modes[index] = PieceMode::Some;
        }

        if (pieceColor)
        {
            auto const row = index / across;
            auto const col = index % across;
            fill_rects[used_count] = NSMakeRect(
                col * (cell_width + kBetweenPadding) + kBetweenPadding + extra_border,
                full_width - (row + 1) * (cell_width + kBetweenPadding) - extra_border,
                cell_width,
                cell_width);
            fill_colors[used_count] = pieceColor;

            ++used_count;
        }
    }

    if (used_count > 0)
    {
        self.image = [NSImage imageWithSize:self.bounds.size flipped:NO drawingHandler:^BOOL(NSRect /*dstRect*/) {
            NSRectFillListWithColors(std::data(fill_rects), std::data(fill_colors), used_count);
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
