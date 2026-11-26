// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <libtransmission/env.h>
#include <libtransmission/file.h>

#include "libtransmission-app/favicon-cache.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace
{

struct TestIcon
{
    std::string data;

    explicit operator bool() const noexcept
    {
        return !std::empty(data);
    }
};

struct TestEnv
{
    using Task = std::pair<std::function<void()>, std::shared_ptr<std::promise<void>>>;

    std::string cache_dir;
    bool block_in_add_to_ui_thread = false;

    std::mutex mutex;
    std::condition_variable cv;
    std::queue<Task> tasks;
    bool closed = false;

    void enqueue(std::function<void()> func)
    {
        auto done = std::shared_ptr<std::promise<void>>{};
        auto future = std::future<void>{};

        {
            auto const lock = std::scoped_lock{ mutex };
            if (closed)
            {
                return;
            }

            if (block_in_add_to_ui_thread)
            {
                done = std::make_shared<std::promise<void>>();
                future = done->get_future();
            }

            tasks.emplace(std::move(func), done);
        }

        cv.notify_one();

        if (future.valid())
        {
            future.wait();
        }
    }

    bool wait_for_task(std::chrono::milliseconds timeout)
    {
        auto lock = std::unique_lock{ mutex };
        return cv.wait_for(lock, timeout, [this]() { return !std::empty(tasks); });
    }

    bool run_one(std::chrono::milliseconds timeout)
    {
        auto task = Task{};
        {
            auto lock = std::unique_lock{ mutex };
            if (!cv.wait_for(lock, timeout, [this]() { return !std::empty(tasks); }))
            {
                return false;
            }

            task = std::move(tasks.front());
            tasks.pop();
        }

        task.first();
        // Release the UI lambda (and its shared_ptr<InFlightData>) before
        // unblocking the curl thread so the curl thread drops the final ref.
        task.first = {};

        if (task.second)
        {
            task.second->set_value();
        }

        return true;
    }

    size_t drain_and_close()
    {
        auto remaining = std::queue<Task>{};
        {
            auto const lock = std::scoped_lock{ mutex };
            closed = true;
            std::swap(remaining, tasks);
        }

        auto n_ran = size_t{ 0 };
        while (!std::empty(remaining))
        {
            auto task = std::move(remaining.front());
            remaining.pop();
            task.first();
            task.first = {};
            if (task.second)
            {
                task.second->set_value();
            }
            ++n_ran;
        }

        return n_ran;
    }
};

TestEnv* g_env = nullptr;

class FaviconCacheTest : public TransmissionTest
{
protected:
    void SetUp() override
    {
        TransmissionTest::SetUp();

        auto parent_dir = tr_env_get_string("TMPDIR");
        if (std::empty(parent_dir) || !tr_sys_path_exists(parent_dir))
        {
            parent_dir = tr_sys_dir_get_current();
        }

        auto path = fmt::format("{:s}/transmission-favicon-test-XXXXXX", tr_sys_path_resolve(parent_dir));
        ASSERT_TRUE(tr_sys_dir_create_temp(std::data(path)));
        tr_sys_path_native_separators(std::data(path));

        env_ = std::make_unique<TestEnv>();
        env_->cache_dir = std::move(path);
        g_env = env_.get();
    }

    void TearDown() override
    {
        if (env_)
        {
            env_->drain_and_close();
            auto ec = std::error_code{};
            std::filesystem::remove_all(env_->cache_dir, ec);
        }

        g_env = nullptr;
        env_.reset();

        TransmissionTest::TearDown();
    }

    std::unique_ptr<TestEnv> env_;
};

} // namespace

namespace tr::app
{

template<>
TestIcon FaviconCache<TestIcon>::create_from_file(std::string_view filename) const
{
    auto ifs = std::ifstream{ std::string{ filename }, std::ios_base::binary };
    if (!ifs.is_open())
    {
        return {};
    }

    return TestIcon{ std::string{ std::istreambuf_iterator<char>{ ifs }, std::istreambuf_iterator<char>{} } };
}

template<>
TestIcon FaviconCache<TestIcon>::create_from_data(void const* /*data*/, size_t /*datalen*/) const
{
    return {};
}

template<>
std::string FaviconCache<TestIcon>::app_cache_dir() const
{
    return g_env != nullptr ? g_env->cache_dir : std::string{};
}

template<>
void FaviconCache<TestIcon>::add_to_ui_thread(std::function<void()> idlefunc)
{
    if (g_env != nullptr)
    {
        g_env->enqueue(std::move(idlefunc));
    }
}

} // namespace tr::app

