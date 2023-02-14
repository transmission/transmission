// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#pragma once

#include <bitset>
#include <ctime> // for time_t
#include <optional>

#include "transmission.h" // for TR_SCHED_ALL

#include "quark.h"

struct tr_variant;

#define ALT_SPEEDS_FIELDS(V) \
    V(TR_KEY_alt_speed_up, speed_up_kilobytes_per_second_, size_t, 50U, "") \
    V(TR_KEY_alt_speed_down, speed_down_kilobytes_per_second_, size_t, 50U, "") \
    V(TR_KEY_alt_speed_time_enabled, scheduler_enabled_, bool, false, "whether alt speeds toggle on and off on schedule") \
    V(TR_KEY_alt_speed_time_day, use_on_these_weekdays_, size_t, TR_SCHED_ALL, "days of the week") \
    V(TR_KEY_alt_speed_time_begin, minute_begin_, size_t, 540U, "minutes past midnight; 9AM") \
    V(TR_KEY_alt_speed_time_end, minute_end_, size_t, 1020U, "minutes past midnight; 5PM")

/** Manages alternate speed limits and a scheduler to auto-toggle them. */
class tr_session_alt_speeds
{
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
        virtual void isActiveChanged(bool is_active, ChangeReason reason) = 0;

        [[nodiscard]] virtual time_t time() = 0;
    };

    constexpr explicit tr_session_alt_speeds(Mediator& mediator) noexcept
        : mediator_{ mediator }
    {
    }

    void load(tr_variant* src);
    void save(tr_variant* tgt) const;
    static void defaultSettings(tr_variant* tgt);

    [[nodiscard]] constexpr bool isActive() const noexcept
    {
        return is_active_;
    }

    void checkScheduler();

    void setSchedulerEnabled(bool enabled)
    {
        scheduler_enabled_ = enabled;
        updateScheduler();
    }

    // return true iff the scheduler will turn alt speeds on/off
    [[nodiscard]] constexpr auto isSchedulerEnabled() const noexcept
    {
        return scheduler_enabled_;
    }

    void setStartMinute(size_t minute)
    {
        minute_begin_ = minute;
        updateScheduler();
    }

    [[nodiscard]] constexpr auto startMinute() const noexcept
    {
        return minute_begin_;
    }

    void setEndMinute(size_t minute)
    {
        minute_end_ = minute;
        updateScheduler();
    }

    [[nodiscard]] constexpr auto endMinute() const noexcept
    {
        return minute_end_;
    }

    void setWeekdays(tr_sched_day days)
    {
        use_on_these_weekdays_ = days;
        updateScheduler();
    }

    [[nodiscard]] constexpr tr_sched_day weekdays() const noexcept
    {
        return static_cast<tr_sched_day>(use_on_these_weekdays_);
    }

    [[nodiscard]] constexpr auto limitKBps(tr_direction dir) const noexcept
    {
        return dir == TR_DOWN ? speed_down_kilobytes_per_second_ : speed_up_kilobytes_per_second_;
    }

    constexpr void setLimitKBps(tr_direction dir, size_t limit) noexcept
    {
        if (dir == TR_DOWN)
        {
            speed_down_kilobytes_per_second_ = limit;
        }
        else
        {
            speed_up_kilobytes_per_second_ = limit;
        }
    }

    void setActive(bool active, ChangeReason reason);

private:
    Mediator& mediator_;

    void updateScheduler();
    void updateMinutes();

    // whether `time` hits in one of the `minutes_` that is true
    [[nodiscard]] bool isActiveMinute(time_t time) const noexcept;

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
