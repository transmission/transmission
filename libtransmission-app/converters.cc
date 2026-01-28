// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <mutex>
#include <string_view>
#include <utility>

#include "libtransmission/serializer.h"
#include "libtransmission/variant.h"

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/converters.h"

namespace tr::app::detail
{
namespace
{
template<typename T, size_t N>
using Lookup = std::array<std::pair<std::string_view, T>, N>;

// ---

auto constexpr ShowKeys = std::array<std::pair<std::string_view, ShowMode>, ShowModeCount>{ {
    { "show_active", ShowMode::ShowActive },
    { "show_all", ShowMode::ShowAll },
    { "show_downloading", ShowMode::ShowDownloading },
    { "show_error", ShowMode::ShowError },
    { "show_finished", ShowMode::ShowFinished },
    { "show_paused", ShowMode::ShowPaused },
    { "show_seeding", ShowMode::ShowSeeding },
    { "show_verifying", ShowMode::ShowVerifying },
} };

bool to_show_mode(tr_variant const& src, ShowMode* tgt)
{
    static constexpr auto& Keys = ShowKeys;

    if (auto const str = src.value_if<std::string_view>())
    {
        for (auto const& [key, val] : Keys)
        {
            if (str == key)
            {
                *tgt = val;
                return true;
            }
        }
    }

    return false;
}

tr_variant from_show_mode(ShowMode const& src)
{
    static constexpr auto& Keys = ShowKeys;

    for (auto const& [key, val] : Keys)
    {
        if (src == val)
        {
            return tr_variant::unmanaged_string(key);
        }
    }

    return from_show_mode(DefaultShowMode);
}

// ---

auto constexpr SortKeys = std::array<std::pair<std::string_view, SortMode>, SortModeCount>{ {
    { "sort_by_activity", SortMode::SortByActivity },
    { "sort_by_age", SortMode::SortByAge },
    { "sort_by_eta", SortMode::SortByEta },
    { "sort_by_id", SortMode::SortById },
    { "sort_by_name", SortMode::SortByName },
    { "sort_by_progress", SortMode::SortByProgress },
    { "sort_by_queue", SortMode::SortByQueue },
    { "sort_by_ratio", SortMode::SortByRatio },
    { "sort_by_size", SortMode::SortBySize },
    { "sort_by_state", SortMode::SortByState },
} };

bool to_sort_mode(tr_variant const& src, SortMode* tgt)
{
    static constexpr auto& Keys = SortKeys;

    if (auto const str = src.value_if<std::string_view>())
    {
        for (auto const& [key, val] : Keys)
        {
            if (str == key)
            {
                *tgt = val;
                return true;
            }
        }
    }

    return false;
}

tr_variant from_sort_mode(SortMode const& src)
{
    static constexpr auto& Keys = SortKeys;

    for (auto const& [key, val] : Keys)
    {
        if (src == val)
        {
            return tr_variant::unmanaged_string(key);
        }
    }

    return from_sort_mode(DefaultSortMode);
}
} // unnamed namespace

void register_app_converters()
{
    static auto once = std::once_flag{};
    std::call_once(
        once,
        []
        {
            using Converters = tr::serializer::Converters;
            Converters::add(to_show_mode, from_show_mode);
            Converters::add(to_sort_mode, from_sort_mode);
        });
}

} // namespace tr::app::detail
