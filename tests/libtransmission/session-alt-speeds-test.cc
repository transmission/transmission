// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <ctime>
#include <vector>

#include <fmt/chrono.h>

#include "transmission.h"
#include "session.h"
#include "session-id.h"
#include "version.h"

#include "test-fixtures.h"

using namespace std::literals;

class SessionAltSpeedsTest : public ::testing::Test
{
protected:
    using ChangeReason = tr_session_alt_speeds::ChangeReason;

    class MockMediator final : public tr_session_alt_speeds::Mediator
    {
    public:
        ~MockMediator() override = default;

        void isActiveChanged(bool is_active, ChangeReason reason) override
        {
            changelog_.emplace_back(is_active, reason, time());
        }

        [[nodiscard]] time_t time() override
        {
            return current_time_;
        }

        time_t current_time_ = 0;

        struct Change
        {
            Change() = default;

            Change(bool is_active_in, ChangeReason reason_in, time_t timestamp_in)
                : is_active{ is_active_in }
                , reason{ reason_in }
                , timestamp{ timestamp_in }
            {
            }

            bool is_active = false;
            ChangeReason reason = ChangeReason::User;
            time_t timestamp = 0;
        };

        std::vector<Change> changelog_;
    };

    static auto constexpr ArbitraryTimestamp1 = time_t{ 666 };

    static auto midnightSundayMorning()
    {
        // new year's, Sun Jan 1 2023
        auto tmdate = tm{};
        tmdate.tm_sec = 0;
        tmdate.tm_min = 0;
        tmdate.tm_hour = 0;
        tmdate.tm_mday = 1;
        tmdate.tm_mon = 0;
        tmdate.tm_year = 2023 - 1900;
        tmdate.tm_wday = 0; // Sunday
        tmdate.tm_yday = 0;
        tmdate.tm_isdst = 0;

        fmt::print("{:s}:{:d} tm_sec {:d}\n", __FILE__, __LINE__, tmdate.tm_sec);
        fmt::print("{:s}:{:d} tm_min {:d}\n", __FILE__, __LINE__, tmdate.tm_min);
        fmt::print("{:s}:{:d} tm_hour {:d}\n", __FILE__, __LINE__, tmdate.tm_hour);
        fmt::print("{:s}:{:d} tm_mday {:d}\n", __FILE__, __LINE__, tmdate.tm_mday);
        fmt::print("{:s}:{:d} tm_mon {:d}\n", __FILE__, __LINE__, tmdate.tm_mon);
        fmt::print("{:s}:{:d} tm_year {:d}\n", __FILE__, __LINE__, tmdate.tm_year);
        fmt::print("{:s}:{:d} tm_wday {:d}\n", __FILE__, __LINE__, tmdate.tm_wday);
        fmt::print("{:s}:{:d} tm_yday {:d}\n", __FILE__, __LINE__, tmdate.tm_yday);
        fmt::print("{:s}:{:d} tm_isdst {:d}\n", __FILE__, __LINE__, tmdate.tm_isdst);
        fmt::print("{:s}:{:d}\n", __FILE__, __LINE__);

        auto const ret = mktime(&tmdate);
        fmt::print("{:s}:{:d} tm_sec {:d}\n", __FILE__, __LINE__, tmdate.tm_sec);
        fmt::print("{:s}:{:d} tm_min {:d}\n", __FILE__, __LINE__, tmdate.tm_min);
        fmt::print("{:s}:{:d} tm_hour {:d}\n", __FILE__, __LINE__, tmdate.tm_hour);
        fmt::print("{:s}:{:d} tm_mday {:d}\n", __FILE__, __LINE__, tmdate.tm_mday);
        fmt::print("{:s}:{:d} tm_mon {:d}\n", __FILE__, __LINE__, tmdate.tm_mon);
        fmt::print("{:s}:{:d} tm_year {:d}\n", __FILE__, __LINE__, tmdate.tm_year);
        fmt::print("{:s}:{:d} tm_wday {:d}\n", __FILE__, __LINE__, tmdate.tm_wday);
        fmt::print("{:s}:{:d} tm_yday {:d}\n", __FILE__, __LINE__, tmdate.tm_yday);
        fmt::print("{:s}:{:d} tm_isdst {:d}\n", __FILE__, __LINE__, tmdate.tm_isdst);
        fmt::print("{:s}:{:d}\n", __FILE__, __LINE__);
        fmt::print(FMT_STRING("{:s}:{:d} {:d}\n"), __FILE__, __LINE__, ret);
        fmt::print(FMT_STRING("{:s}:{:d} {:%Y-%m-%d %H:%M:%S}\n"), __FILE__, __LINE__, tmdate);
        return ret;
    }
};

