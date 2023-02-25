// This file Copyright 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstddef> // for size_t
#include <cstdint> // for uint32_t
#include <ctime> // for time_t

/**
 * A short-term memory object that remembers how many times something
 * happened over the last Seconds seconds. `tr_peer` uses it to count
 * how many bytes transferred to estimate the speed over the last
 * Seconds seconds.
 */
template<typename SizeType, std::size_t Seconds = 60>
class tr_recentHistory
{
public:
    /**
     * @brief add a counter to the recent history object.
     * @param when the current time in sec, such as from tr_time()
     * @param n how many items to add to the history's counter
     */
    constexpr void add(time_t now, SizeType n)
    {
        if (timestamps_[newest_] != now)
        {
            newest_ = (newest_ + 1) % Seconds;
            timestamps_[newest_] = now;
            count_[newest_] = {};
        }

        count_[newest_] += n;
    }

    /**
     * @brief count how many events have occurred in the last N seconds.
     * @param when the current time in sec, such as from tr_time()
     * @param seconds how many seconds to count back through.
     */
    [[nodiscard]] constexpr SizeType count(time_t now, unsigned int age_sec) const
    {
        auto sum = SizeType{};
        time_t const oldest = now - age_sec;

        for (std::size_t i = 0; i < Seconds; ++i)
        {
            if (timestamps_[i] >= oldest)
            {
                sum += count_[i];
            }
        }

        return sum;
    }

private:
    std::array<time_t, Seconds> timestamps_ = {};
    std::array<SizeType, Seconds> count_ = {};
    uint32_t newest_ = 0;
};
