// This file Copyright (C) 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include <libtransmission/transmission.h>

#include <libtransmission/file.h>
#include <libtransmission/net.h>
#include <libtransmission/watchdir.h>
#include <libtransmission/watchdir-base.h>
#include <libtransmission/timer-ev.h>

#include "test-fixtures.h"

#include <event2/event.h>

using namespace std::literals;

/***
****
***/

static auto constexpr GenericRescanInterval = 100ms;
static auto constexpr RetryDuration = 100ms;

// should be at least 2x the watchdir-generic size to ensure that
// we have time to pump all events at least once in processEvents()
static auto constexpr ProcessEventsTimeout = 300ms;
static_assert(ProcessEventsTimeout > GenericRescanInterval);

namespace libtransmission::test
{

enum class WatchMode
{
    NATIVE,
    GENERIC
};

class WatchDirTest
    : public SandboxedTest
    , public ::testing::WithParamInterface<WatchMode>
{
private:
    std::shared_ptr<struct event_base> ev_base_;
    std::unique_ptr<libtransmission::TimerMaker> timer_maker_;

protected:
    void SetUp() override
    {
        SandboxedTest::SetUp();
        ev_base_.reset(event_base_new(), event_base_free);
        timer_maker_ = std::make_unique<libtransmission::EvTimerMaker>(ev_base_.get());
        Watchdir::setGenericRescanInterval(GenericRescanInterval);
    }

    void TearDown() override
    {
        ev_base_.reset();

        SandboxedTest::TearDown();
    }

    auto createWatchDir(std::string_view path, Watchdir::Callback callback)
    {
        auto const force_generic = GetParam() == WatchMode::GENERIC;
        auto watchdir = force_generic ?
            Watchdir::createGeneric(path, std::move(callback), *timer_maker_, GenericRescanInterval) :
            Watchdir::create(path, std::move(callback), *timer_maker_, ev_base_.get());

        if (auto* const base_watchdir = dynamic_cast<impl::BaseWatchdir*>(watchdir.get()); base_watchdir != nullptr)
        {
            base_watchdir->setRetryDuration(RetryDuration);
        }

        return watchdir;
    }

    void createFile(std::string_view dirname, std::string_view basename, std::string_view contents = ""sv)
    {
        createFileWithContents(tr_pathbuf{ dirname, '/', basename }, contents);
    }

    static std::string createDir(std::string_view dirname, std::string_view basename)
    {
        auto path = std::string{ dirname };
        path += TR_PATH_DELIMITER;
        path += basename;

        tr_sys_dir_create(path, 0, 0700);

        return path;
    }

    void processEvents(std::chrono::milliseconds wait_interval = ProcessEventsTimeout)
    {
        auto tv = timeval{};
        auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(wait_interval);
        tv.tv_sec = static_cast<decltype(tv.tv_sec)>(seconds.count());

        wait_interval -= seconds;
        auto const usec = std::chrono::duration_cast<std::chrono::microseconds>(wait_interval);
        tv.tv_usec = static_cast<decltype(tv.tv_usec)>(usec.count());

        event_base_loopexit(ev_base_.get(), &tv);
        event_base_dispatch(ev_base_.get());
    }
};

TEST_P(WatchDirTest, construct)
{
    auto const path = sandboxDir();

    auto callback = [](std::string_view /*dirname*/, std::string_view /*basename*/)
    {
        return Watchdir::Action::Done;
    };
    auto watchdir = createWatchDir(path, callback);
    EXPECT_TRUE(watchdir);
    EXPECT_EQ(path, watchdir->dirname());

    processEvents();
}

TEST_P(WatchDirTest, initialScan)
{
    auto const path = sandboxDir();

    // setup: start with an empty directory.
    // this block confirms that it's empty
    {
        auto called = bool{ false };
        auto callback = [&called](std::string_view /*dirname*/, std::string_view /*basename*/)
        {
            called = true;
            return Watchdir::Action::Done;
        };
        auto watchdir = createWatchDir(path, callback);
        EXPECT_TRUE(watchdir);
        processEvents();
        EXPECT_FALSE(called);
    }

    // add a file
    auto const base_name = "test.txt"sv;
    createFile(path, base_name);

    // confirm that a wd will pick up the file that
    // was created before the wd was instantiated
    {
        auto names = std::set<std::string>{};
        auto callback = [&names](std::string_view /*dirname*/, std::string_view basename)
        {
            names.insert(std::string{ basename });
            return Watchdir::Action::Done;
        };
        auto watchdir = createWatchDir(path, callback);
        EXPECT_TRUE(watchdir);
        processEvents();
        EXPECT_EQ(1U, std::size(names));
        EXPECT_EQ(base_name, *names.begin());
    }
}

TEST_P(WatchDirTest, watch)
{
    auto const dirname = sandboxDir();

    // create a new watchdir and confirm it's empty
    auto names = std::vector<std::string>{};
    auto callback = [&names](std::string_view /*dirname*/, std::string_view basename)
    {
        names.emplace_back(basename);
        return Watchdir::Action::Done;
    };
    auto watchdir = createWatchDir(dirname, callback);
    processEvents();
    EXPECT_TRUE(watchdir);
    EXPECT_TRUE(std::empty(names));

    // test that a new file in an empty directory shows up
    auto const file1 = "test1"sv;
    createFile(dirname, file1);
    processEvents();
    EXPECT_EQ(1U, std::size(names));
    if (!std::empty(names))
    {
        EXPECT_EQ(file1, names.front());
    }

    // test that a new file in a nonempty directory shows up
    names.clear();
    auto const file2 = "test2"sv;
    createFile(dirname, file2);
    processEvents();
    processEvents();
    EXPECT_EQ(1U, std::size(names));
    if (!std::empty(names))
    {
        EXPECT_EQ(file2, names.front());
    }

    // test that folders don't trigger the callback
    names.clear();
    createDir(dirname, "test3"sv);
    processEvents();
    EXPECT_TRUE(std::empty(names));
}

TEST_P(WatchDirTest, DISABLED_retry)
{
    auto const path = sandboxDir();

    // test setup:
    // Start watching the test directory.
    // Create a file and return 'retry' back to the watchdir code from our callback.
    // This should cause the wd to wait a bit and try again.
    auto names = std::vector<std::string>{};
    auto callback = [&names](std::string_view /*dirname*/, std::string_view basename)
    {
        names.emplace_back(basename);
        return Watchdir::Action::Retry;
    };
    auto watchdir = createWatchDir(path, callback);
    auto constexpr FastRetryWaitTime = 20ms;
    auto constexpr SlowRetryWaitTime = 200ms;
    auto* const base_watchdir = dynamic_cast<impl::BaseWatchdir*>(watchdir.get());
    ASSERT_TRUE(base_watchdir != nullptr);
    base_watchdir->setRetryDuration(FastRetryWaitTime);

    processEvents(SlowRetryWaitTime);
    EXPECT_EQ(0U, std::size(names));

    auto const test_file = "test.txt"sv;
    createFile(path, test_file);
    processEvents(SlowRetryWaitTime);
    EXPECT_LE(2U, std::size(names));
    for (auto const& name : names)
    {
        EXPECT_EQ(test_file, name);
    }
}

INSTANTIATE_TEST_SUITE_P( //
    WatchDir,
    WatchDirTest,
    ::testing::Values( //
        WatchMode::NATIVE,
        WatchMode::GENERIC));

} // namespace libtransmission::test
