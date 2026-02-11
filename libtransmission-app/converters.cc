// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <ctime>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include "libtransmission/serializer.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/converters.h"

using namespace std::literals;

namespace tr::app::detail
{
namespace
{
template<typename T>
inline constexpr bool HasTmGmtoffV = requires(T t) { t.tm_gmtoff; };

template<typename T, size_t N>
using Lookup = std::array<std::pair<std::string_view, T>, N>;

// ---

struct TrYearMonthDay
{
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    bool valid = false;

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return valid;
    }
};

// c++20 (P0355) replace with std::chrono::year_month_day() after Debian 11 is EOL
[[nodiscard]] constexpr TrYearMonthDay tr_year_month_day(int year, unsigned month, unsigned day)
{
    auto const is_leap_year = [](int y) constexpr
    {
        return (y % 4 == 0) && ((y % 100) != 0 || (y % 400) == 0);
    };

    auto const days_in_month = [&](int y, unsigned m) constexpr
    {
        switch (m)
        {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            return 31;
        case 4:
        case 6:
        case 9:
        case 11:
            return 30;
        case 2:
            return is_leap_year(y) ? 29 : 28;
        default:
            return 0;
        }
    };

    auto const is_valid_ymd = [&](int y, unsigned m, unsigned d) constexpr
    {
        if (m < 1 || m > 12)
        {
            return false;
        }

        auto const dim = days_in_month(y, m);
        return d >= 1 && std::cmp_less_equal(d, dim);
    };

    return TrYearMonthDay{ .year = year, .month = month, .day = day, .valid = is_valid_ymd(year, month, day) };
}

// c++20 (P0355) replace with std::chrono::sys_days{} after Debian 11 is EOL
// Returns days since 1970-01-01. Based on Howard Hinnant's civil calendar algorithms.
[[nodiscard]] constexpr std::chrono::sys_days tr_sys_days(TrYearMonthDay const& ymd)
{
    auto const days_from_civil = [](int year, unsigned month, unsigned day) constexpr
    {
        year -= static_cast<int>(month <= 2);
        auto const era = (year >= 0 ? year : year - 399) / 400;
        auto const yoe = static_cast<unsigned>(year - (era * 400));
        auto const doy = ((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5) + day - 1;
        auto const doe = (yoe * 365) + (yoe / 4) - (yoe / 100) + doy;
        return (static_cast<int64_t>(era) * 146097) + static_cast<int64_t>(doe) - 719468;
    };

    return std::chrono::sys_days{ std::chrono::days{ days_from_civil(ymd.year, ymd.month, ymd.day) } };
}

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

// ---

auto constexpr StatsKeys = std::array<std::pair<std::string_view, StatsMode>, StatsModeCount>{ {
    { "session_ratio", StatsMode::SessionRatio },
    { "session_transfer", StatsMode::SessionTransfer },
    { "total_ratio", StatsMode::TotalRatio },
    { "total_transfer", StatsMode::TotalTransfer },
} };

bool to_stats_mode(tr_variant const& src, StatsMode* tgt)
{
    static constexpr auto& Keys = StatsKeys;

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

tr_variant from_stats_mode(StatsMode const& src)
{
    static constexpr auto& Keys = StatsKeys;

    for (auto const& [key, val] : Keys)
    {
        if (src == val)
        {
            return tr_variant::unmanaged_string(key);
        }
    }

    return from_stats_mode(DefaultStatsMode);
}

// ---

// c++20(P0355): use std::chrono::parse if/when it's ever available
[[nodiscard]] std::optional<std::chrono::sys_seconds> parse_sys_seconds(std::string_view str)
{
    auto const sv = tr_strv_strip(str);
    if ((std::size(sv) != 20U && std::size(sv) != 24U && std::size(sv) != 25U) || sv[4] != '-' || sv[7] != '-' ||
        sv[10] != 'T' || sv[13] != ':' || sv[16] != ':')
    {
        return {};
    }

    auto parse_int = [](std::string_view token, int min, int max, int* out) -> bool
    {
        if (auto const parsed = tr_num_parse<int>(token); parsed && *parsed >= min && *parsed <= max)
        {
            *out = *parsed;
            return true;
        }

        return false;
    };

    auto year = int{};
    auto month = int{};
    auto day = int{};
    auto hour = int{};
    auto minute = int{};
    auto second = int{};

    if (!parse_int(sv.substr(0, 4), 0, 9999, &year) || !parse_int(sv.substr(5, 2), 1, 12, &month) ||
        !parse_int(sv.substr(8, 2), 1, 31, &day) || !parse_int(sv.substr(11, 2), 0, 23, &hour) ||
        !parse_int(sv.substr(14, 2), 0, 59, &minute) || !parse_int(sv.substr(17, 2), 0, 59, &second))
    {
        return {};
    }

    auto const ymd = tr_year_month_day(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    if (!ymd.ok())
    {
        return {};
    }

    auto const day_point = std::chrono::time_point_cast<std::chrono::seconds>(tr_sys_days(ymd));
    auto const local_tp = day_point + std::chrono::hours{ hour } + std::chrono::minutes{ minute } +
        std::chrono::seconds{ second };

    if (std::size(sv) == 20U)
    {
        if (sv[19] != 'Z')
        {
            return {};
        }

        return std::chrono::sys_seconds{ local_tp };
    }

    auto const sign = sv[19];
    if (sign != '+' && sign != '-')
    {
        return {};
    }

    auto off_hours = int{};
    auto off_minutes = int{};

    if (std::size(sv) == 24U)
    {
        if (!parse_int(sv.substr(20, 2), 0, 23, &off_hours) || !parse_int(sv.substr(22, 2), 0, 59, &off_minutes))
        {
            return {};
        }
    }
    else
    {
        if (sv[22] != ':' || !parse_int(sv.substr(20, 2), 0, 23, &off_hours) ||
            !parse_int(sv.substr(23, 2), 0, 59, &off_minutes))
        {
            return {};
        }
    }

    auto const offset = std::chrono::minutes{ (off_hours * 60) + off_minutes } * (sign == '-' ? -1 : 1);
    return std::chrono::sys_seconds{ local_tp - offset };
}

[[nodiscard]] std::string format_sys_seconds(std::chrono::sys_seconds const& src)
{
    auto const tp = std::chrono::time_point_cast<std::chrono::seconds>(src);
    auto const tt = std::chrono::system_clock::to_time_t(tp);

    // prefer localtime with TZ offset data when we can get it.
    if constexpr (HasTmGmtoffV<std::tm>)
    {
        if (auto const* local = std::localtime(&tt))
        {
            // fmt::runtime to workaround FTBFS in clang
            return fmt::format(fmt::runtime("{:%FT%T%z}"), *local);
        }
    }

    return fmt::format("{:%FT%TZ}", src);
}

bool to_sys_seconds(tr_variant const& src, std::chrono::sys_seconds* tgt)
{
    if (auto const val = src.value_if<std::string_view>())
    {
        if (auto const parsed = parse_sys_seconds(*val); parsed)
        {
            *tgt = *parsed;
            return true;
        }
    }

    if (auto const val = src.value_if<int64_t>())
    {
        auto const tp = std::chrono::system_clock::from_time_t(static_cast<time_t>(*val));
        *tgt = std::chrono::time_point_cast<std::chrono::seconds>(tp);
        return true;
    }

    return false;
}

tr_variant from_sys_seconds(std::chrono::sys_seconds const& src)
{
    auto const formatted = format_sys_seconds(src);
    return tr_variant{ formatted };
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
            Converters::add(to_stats_mode, from_stats_mode);
            Converters::add(to_sys_seconds, from_sys_seconds);
        });
}

} // namespace tr::app::detail