TEST_F(FaviconCacheTest, loadCompletesCallbackOnUiThreadWithoutDeadlock)
{
    // Reproduce GH #8645 deterministically: make the UI thread finish running
    // and destroying each queued check_responses() lambda before the curl
    // thread returns from its fetch callback, forcing the curl thread to drop
    // the final reference to InFlightData.
    env_->block_in_add_to_ui_thread = true;

    auto const ui_thread_id = std::this_thread::get_id();
    auto callback_count = 0;
    auto callback_thread_id = std::thread::id{};
    TestIcon const* callback_icon = nullptr;

    auto cache = tr::app::FaviconCache<TestIcon>{};
    cache.load(
        "http://127.0.0.1:80/announce"sv,
        [&](TestIcon const* icon)
        {
            ++callback_count;
            callback_thread_id = std::this_thread::get_id();
            callback_icon = icon;
        });

    auto const deadline = std::chrono::steady_clock::now() + 30s;
    while (callback_count == 0 && std::chrono::steady_clock::now() < deadline)
    {
        env_->run_one(100ms);
    }

    env_->drain_and_close();

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(callback_thread_id, ui_thread_id);
    EXPECT_EQ(callback_icon, nullptr);
    EXPECT_NE(cache.find("127.0.0.1"sv), nullptr);
}

TEST_F(FaviconCacheTest, destroyWithFetchesInFlightDoesNotUseAfterFree)
{
    env_->block_in_add_to_ui_thread = false;

    auto callback_count = 0;
    auto cache = std::make_unique<tr::app::FaviconCache<TestIcon>>();
    cache->load("http://127.0.0.1:80/announce"sv, [&](TestIcon const* /*icon*/) { ++callback_count; });

    // Wait until at least one UI callback has been queued, then destroy the
    // cache before the UI thread drains its event loop.
    ASSERT_TRUE(env_->wait_for_task(30s));
    cache.reset();

    auto const n_ran = env_->drain_and_close();
    EXPECT_GT(n_ran, 0U);
    EXPECT_EQ(callback_count, 0);
}

TEST_F(FaviconCacheTest, loadInvalidUrlInvokesCallbackImmediately)
{
    auto const ui_thread_id = std::this_thread::get_id();
    auto callback_count = 0;
    auto callback_thread_id = std::thread::id{};
    TestIcon const* callback_icon = nullptr;

    auto cache = tr::app::FaviconCache<TestIcon>{};
    cache.load(
        "not a valid url"sv,
        [&](TestIcon const* icon)
        {
            ++callback_count;
            callback_thread_id = std::this_thread::get_id();
            callback_icon = icon;
        });

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(callback_thread_id, ui_thread_id);
    EXPECT_EQ(callback_icon, nullptr);
}

TEST_F(FaviconCacheTest, loadUsesDiskCachedIcon)
{
    auto const icons_dir = fmt::format("{:s}/favicons", env_->cache_dir);
    ASSERT_TRUE(tr_sys_dir_create(icons_dir, TR_SYS_DIR_CREATE_PARENTS, 0700));
    {
        auto ofs = std::ofstream{ fmt::format("{:s}/example", icons_dir), std::ios_base::binary };
        ofs << "fake-icon-bytes";
    }

    auto callback_count = 0;
    auto icon_data = std::string{};

    auto cache = tr::app::FaviconCache<TestIcon>{};
    cache.load(
        "http://example.com/announce"sv,
        [&](TestIcon const* icon)
        {
            ++callback_count;
            if (icon != nullptr)
            {
                icon_data = icon->data;
            }
        });

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(icon_data, "fake-icon-bytes");
    ASSERT_NE(cache.find("example"sv), nullptr);
    EXPECT_EQ(cache.find("example"sv)->data, "fake-icon-bytes");
}
