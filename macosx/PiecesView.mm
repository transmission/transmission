// This file Copyright © 2006-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>

#include <libtransmission/transmission.h>

#import "PiecesView.h"
#import "Torrent.h"
#import "InfoWindowController.h"
#import "NSApplicationAdditions.h"

namespace
{
constexpr auto kMaxAcross = NSInteger{ 18 };
constexpr auto kMaxCells = kMaxAcross * kMaxAcross;

constexpr auto kBetweenPadding = CGFloat{ 1.0 };

using PieceInfo = union
{
    std::array<int8_t, kMaxCells> available;
    std::array<float, kMaxCells> complete;
};

auto* const DoneColor = NSColor.systemBlueColor;
auto* const BlinkColor = NSColor.systemOrangeColor;
auto* const HighColor = NSColor.systemGreenColor; // high availability

[[nodiscard]] NSColor* backgroundColor()
{
    return NSApp.darkMode ? NSColor.blackColor : NSColor.whiteColor;
}

[[nodiscard]] NSColor* availabilityColor(int8_t old_val, int8_t new_val, bool no_blink)
{
    constexpr auto kHighPeers = int8_t{ 10 };

    constexpr auto test_done = [](auto val)
    {
        return val == -1;
    };
    constexpr auto test_none = [](auto val)
    {
        return val == 0;
    };
    constexpr auto test_high = [](auto val)
    {
        return val >= kHighPeers;
    };

    if (test_done(new_val))
    {
        return no_blink || test_done(old_val) ? DoneColor : BlinkColor;
    }

    if (test_none(new_val))
    {
        return no_blink || test_none(old_val) ? backgroundColor() : BlinkColor;
    }

    if (test_high(new_val))
    {
        return no_blink || test_high(old_val) ? HighColor : BlinkColor;
    }

    auto percent = static_cast<CGFloat>(new_val) / kHighPeers;
    return [backgroundColor() blendedColorWithFraction:percent ofColor:HighColor];
}

[[nodiscard]] NSColor* completenessColor(float old_val, float new_val, bool no_blink)
{
    constexpr auto test_done = [](auto val)
    {
        return val >= 1.0F;
    };
    constexpr auto test_none = [](auto val)
    {
        return val <= 0.0F;
    };

    if (test_done(new_val))
    {
        return no_blink || test_done(old_val) ? DoneColor : BlinkColor;
    }

    if (test_none(new_val))
    {
        return no_blink || test_none(old_val) ? backgroundColor() : BlinkColor;
    }

    return [backgroundColor() blendedColorWithFraction:new_val ofColor:DoneColor];
}

} // namespace

@interface PiecesView ()
@end

@implementation PiecesView
{
    PieceInfo fPieceInfo;
    NSString* fRenderedHashString;
}

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
    self.image = [[NSImage alloc] initWithSize:self.bounds.size];

    [self setNeedsDisplay];
}

- (void)clearView
{
    fRenderedHashString = nil;
    fPieceInfo = {};
}

- (void)updateView
{
    if (!self.torrent)
    {
        return;
    }

    auto const n_cells = std::min(_torrent.pieceCount, kMaxCells);
    auto const full_width = self.bounds.size.width;
    auto const across = static_cast<NSInteger>(ceil(sqrt(n_cells)));
    auto const cell_width = static_cast<NSInteger>((full_width - (across + 1) * kBetweenPadding) / across);
    auto const extra_border = static_cast<NSInteger>((full_width - ((cell_width + kBetweenPadding) * across + kBetweenPadding)) / 2);

    // get the previous state
    auto const old_info = fPieceInfo;
    auto const first = fRenderedHashString != self.torrent.hashString;

    // get the info that we're going to render
    auto info = PieceInfo{};
    auto const show_availability = [NSUserDefaults.standardUserDefaults boolForKey:@"PiecesViewShowAvailability"];
    if (show_availability)
    {
        [self.torrent getAvailability:std::data(info.available) size:n_cells];
    }
    else
    {
        [self.torrent getAmountFinished:std::data(info.complete) size:n_cells];
    }

    // get the bounds and color for each cell
    auto cell_colors = std::array<NSColor*, kMaxCells>{};
    auto cell_rects = std::array<NSRect, kMaxCells>{};
    for (NSInteger index = 0; index < n_cells; ++index)
    {
        auto const row = index / across;
        auto const col = index % across;

        cell_rects[index] = NSMakeRect(
            col * (cell_width + kBetweenPadding) + kBetweenPadding + extra_border,
            full_width - (row + 1) * (cell_width + kBetweenPadding) - extra_border,
            cell_width,
            cell_width);
        cell_colors[index] = show_availability ? availabilityColor(old_info.available[index], info.available[index], first) :
                                                 completenessColor(old_info.complete[index], info.complete[index], first);
    }

    // draw it
    if (n_cells > 0)
    {
        self.image = [NSImage imageWithSize:self.bounds.size flipped:NO drawingHandler:^BOOL(NSRect /*dstRect*/) {
            NSRectFillListWithColors(std::data(cell_rects), std::data(cell_colors), n_cells);
            return YES;
        }];
        [self setNeedsDisplay];
    }

    // save the current state so we can compare it later
    fPieceInfo = info;
    fRenderedHashString = self.torrent.hashString;
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
