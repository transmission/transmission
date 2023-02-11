// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fmt/chrono.h>

#include "transmission.h"

#include "log.h"
#include "session-alt-speeds.h"
#include "variant.h"
#include "utils.h" // for _()

using namespace std::literals;

void tr_session_alt_speeds::load(tr_variant* src)
{
#define V(key, field, type, default_value, comment) \
    if (auto* const child = tr_variantDictFind(src, key); child != nullptr) \
    { \
        if (auto val = libtransmission::VariantConverter::load<decltype(field)>(child); val) \
        { \
            this->field = *val; \
        } \
    }
    ALT_SPEEDS_FIELDS(V)
#undef V

    updateScheduler();
}

void tr_session_alt_speeds::save(tr_variant* tgt) const
{
#define V(key, field, type, default_value, comment) \
    tr_variantDictRemove(tgt, key); \
    libtransmission::VariantConverter::save<decltype(field)>(tr_variantDictAdd(tgt, key), field);
    ALT_SPEEDS_FIELDS(V)
#undef V
}

void tr_session_alt_speeds::defaultSettings(tr_variant* tgt)
{
#define V(key, field, type, default_value, comment) \
    { \
        type const val = default_value; \
        tr_variantDictRemove(tgt, key); \
        libtransmission::VariantConverter::save<decltype(field)>(tr_variantDictAdd(tgt, key), val); \
    }
    ALT_SPEEDS_FIELDS(V)
#undef V
}

// --- minutes

void tr_session_alt_speeds::updateMinutes()
{
    minutes_.reset();

    for (int day = 0; day < 7; ++day)
    {
        if ((static_cast<tr_sched_day>(use_on_these_weekdays_) & (1 << day)) != 0)
        {
            auto const begin = minute_begin_;
            auto const end = minute_end_ > minute_begin_ ? minute_end_ : minute_end_ + MinutesPerDay;
            for (auto i = begin; i < end; ++i)
            {
                minutes_.set((i + day * MinutesPerDay) % MinutesPerWeek);
            }
        }
    }
}

void tr_session_alt_speeds::updateScheduler()
{
    updateMinutes();
    scheduler_set_is_active_to_.reset();
    checkScheduler();
}

void tr_session_alt_speeds::checkScheduler()
{
    if (!isSchedulerEnabled())
    {
        return;
    }

    if (auto const active = isActiveMinute(mediator_.time());
        !scheduler_set_is_active_to_ || scheduler_set_is_active_to_ != active)
    {
        tr_logAddInfo(active ? _("Time to turn on turtle mode") : _("Time to turn off turtle mode"));
        scheduler_set_is_active_to_ = active;
        setActive(active, ChangeReason::Scheduler);
    }
}

void tr_session_alt_speeds::setActive(bool active, ChangeReason reason)
{
    if (is_active_ != active)
    {
        is_active_ = active;
        mediator_.isActiveChanged(is_active_, reason);
    }
}

[[nodiscard]] bool tr_session_alt_speeds::isActiveMinute(time_t time) const noexcept
{
    auto const tm = fmt::localtime(time);

    size_t minute_of_the_week = tm.tm_wday * MinutesPerDay + tm.tm_hour * MinutesPerHour + tm.tm_min;

    if (minute_of_the_week >= MinutesPerWeek) /* leap minutes? */
    {
        minute_of_the_week = MinutesPerWeek - 1;
    }

    return minutes_.test(minute_of_the_week);
}
