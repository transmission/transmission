// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <ctime>
#include <vector>

#include <libtransmission/transmission.h>
#include <libtransmission/session-alt-speeds.h>

#include "test-fixtures.h"

using namespace std::literals;

class SessionAltSpeedsTest : public ::tr::test::TransmissionTest
{
protected:
    using ChangeReason = tr_session_alt_speeds::ChangeReason;

    class MockMediator final : public tr_session_alt_speeds::Mediator
    {
    public:
        void is_active_changed(bool is_active, ChangeReason reason) override
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
};

namespace tr::test
{

TEST_F(SessionAltSpeedsTest, canInstantiate)
{
    auto mediator = MockMediator{};
    auto alt_speeds = tr_session_alt_speeds{ mediator };
    EXPECT_FALSE(alt_speeds.is_active());
}

TEST_F(SessionAltSpeedsTest, canActivate)
{
    static auto constexpr Now = ArbitraryTimestamp1;
    auto mediator = MockMediator{};
    mediator.current_time_ = Now;

    auto alt_speeds = tr_session_alt_speeds{ mediator };
    auto const changed_value = !alt_speeds.is_active();
    EXPECT_EQ(0U, std::size(mediator.changelog_));

    static auto constexpr Reason = ChangeReason::User;
    alt_speeds.set_active(changed_value, Reason);
    EXPECT_EQ(changed_value, alt_speeds.is_active());
    ASSERT_EQ(1U, std::size(mediator.changelog_));
    EXPECT_EQ(changed_value, mediator.changelog_[0].is_active);
    EXPECT_EQ(Reason, mediator.changelog_[0].reason);
    EXPECT_EQ(Now, mediator.changelog_[0].timestamp);
}

TEST_F(SessionAltSpeedsTest, canSchedule)
{
    auto mediator = MockMediator{};

    auto current_timestamp = ArbitraryTimestamp1;
    mediator.current_time_ = current_timestamp;

    auto const current_local_time = *std::localtime(&current_timestamp);
    auto const current_minute_of_day = static_cast<size_t>(current_local_time.tm_hour * 60 + current_local_time.tm_min);
    auto const schedule_start_minute = current_minute_of_day + 60U;
    auto const schedule_end_minute = schedule_start_minute + 60U;

    auto alt_speeds = tr_session_alt_speeds{ mediator };
    alt_speeds.set_start_minute(schedule_start_minute);
    alt_speeds.set_end_minute(schedule_end_minute);
    alt_speeds.set_weekdays(TR_SCHED_ALL);
    alt_speeds.set_scheduler_enabled(true);
    auto n_changes = std::size(mediator.changelog_);
    EXPECT_EQ(0U, n_changes);

    // Confirm that walking up to the threshold, but not crossing it, does not enable
    current_timestamp += std::chrono::duration_cast<std::chrono::seconds>(59min).count();
    mediator.current_time_ = current_timestamp;
    alt_speeds.check_scheduler();
    EXPECT_EQ(n_changes, std::size(mediator.changelog_));

    // Confirm that crossing the threshold does enable
    current_timestamp += std::chrono::duration_cast<std::chrono::seconds>(1min).count();
    mediator.current_time_ = current_timestamp;
    alt_speeds.check_scheduler();
    ASSERT_EQ(n_changes + 1, std::size(mediator.changelog_));
    EXPECT_EQ(true, mediator.changelog_[n_changes].is_active);
    EXPECT_EQ(ChangeReason::Scheduler, mediator.changelog_[n_changes].reason);
    EXPECT_EQ(current_timestamp, mediator.changelog_[n_changes].timestamp);
    ++n_changes;

    // Confirm that walking up to the threshold, but not crossing it, does not disable
    current_timestamp += std::chrono::duration_cast<std::chrono::seconds>(59min).count();
    mediator.current_time_ = current_timestamp;
    alt_speeds.check_scheduler();
    EXPECT_EQ(n_changes, std::size(mediator.changelog_));

    // Confirm that crossing the threshold does disable
    current_timestamp += std::chrono::duration_cast<std::chrono::seconds>(1min).count();
    mediator.current_time_ = current_timestamp;
    alt_speeds.check_scheduler();
    ASSERT_EQ(n_changes + 1, std::size(mediator.changelog_));
    EXPECT_EQ(false, mediator.changelog_[n_changes].is_active);
    EXPECT_EQ(ChangeReason::Scheduler, mediator.changelog_[n_changes].reason);
    EXPECT_EQ(current_timestamp, mediator.changelog_[n_changes].timestamp);
}

} // namespace tr::test
