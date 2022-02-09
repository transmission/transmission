// This file Copyright 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstddef> // size_t
#include <ctime> // time_t
#include <numeric> // std::accumulate

/**
 * A short-term memory object that remembers how many times something
 * happened over the last N seconds. tr_peer uses it to count how many
 * bytes transferred to estimate the speed over the last N seconds.
 */
class tr_recentHistory
{
public:
    /**
     * @brief add a counter to the recent history object.
     * @param when the current time in sec, such as from tr_time()
     * @param n how many items to add to the history's counter
     */
    void add(time_t now, size_t n)
    {
        if (slices[newest].time != now)
        {
            newest = (newest + 1) % TR_RECENT_HISTORY_PERIOD_SEC;
            slices[newest].time = now;
        }

        slices[newest].n += n;
    }

    /**
     * @brief count how many events have occurred in the last N seconds.
     * @param when the current time in sec, such as from tr_time()
     * @param seconds how many seconds to count back through.
     */
    size_t count(time_t now, unsigned int age_sec) const
    {
        time_t const oldest = now - age_sec;

        return std::accumulate(
            std::begin(slices),
            std::end(slices),
            size_t{ 0 },
            [&oldest](size_t sum, auto const& slice) { return slice.time >= oldest ? sum + slice.n : sum; });
    }

private:
    inline auto static constexpr TR_RECENT_HISTORY_PERIOD_SEC = size_t{ 60 };

    int newest = 0;

    struct slice_t
    {
        size_t n = 0;
        time_t time = 0;
    };

    std::array<slice_t, TR_RECENT_HISTORY_PERIOD_SEC> slices = {};
};
