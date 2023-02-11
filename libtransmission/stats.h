// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

#include "transmission.h" // for tr_session_stats

// per-session data structure for bandwidth use statistics
class tr_stats
{
public:
    tr_stats(std::string_view config_dir, time_t now)
        : config_dir_{ config_dir }
        , start_time_{ now }
    {
        single_.sessionCount = 1;
        old_ = loadOldStats(config_dir_);
    }

    ~tr_stats()
    {
        saveIfDirty();
    }

    void clear();

    [[nodiscard]] tr_session_stats current() const;

    [[nodiscard]] auto cumulative() const
    {
        return add(current(), old_);
    }

    constexpr void addUploaded(uint32_t n_bytes) noexcept
    {
        single_.uploadedBytes += n_bytes;
        is_dirty_ = true;
    }

    constexpr void addDownloaded(uint32_t n_bytes) noexcept
    {
        single_.downloadedBytes += n_bytes;
        is_dirty_ = true;
    }

    constexpr void addFileCreated() noexcept
    {
        ++single_.filesAdded;
        is_dirty_ = true;
    }

    void saveIfDirty()
    {
        if (is_dirty_)
        {
            save();
            is_dirty_ = false;
        }
    }

private:
    static tr_session_stats add(tr_session_stats const& a, tr_session_stats const& b);

    void save() const;

    static tr_session_stats loadOldStats(std::string_view config_dir);

    std::string const config_dir_;
    time_t start_time_;

    static constexpr auto Zero = tr_session_stats{ TR_RATIO_NA, 0U, 0U, 0U, 0U, 0U };
    tr_session_stats single_ = Zero;
    tr_session_stats old_ = Zero;

    bool is_dirty_ = false;
};