namespace libtransmission::test
{

TEST_F(SessionAltSpeedsTest, canInstantiate)
{
    auto mediator = MockMediator{};
    auto alt_speeds = tr_session_alt_speeds{ mediator };
    EXPECT_FALSE(alt_speeds.isActive());
}

TEST_F(SessionAltSpeedsTest, canActivate)
{
    static auto constexpr Now = ArbitraryTimestamp1;
    auto mediator = MockMediator{};
    mediator.current_time_ = Now;

    auto alt_speeds = tr_session_alt_speeds{ mediator };
    auto const changed_value = !alt_speeds.isActive();
    EXPECT_EQ(0U, std::size(mediator.changelog_));

    static auto constexpr Reason = ChangeReason::User;
    alt_speeds.setActive(changed_value, Reason);
    EXPECT_EQ(changed_value, alt_speeds.isActive());
    ASSERT_EQ(1U, std::size(mediator.changelog_));
    EXPECT_EQ(changed_value, mediator.changelog_[0].is_active);
    EXPECT_EQ(Reason, mediator.changelog_[0].reason);
    EXPECT_EQ(Now, mediator.changelog_[0].timestamp);
}

TEST_F(SessionAltSpeedsTest, canSchedule)
{
    auto mediator = MockMediator{};

    auto now = midnightSundayMorning(); // midnight
    mediator.current_time_ = now;
    fmt::print("{:s}:{:d} {:%Y-%m-%d %H:%M:%S}\n", __FILE__, __LINE__, fmt::localtime(now));

    auto alt_speeds = tr_session_alt_speeds{ mediator };
    alt_speeds.setStartMinute(60); // start at 1AM
    alt_speeds.setEndMinute(120); // end at 2AM
    alt_speeds.setWeekdays(TR_SCHED_ALL); // every day
    alt_speeds.setSchedulerEnabled(true);
    auto n_changes = std::size(mediator.changelog_);
    EXPECT_EQ(0U, n_changes);

    // Confirm that walking up to the threshold, but not crossing it, does not enable
    now += std::chrono::duration_cast<std::chrono::seconds>(59min).count();
    fmt::print("{:s}:{:d} {:%Y-%m-%d %H:%M:%S}\n", __FILE__, __LINE__, fmt::localtime(now));
    mediator.current_time_ = now;
    alt_speeds.checkScheduler();
    EXPECT_EQ(n_changes, std::size(mediator.changelog_));

    // Confirm that crossin the threshold does enable
    now += std::chrono::duration_cast<std::chrono::seconds>(1min).count();
    fmt::print("{:s}:{:d} {:%Y-%m-%d %H:%M:%S}\n", __FILE__, __LINE__, fmt::localtime(now));
    mediator.current_time_ = now;
    alt_speeds.checkScheduler();
    ASSERT_EQ(n_changes + 1, std::size(mediator.changelog_));
    EXPECT_EQ(true, mediator.changelog_[n_changes].is_active);
    EXPECT_EQ(ChangeReason::Scheduler, mediator.changelog_[n_changes].reason);
    EXPECT_EQ(now, mediator.changelog_[n_changes].timestamp);
    ++n_changes;

    // Confirm that walking up to the threshold, but not crossing it, does not disable
    now += std::chrono::duration_cast<std::chrono::seconds>(59min).count();
    fmt::print("{:s}:{:d} {:%Y-%m-%d %H:%M:%S}\n", __FILE__, __LINE__, fmt::localtime(now));
    mediator.current_time_ = now;
    alt_speeds.checkScheduler();
    EXPECT_EQ(n_changes, std::size(mediator.changelog_));

    // Confirm that crossin the threshold does disable
    now += std::chrono::duration_cast<std::chrono::seconds>(1min).count();
    fmt::print("{:s}:{:d} {:%Y-%m-%d %H:%M:%S}\n", __FILE__, __LINE__, fmt::localtime(now));
    mediator.current_time_ = now;
    alt_speeds.checkScheduler();
    ASSERT_EQ(n_changes + 1, std::size(mediator.changelog_));
    EXPECT_EQ(false, mediator.changelog_[n_changes].is_active);
    EXPECT_EQ(ChangeReason::Scheduler, mediator.changelog_[n_changes].reason);
    EXPECT_EQ(now, mediator.changelog_[n_changes].timestamp);
}

} // namespace libtransmission::test
