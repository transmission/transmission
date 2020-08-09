/*
 * This file Copyright (C) 2015-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "file.h"
#include "net.h"
#include "utils.h"
#include "watchdir.h"

#include "test-fixtures.h"

#include <event2/event.h>

#include <map>
#include <string>

/***
****
***/

extern "C"
{
extern struct timeval tr_watchdir_generic_interval;
extern unsigned int tr_watchdir_retry_limit;
extern struct timeval tr_watchdir_retry_start_interval;
extern struct timeval tr_watchdir_retry_max_interval;
}

namespace
{

auto constexpr FiftyMsec = timeval { 0, 50000 };
auto constexpr OneHundredMsec = timeval { 0, 100000 };
auto constexpr TwoHundredMsec = timeval { 0, 200000 };

}

namespace libtransmission
{

namespace test
{

enum class WatchMode
{
    NATIVE,
    GENERIC
};

class WatchDirTest :
    public SandboxedTest,
    public ::testing::WithParamInterface<WatchMode>
{
private:
    std::shared_ptr<struct event_base> ev_base_;

protected:
    void SetUp() override
    {
        SandboxedTest::SetUp();
        ev_base_.reset(event_base_new(), event_base_free);

        // speed up generic implementation
        tr_watchdir_generic_interval = OneHundredMsec;
    }

    void TearDown() override
    {
        ev_base_.reset();

        SandboxedTest::TearDown();
    }

    auto createWatchDir(std::string const& path, tr_watchdir_cb cb, void* cb_data)
    {
        auto const force_generic = GetParam() == WatchMode::GENERIC;
        return tr_watchdir_new(path.c_str(), cb, cb_data, ev_base_.get(), force_generic);
    }

    std::string createFile(std::string const& parent_dir, std::string const& name)
    {
        auto path = parent_dir;
        path += TR_PATH_DELIMITER;
        path += name;

        createFileWithContents(path, "");

        return path;
    }

    std::string createDir(std::string const& parent_dir, std::string const& name)
    {
        auto path = parent_dir;
        path += TR_PATH_DELIMITER;
        path += name;

        tr_sys_dir_create(path.c_str(), 0, 0700, nullptr);

        return path;
    }

    void processEvents()
    {
        event_base_loopexit(ev_base_.get(), &TwoHundredMsec);
        event_base_dispatch(ev_base_.get());
    }

    struct CallbackData
    {
        explicit CallbackData(tr_watchdir_status status = TR_WATCHDIR_ACCEPT) :
            result{status} {}
        tr_watchdir_status result {};

        tr_watchdir_t wd = {};
        std::string name = {};
    };

    static tr_watchdir_status callback(tr_watchdir_t wd, char const* name, void* vdata) noexcept
    {
        auto* data = static_cast<CallbackData*>(vdata);
        auto const result = data->result;

        if (result != TR_WATCHDIR_RETRY)
        {
            data->wd = wd;
            data->name = name;
        }

        return result;
    }
};

TEST_P(WatchDirTest, construct)
{
    auto const path = sandboxDir();

    auto wd = createWatchDir(path, &callback, nullptr);
    EXPECT_NE(nullptr, wd);
    EXPECT_TRUE(tr_sys_path_is_same(path.c_str(), tr_watchdir_get_path(wd), nullptr));

    processEvents();

    tr_watchdir_free(wd);
}

TEST_P(WatchDirTest, initialScan)
{
    auto const path = sandboxDir();

    // setup: start with an empty directory.
    // this block confirms that it's empty
    {
        auto wd_data = CallbackData(TR_WATCHDIR_ACCEPT);
        auto wd = createWatchDir(path, &callback, &wd_data);
        EXPECT_NE(nullptr, wd);

        processEvents();
        EXPECT_EQ(nullptr, wd_data.wd);
        EXPECT_EQ("", wd_data.name);

        tr_watchdir_free(wd);
    }

    // add a file
    auto const base_name = std::string { "test.txt" };
    createFile(path, base_name);

    // confirm that a wd will pick up the file that
    // was created before the wd was instantiated
    {
        auto wd_data = CallbackData(TR_WATCHDIR_ACCEPT);
        auto wd = createWatchDir(path, &callback, &wd_data);
        EXPECT_NE(nullptr, wd);

        processEvents();
        EXPECT_EQ(wd, wd_data.wd);
        EXPECT_EQ(base_name, wd_data.name);

        tr_watchdir_free(wd);
    }
}

TEST_P(WatchDirTest, watch)
{
    auto const path = sandboxDir();

    // create a new watchdir and confirm it's empty
    auto wd_data = CallbackData(TR_WATCHDIR_ACCEPT);
    auto wd = createWatchDir(path, &callback, &wd_data);
    EXPECT_NE(nullptr, wd);
    processEvents();
    EXPECT_EQ(nullptr, wd_data.wd);
    EXPECT_EQ("", wd_data.name);

    // test that a new file in an empty directory shows up
    auto const file1 = std::string { "test1" };
    createFile(path, file1);
    processEvents();
    EXPECT_EQ(wd, wd_data.wd);
    EXPECT_EQ(file1, wd_data.name);

    // test that a new file in a nonempty directory shows up
    wd_data = CallbackData(TR_WATCHDIR_ACCEPT);
    auto const file2 = std::string { "test2" };
    createFile(path, file2);
    processEvents();
    EXPECT_EQ(wd, wd_data.wd);
    EXPECT_EQ(file2, wd_data.name);

    // test that folders don't trigger the callback
    wd_data = CallbackData(TR_WATCHDIR_ACCEPT);
    createDir(path, "test3");
    processEvents();
    EXPECT_EQ(nullptr, wd_data.wd);
    EXPECT_EQ("", wd_data.name);

    // cleanup
    tr_watchdir_free(wd);
}

TEST_P(WatchDirTest, watchTwoDirs)
{
    auto top = sandboxDir();

    // create two empty directories and watch them
    auto wd1_data = CallbackData(TR_WATCHDIR_ACCEPT);
    auto const dir1 = createDir(top, "a");
    auto wd1 = createWatchDir(dir1, &callback, &wd1_data);
    EXPECT_NE(wd1, nullptr);
    auto wd2_data = CallbackData(TR_WATCHDIR_ACCEPT);
    auto const dir2 = createDir(top, "b");
    auto wd2 = createWatchDir(dir2, &callback, &wd2_data);
    EXPECT_NE(wd2, nullptr);

    processEvents();
    EXPECT_EQ(nullptr, wd1_data.wd);
    EXPECT_EQ("", wd1_data.name);
    EXPECT_EQ(nullptr, wd2_data.wd);
    EXPECT_EQ("", wd2_data.name);

    // add a file into directory 1 and confirm it triggers
    // a callback with the right wd
    auto const file1 = std::string { "test.txt" };
    createFile(dir1, file1);
    processEvents();
    EXPECT_EQ(wd1, wd1_data.wd);
    EXPECT_EQ(file1, wd1_data.name);
    EXPECT_EQ(nullptr, wd2_data.wd);
    EXPECT_EQ("", wd2_data.name);

    // add a file into directory 2 and confirm it triggers
    // a callback with the right wd
    wd1_data = CallbackData(TR_WATCHDIR_ACCEPT);
    wd2_data = CallbackData(TR_WATCHDIR_ACCEPT);
    auto const file2 = std::string { "test2.txt" };
    createFile(dir2, file2);
    processEvents();
    EXPECT_EQ(nullptr, wd1_data.wd);
    EXPECT_EQ("", wd1_data.name);
    EXPECT_EQ(wd2, wd2_data.wd);
    EXPECT_EQ(file2, wd2_data.name);

    // TODO(ckerr): watchdir.c seems to treat IGNORE and ACCEPT identically
    // so I'm not sure what's intended or what this is supposed to
    // be testing.
    wd1_data = CallbackData(TR_WATCHDIR_IGNORE);
    wd2_data = CallbackData(TR_WATCHDIR_IGNORE);
    auto const file3 = std::string { "test3.txt" };
    auto const file4 = std::string { "test4.txt" };
    createFile(dir1, file3);
    createFile(dir2, file4);
    processEvents();
    EXPECT_EQ(wd1, wd1_data.wd);
    EXPECT_EQ(file3, wd1_data.name);
    EXPECT_EQ(wd2, wd2_data.wd);
    EXPECT_EQ(file4, wd2_data.name);

    // confirm that callbacks don't get confused
    // when there's a new file in directory 'a'
    // and a new directory in directory 'b'
    wd1_data = CallbackData(TR_WATCHDIR_ACCEPT);
    wd2_data = CallbackData(TR_WATCHDIR_ACCEPT);
    auto const file5 = std::string { "test5.txt" };
    createFile(dir1, file5);
    createDir(dir2, file5);
    processEvents();
    EXPECT_EQ(wd1, wd1_data.wd);
    EXPECT_EQ(file5, wd1_data.name);
    EXPECT_EQ(nullptr, wd2_data.wd);
    EXPECT_EQ("", wd2_data.name);

    // reverse the order of the previous test:
    // confirm that callbacks don't get confused
    // when there's a new file in directory 'b'
    // and a new directory in directory 'a'
    wd1_data = CallbackData(TR_WATCHDIR_ACCEPT);
    wd2_data = CallbackData(TR_WATCHDIR_ACCEPT);
    auto const file6 = std::string { "test6.txt" };
    createDir(dir1, file6);
    createFile(dir2, file6);
    processEvents();
    EXPECT_EQ(nullptr, wd1_data.wd);
    EXPECT_EQ("", wd1_data.name);
    EXPECT_EQ(wd2, wd2_data.wd);
    EXPECT_EQ(file6, wd2_data.name);

    // confirm that creating new directories in BOTH
    // watchdirs still triggers no callbacks
    wd1_data = CallbackData(TR_WATCHDIR_ACCEPT);
    wd2_data = CallbackData(TR_WATCHDIR_ACCEPT);
    auto const file7 = std::string { "test7.txt" };
    auto const file8 = std::string { "test8.txt" };
    createDir(dir1, file7);
    createDir(dir2, file8);
    processEvents();
    EXPECT_EQ(nullptr, wd1_data.wd);
    EXPECT_EQ("", wd1_data.name);
    EXPECT_EQ(nullptr, wd2_data.wd);
    EXPECT_EQ("", wd2_data.name);

    // cleanup
    tr_watchdir_free(wd2);
    tr_watchdir_free(wd1);
}

TEST_P(WatchDirTest, retry)
{
    auto const path = sandboxDir();

    // tune retry logic
    tr_watchdir_retry_limit = 10;
    tr_watchdir_retry_start_interval = FiftyMsec;
    tr_watchdir_retry_max_interval = tr_watchdir_retry_start_interval;

    // test setup:
    // Start watching the test directory.
    // Create a file and return 'retry' back to the watchdir code
    // from our callback. This should cause the wd to wait a bit
    // and try again.
    auto wd_data = CallbackData(TR_WATCHDIR_RETRY);
    auto wd = createWatchDir(path, &callback, &wd_data);
    EXPECT_NE(nullptr, wd);
    processEvents();
    EXPECT_EQ(nullptr, wd_data.wd);
    EXPECT_EQ("", wd_data.name);

    auto const test_file = std::string { "test" };
    createFile(path, test_file);
    processEvents();
    EXPECT_EQ(nullptr, wd_data.wd);
    EXPECT_EQ("", wd_data.name);

    // confirm that wd retries.
    // return 'accept' in the callback so it won't keep retrying.
    wd_data = CallbackData(TR_WATCHDIR_ACCEPT);
    processEvents();
    EXPECT_EQ(wd, wd_data.wd);
    EXPECT_EQ(test_file, wd_data.name);
}

INSTANTIATE_TEST_SUITE_P(
    WatchDir,
    WatchDirTest,
    ::testing::Values(WatchMode::NATIVE, WatchMode::GENERIC)
    );

} // namespace test

} // namespace libtransmission
