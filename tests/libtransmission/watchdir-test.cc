// This file Copyright (C) 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <string>
#include <vector>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "transmission.h"

#include "file.h"
#include "net.h"
#include "watchdir.h"
#include "watchdir-base.h"

#include "test-fixtures.h"

#include <event2/event.h>

/***
****
***/

namespace
{

auto constexpr GenericRescanIntervalMsec = size_t{ 100U };

// should be at least 2x the watchdir-generic size to ensure that
// we have time to pump all events at least once in processEvents()
auto constexpr DefaultProcessEventsTimeoutMsec = time_t{ 200U };
static_assert(DefaultProcessEventsTimeoutMsec > GenericRescanIntervalMsec);
auto process_events_timeout_msec = DefaultProcessEventsTimeoutMsec;

namespace current_time_mock
{
namespace
{
auto value = time_t{};
}

time_t get()
{
    return value;
}

#if 0
void set(time_t now)
{
    value = now;
}
#endif

} // namespace current_time_mock

} // namespace

namespace libtransmission
{

namespace test
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

protected:
    void SetUp() override
    {
        SandboxedTest::SetUp();
        ev_base_.reset(event_base_new(), event_base_free);
        tr_watchdir::setGenericRescanIntervalMsec(GenericRescanIntervalMsec);
        process_events_timeout_msec = DefaultProcessEventsTimeoutMsec;
        retry_multiplier_msec_ = 100U;
    }

    void TearDown() override
    {
        ev_base_.reset();

        SandboxedTest::TearDown();
    }

    auto createWatchDir(std::string_view path, tr_watchdir::Callback callback)
    {
        auto const force_generic = GetParam() == WatchMode::GENERIC;
        auto watchdir = force_generic ?
            tr_watchdir::createGeneric(path, std::move(callback), ev_base_.get(), current_time_mock::get) :
            tr_watchdir::create(path, std::move(callback), ev_base_.get(), current_time_mock::get);
        dynamic_cast<tr_watchdir_base*>(watchdir.get())->setRetryMultiplierMsec(retry_multiplier_msec_);
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

    void processEvents()
    {
        auto const interval = timeval{ process_events_timeout_msec / 1000U,
                                       static_cast<long>((process_events_timeout_msec % 1000U) * 1000U) };
        event_base_loopexit(ev_base_.get(), &interval);
        event_base_dispatch(ev_base_.get());
    }

    size_t retry_multiplier_msec_ = 100U;
};

TEST_P(WatchDirTest, construct)
{
    auto const path = sandboxDir();

    auto callback = [](std::string_view /*dirname*/, std::string_view /*basename*/)
    {
        return tr_watchdir::Action::Done;
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
            return tr_watchdir::Action::Done;
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
            return tr_watchdir::Action::Done;
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
        names.emplace_back(std::string{ basename });
        return tr_watchdir::Action::Done;
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

TEST_P(WatchDirTest, retry)
{
    auto const path = sandboxDir();

    // test setup:
    // Start watching the test directory.
    // Create a file and return 'retry' back to the watchdir code from our callback.
    // This should cause the wd to wait a bit and try again.
    auto names = std::vector<std::string>{};
    auto callback = [&names](std::string_view /*dirname*/, std::string_view basename)
    {
        names.emplace_back(std::string{ basename });
        return tr_watchdir::Action::Retry;
    };
    retry_multiplier_msec_ = 50U;
    auto watchdir = createWatchDir(path, callback);

    processEvents();
    EXPECT_EQ(0U, std::size(names));

    auto const test_file = "test.txt"sv;
    createFile(path, test_file);
    processEvents();
    EXPECT_LE(2, std::size(names));
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

} // namespace test

} // namespace libtransmission
