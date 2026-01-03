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

namespace transmission::app::detail
{
namespace
{
template<typename T, size_t N>
using Lookup = std::array<std::pair<std::string_view, T>, N>;

// ---

auto constexpr ShowKeys = std::array<std::pair<tr_quark, ShowMode>, ShowModeCount>{ {
    { TR_KEY_show_active, ShowMode::ShowActive },
    { TR_KEY_show_all, ShowMode::ShowAll },
    { TR_KEY_show_downloading, ShowMode::ShowDownloading },
    { TR_KEY_show_error, ShowMode::ShowError },
    { TR_KEY_show_finished, ShowMode::ShowFinished },
    { TR_KEY_show_paused, ShowMode::ShowPaused },
    { TR_KEY_show_seeding, ShowMode::ShowSeeding },
    { TR_KEY_show_verifying, ShowMode::ShowVerifying },
} };

bool to_show_mode(tr_variant const& src, ShowMode* tgt)
{
    static constexpr auto& Keys = ShowKeys;

    if (auto const str = src.value_if<std::string_view>())
    {
        if (auto const needle = tr_quark_lookup(*str))
        {
            for (auto const& [key, val] : Keys)
            {
                if (needle == key)
                {
                    *tgt = val;
                    return true;
                }
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

    return tr_variant::unmanaged_string(TR_KEY_show_all);
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
            using Converters = libtransmission::serializer::Converters;
            Converters::add(to_show_mode, from_show_mode);
            Converters::add(to_sort_mode, from_sort_mode);
        });
}

} // namespace transmission::app::detail
