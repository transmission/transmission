// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <utility> // for std::move(), std::swap()

#include <csignal>

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <uv.h>

#include "libtransmission/session-thread.h"
#include "libtransmission/tr-assert.h"

using namespace std::literals;

// ---

void tr_session_thread::tr_evthread_init()
{
    // libuv doesn't require explicit thread initialization like libevent
    // This function is kept for API compatibility but is a no-op for libuv
}

class tr_session_thread_libuv_impl final : public tr_session_thread
{
public:
    explicit tr_session_thread_libuv_impl()
    {
        // Initialize the loop
        uv_loop_ = new uv_loop_t;
        if (uv_loop_init(uv_loop_) != 0)
        {
            delete uv_loop_;
            throw std::runtime_error("Failed to initialize uv_loop");
        }

        // Initialize the async handle for work queue notifications
        work_queue_async_ = new uv_async_t;
        if (uv_async_init(uv_loop_, work_queue_async_, on_work_available_static) != 0)
        {
            delete work_queue_async_;
            uv_loop_close(uv_loop_);
            delete uv_loop_;
            throw std::runtime_error("Failed to initialize uv_async");
        }
        work_queue_async_->data = this;

        // Keep the async handle referenced so the loop doesn't exit
        uv_ref(reinterpret_cast<uv_handle_t*>(work_queue_async_));

        auto lock = std::unique_lock(is_looping_mutex_);

        thread_ = std::thread(&tr_session_thread_libuv_impl::session_thread_func, this, uv_loop_);
        thread_id_ = thread_.get_id();

        // wait for the session thread's main loop to start
        is_looping_cv_.wait(lock, [this]() { return is_looping_.load(); });
    }

    tr_session_thread_libuv_impl(tr_session_thread_libuv_impl&&) = delete;
    tr_session_thread_libuv_impl(tr_session_thread_libuv_impl const&) = delete;
    tr_session_thread_libuv_impl& operator=(tr_session_thread_libuv_impl&&) = delete;
    tr_session_thread_libuv_impl& operator=(tr_session_thread_libuv_impl const&) = delete;

    ~tr_session_thread_libuv_impl() override
    {
        TR_ASSERT(!am_in_session_thread());
        TR_ASSERT(is_looping_);

        // Signal shutdown and send async notification to wake up the loop
        is_shutting_down_ = true;
        uv_async_send(work_queue_async_);

        // Wait for the thread to finish with timeout
        auto lock = std::unique_lock(is_looping_mutex_);
        is_looping_cv_.wait_for(lock, Deadline, [this]() { return !is_looping_; });
        lock.unlock();

        // Join the thread
        thread_.join();

        // Cleanup the loop
        if (uv_loop_ != nullptr)
        {
            uv_loop_close(uv_loop_);
            delete uv_loop_;
            uv_loop_ = nullptr;
        }
    }

    [[nodiscard]] struct event_base* event_base() noexcept override
    {
        // Return nullptr since this implementation doesn't use libevent
        // Code using this should check the return value or use a different interface
        return nullptr;
    }

    [[nodiscard]] struct uv_loop_s* uv_loop() noexcept override
    {
        return uv_loop_;
    }

    [[nodiscard]] bool am_in_session_thread() const noexcept override
    {
        return thread_id_ == std::this_thread::get_id();
    }

    void queue(std::function<void(void)>&& func) override
    {
        work_queue_mutex_.lock();
        work_queue_.emplace_back(std::move(func));
        work_queue_mutex_.unlock();

        // Signal the async handle to wake up the event loop
        uv_async_send(work_queue_async_);
    }

    void run(std::function<void(void)>&& func) override
    {
        if (am_in_session_thread())
        {
            func();
        }
        else
        {
            queue(std::move(func));
        }
    }

private:
    using callback = std::function<void(void)>;
    using work_queue_t = std::list<callback>;

    void session_thread_func(uv_loop_t* loop)
    {
#ifndef _WIN32
        /* Don't exit when writing on a broken socket */
        (void)signal(SIGPIPE, SIG_IGN);
#endif

        // Signal that the loop is starting
        {
            auto lock = std::unique_lock(is_looping_mutex_);
            is_looping_ = true;
        }
        is_looping_cv_.notify_one();

        // Run the event loop until uv_stop() is called
        uv_run(loop, UV_RUN_DEFAULT);

        if (work_queue_async_ != nullptr)
        {
            uv_close(
                reinterpret_cast<uv_handle_t*>(work_queue_async_),
                [](uv_handle_t* handle) { delete reinterpret_cast<uv_async_t*>(handle); });
            work_queue_async_ = nullptr; // Already closed/deleting via walk
        }

        // Process any remaining events and close callbacks during shutdown
        // Use UV_RUN_DEFAULT repeatedly until no more work to do
        while (uv_run(loop, UV_RUN_DEFAULT) != 0)
        {
            // Continue processing until all callbacks complete
        }

        // Signal that the loop has stopped
        {
            auto lock = std::unique_lock(is_looping_mutex_);
            is_looping_ = false;
        }
        is_looping_cv_.notify_one();
    }

    static void on_work_available_static(uv_async_t* handle)
    {
        static_cast<tr_session_thread_libuv_impl*>(handle->data)->on_work_available();
    }

    void on_work_available()
    {
        TR_ASSERT(am_in_session_thread());

        // Check if we're shutting down first
        if (is_shutting_down_)
        {
            // Stop the loop to break out of uv_run()
            uv_stop(uv_loop_);
            return;
        }

        // steal the work queue
        auto work_queue_lock = std::unique_lock(work_queue_mutex_);
        auto work_queue = work_queue_t{};
        std::swap(work_queue, work_queue_);
        work_queue_lock.unlock();

        // process the work queue
        for (auto const& func : work_queue)
        {
            func();
        }
    }

    uv_loop_t* uv_loop_ = nullptr;
    uv_async_t* work_queue_async_ = nullptr;

    work_queue_t work_queue_;
    std::mutex work_queue_mutex_;

    std::thread thread_;
    std::thread::id thread_id_;

    std::mutex is_looping_mutex_;
    std::condition_variable is_looping_cv_;
    std::atomic<bool> is_looping_ = false;

    std::atomic<bool> is_shutting_down_ = false;
    static constexpr std::chrono::seconds Deadline = 5s;
};

std::unique_ptr<tr_session_thread> tr_session_thread::create_libuv()
{
    return std::make_unique<tr_session_thread_libuv_impl>();
}
