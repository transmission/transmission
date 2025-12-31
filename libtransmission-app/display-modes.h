// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

namespace transmission::app
{
enum class ShowMode
{
    ShowAll,
    ShowActive,
    ShowDownloading,
    ShowSeeding,
    ShowPaused,
    ShowFinished,
    ShowVerifying,
    ShowError,
};
inline auto constexpr ShowModeCount = 8U;
inline auto constexpr DefaultShowMode = ShowMode::ShowAll;

enum class SortMode
{
    SortByActivity,
    SortByAge,
    SortByEta,
    SortByName,
    SortByProgress,
    SortByQueue,
    SortByRatio,
    SortBySize,
    SortByState,
    SortById,
};
inline auto constexpr SortModeCount = 10U;
inline auto constexpr DefaultSortMode = SortMode::SortByName;

} // namespace transmission::app
