// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission-app/display-modes.h>

#include "Torrent.h"

// The Torrent properties that can affect ShowMode filtering.
// When one of these changes, it's time to refilter.
extern Torrent::fields_t const ShowModeFields;

using ShowMode = transmission::app::ShowMode;
Q_DECLARE_METATYPE(ShowMode)
inline auto constexpr DefaultShowMode = transmission::app::DefaultShowMode;
inline auto constexpr ShowModeCount = transmission::app::ShowModeCount;
bool should_show_torrent(Torrent const& torrent, ShowMode);

using SortMode = transmission::app::SortMode;
Q_DECLARE_METATYPE(SortMode)
inline auto constexpr DefaultSortMode = transmission::app::DefaultSortMode;
