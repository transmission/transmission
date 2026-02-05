// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string_view>

#include <gtest/gtest.h>

#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include "libtransmission-app/display-modes.h"

#include "test-fixtures.h"

using DisplayModeTest = TransmissionTest;
using namespace std::literals;
using tr::serializer::Converters;

namespace
{

template<typename T, size_t N>
void testModeRoundtrip(std::array<std::pair<std::string_view, T>, N> const& items)
{
    for (auto const& [key, mode] : items)
    {
        auto const var = Converters::serialize(mode);
        EXPECT_TRUE(var.template holds_alternative<std::string_view>());
        EXPECT_EQ(var.template value_if<std::string_view>().value_or(""sv), key);

        auto out = T{};
        EXPECT_TRUE(Converters::deserialize(tr_variant{ key }, &out));
        EXPECT_EQ(out, mode);
    }
}

} // namespace

TEST_F(DisplayModeTest, showModeStringsRoundtrip)
{
    auto constexpr Items = std::array<std::pair<std::string_view, tr::app::ShowMode>, tr::app::ShowModeCount>{ {
        { "show_active", tr::app::ShowMode::ShowActive },
        { "show_all", tr::app::ShowMode::ShowAll },
        { "show_downloading", tr::app::ShowMode::ShowDownloading },
        { "show_error", tr::app::ShowMode::ShowError },
        { "show_finished", tr::app::ShowMode::ShowFinished },
        { "show_paused", tr::app::ShowMode::ShowPaused },
        { "show_seeding", tr::app::ShowMode::ShowSeeding },
        { "show_verifying", tr::app::ShowMode::ShowVerifying },
    } };

    testModeRoundtrip(Items);
}

TEST_F(DisplayModeTest, sortModeStringsRoundtrip)
{
    auto constexpr Items = std::array<std::pair<std::string_view, tr::app::SortMode>, tr::app::SortModeCount>{ {
        { "sort_by_activity", tr::app::SortMode::SortByActivity },
        { "sort_by_age", tr::app::SortMode::SortByAge },
        { "sort_by_eta", tr::app::SortMode::SortByEta },
        { "sort_by_id", tr::app::SortMode::SortById },
        { "sort_by_name", tr::app::SortMode::SortByName },
        { "sort_by_progress", tr::app::SortMode::SortByProgress },
        { "sort_by_queue", tr::app::SortMode::SortByQueue },
        { "sort_by_ratio", tr::app::SortMode::SortByRatio },
        { "sort_by_size", tr::app::SortMode::SortBySize },
        { "sort_by_state", tr::app::SortMode::SortByState },
    } };

    testModeRoundtrip(Items);
}

TEST_F(DisplayModeTest, statsModeStringsRoundtrip)
{
    auto constexpr Items = std::array<std::pair<std::string_view, tr::app::StatsMode>, tr::app::StatsModeCount>{ {
        { "total_ratio", tr::app::StatsMode::TotalRatio },
        { "total_transfer", tr::app::StatsMode::TotalTransfer },
        { "session_ratio", tr::app::StatsMode::SessionRatio },
        { "session_transfer", tr::app::StatsMode::SessionTransfer },
    } };

    testModeRoundtrip(Items);
}
