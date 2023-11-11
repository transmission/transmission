// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#pragma once

#include <bitset>
#include <cstddef> // size_t
#include <ctime> // for time_t
#include <optional>

#include "libtransmission/transmission.h" // for TR_SCHED_ALL

#include "libtransmission/quark.h"
#include "libtransmission/values.h"

struct tr_variant;

#define ALT_SPEEDS_FIELDS(V) \
    V(TR_KEY_alt_speed_up, speed_up_kbyps_, size_t, 50U, "") \
    V(TR_KEY_alt_speed_down, speed_down_kbyps_, size_t, 50U, "") \
    V(TR_KEY_alt_speed_time_enabled, scheduler_enabled_, bool, false, "whether alt speeds toggle on and off on schedule") \
    V(TR_KEY_alt_speed_time_day, use_on_these_weekdays_, size_t, TR_SCHED_ALL, "days of the week") \
    V(TR_KEY_alt_speed_time_begin, minute_begin_, size_t, 540U, "minutes past midnight; 9AM") \
    V(TR_KEY_alt_speed_time_end, minute_end_, size_t, 1020U, "minutes past midnight; 5PM")

/** Manages alternate speed limits and a scheduler to auto-toggle them. */
class tr_session_alt_speeds
{
    using Speed = libtransmission::Values::Speed;

public:
    enum class ChangeReason
    {
        User,
        Scheduler
    };

    class Mediator
    {
    public:
        virtual ~Mediator() noexcept = default;

        using ChangeReason = tr_session_alt_speeds::ChangeReason;
        virtual void is_active_changed(bool is_active, ChangeReason reason) = 0;

        [[nodiscard]] virtual time_t time() = 0;
    };

    constexpr explicit tr_session_alt_speeds(Mediator& mediator) noexcept
        : mediator_{ mediator }
    {
    }

    void load(tr_variant const& src);
    [[nodiscard]] tr_variant settings() const;
    [[nodiscard]] static tr_variant default_settings();

    [[nodiscard]] constexpr bool is_active() const noexcept
    {
        return is_active_;
    }

    void check_scheduler();

    void set_scheduler_enabled(bool enabled)
    {
        scheduler_enabled_ = enabled;
        update_scheduler();
    }

    // return true iff the scheduler will turn alt speeds on/off
    [[nodiscard]] constexpr auto is_scheduler_enabled() const noexcept
    {
        return scheduler_enabled_;
    }

    void set_start_minute(size_t minute)
    {
        minute_begin_ = minute;
        update_scheduler();
    }

    [[nodiscard]] constexpr auto start_minute() const noexcept
    {
        return minute_begin_;
    }

    void set_end_minute(size_t minute)
    {
        minute_end_ = minute;
        update_scheduler();
    }

    [[nodiscard]] constexpr auto end_minute() const noexcept
    {
        return minute_end_;
    }

    void set_weekdays(tr_sched_day days)
    {
        use_on_these_weekdays_ = days;
        update_scheduler();
    }

    [[nodiscard]] constexpr tr_sched_day weekdays() const noexcept
    {
        return static_cast<tr_sched_day>(use_on_these_weekdays_);
    }

    [[nodiscard]] auto speed_limit(tr_direction const dir) const noexcept
    {
        auto const kbyps = dir == TR_DOWN ? speed_down_kbyps_ : speed_up_kbyps_;
        return Speed{ kbyps, Speed::Units::KByps };
    }

    constexpr void set_speed_limit(tr_direction dir, Speed const limit) noexcept
    {
        if (dir == TR_DOWN)
        {
            speed_down_kbyps_ = limit.count(Speed::Units::KByps);
        }
        else
        {
            speed_up_kbyps_ = limit.count(Speed::Units::KByps);
        }
    }

    void set_active(bool active, ChangeReason reason);

private:
    Mediator& mediator_;

    void update_scheduler();
    void update_minutes();

    // whether `time` hits in one of the `minutes_` that is true
    [[nodiscard]] bool is_active_minute(time_t time) const noexcept;

    static auto constexpr MinutesPerHour = int{ 60 };
    static auto constexpr MinutesPerDay = int{ MinutesPerHour * 24 };
    static auto constexpr MinutesPerWeek = int{ MinutesPerDay * 7 };

    // are alt speeds active right now?
    bool is_active_ = false;

    // bitfield of all the minutes in a week.
    // Each bit's value indicates whether the scheduler wants
    // alt speeds on or off at that given minute.
    std::bitset<10080> minutes_{};

    // recent change that was made by the scheduler
    std::optional<bool> scheduler_set_is_active_to_;

#define V(key, name, type, default_value, comment) type name = type{ default_value };
    ALT_SPEEDS_FIELDS(V)
#undef V
};
