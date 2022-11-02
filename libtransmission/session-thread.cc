// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

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

#include <event2/event.h>
#include <event2/thread.h>

#include "transmission.h"

#include "log.h"
#include "session-thread.h"
#include "tr-assert.h"
#include "utils.h" // for tr_net_init()

using namespace std::literals;

///

namespace
{
namespace tr_evthread_init_helpers
{
void* lock_alloc(unsigned /*locktype*/)
{
    return new std::recursive_mutex{};
}

void lock_free(void* vlock, unsigned /*locktype*/)
{
    delete static_cast<std::recursive_mutex*>(vlock);
}

int lock_lock(unsigned mode, void* vlock)
{
    auto* lock = static_cast<std::recursive_mutex*>(vlock);
    if ((mode & EVTHREAD_TRY) != 0U)
    {
        auto const success = lock->try_lock();
        return success ? 0 : -1;
    }
    lock->lock();
    return 0;
}

int lock_unlock(unsigned /*mode*/, void* vlock)
{
    static_cast<std::recursive_mutex*>(vlock)->unlock();
    return 0;
}

void* cond_alloc(unsigned /*condflags*/)
{
    return new std::condition_variable_any();
}

void cond_free(void* vcond)
{
    delete static_cast<std::condition_variable_any*>(vcond);
}

int cond_signal(void* vcond, int broadcast)
{
    auto* cond = static_cast<std::condition_variable_any*>(vcond);
    if (broadcast != 0)
    {
        cond->notify_all();
    }
    else
    {
        cond->notify_one();
    }
    return 0;
}

int cond_wait(void* vcond, void* vlock, struct timeval const* tv)
{
    auto* cond = static_cast<std::condition_variable_any*>(vcond);
    auto* lock = static_cast<std::recursive_mutex*>(vlock);
    if (tv == nullptr)
    {
        cond->wait(*lock);
        return 0;
    }

    auto const duration = std::chrono::seconds(tv->tv_sec) + std::chrono::microseconds(tv->tv_usec);
    auto const success = cond->wait_for(*lock, duration);
    return success == std::cv_status::timeout ? 1 : 0;
}

unsigned long thread_current_id()
{
    return std::hash<std::thread::id>()(std::this_thread::get_id());
}

auto evthread_flag = std::once_flag{};

void initEvthreadsOnce()
{
    tr_net_init();

    evthread_lock_callbacks constexpr LockCbs{
        EVTHREAD_LOCK_API_VERSION, EVTHREAD_LOCKTYPE_RECURSIVE, lock_alloc, lock_free, lock_lock, lock_unlock
    };
    evthread_set_lock_callbacks(&LockCbs);

    evthread_condition_callbacks constexpr CondCbs{ EVTHREAD_CONDITION_API_VERSION,
                                                    cond_alloc,
                                                    cond_free,
                                                    cond_signal,
                                                    cond_wait };
    evthread_set_condition_callbacks(&CondCbs);

    evthread_set_id_callback(thread_current_id);
}

} // namespace tr_evthread_init_helpers

auto makeEventBase()
{
    tr_session_thread::tr_evthread_init();

    return std::unique_ptr<event_base, void (*)(event_base*)>{ event_base_new(), event_base_free };
}

} // namespace

///

void tr_session_thread::tr_evthread_init()
{
    using namespace tr_evthread_init_helpers;
    std::call_once(evthread_flag, initEvthreadsOnce);
}

class tr_session_thread_impl final : public tr_session_thread
{
public:
    explicit tr_session_thread_impl()
        : evbase_{ makeEventBase() }
    {
        auto lock = std::unique_lock(work_queue_mutex_);
        thread_ = std::thread(&tr_session_thread_impl::sessionThreadFunc, this, eventBase());
        work_queue_cv_.wait(lock, [this] { return thread_id_; });
    }

    tr_session_thread_impl(tr_session_thread_impl&&) = delete;
    tr_session_thread_impl(tr_session_thread_impl const&) = delete;
    tr_session_thread_impl& operator=(tr_session_thread_impl&&) = delete;
    tr_session_thread_impl& operator=(tr_session_thread_impl const&) = delete;

    ~tr_session_thread_impl() override
    {
        auto lock = std::unique_lock(work_queue_mutex_);

        // Exit the first event loop. This is the steady-state loop that runs
        // continuously, even when there are no events. See: sessionThreadFunc()
        is_shutting_down_ = true;
        event_base_loopexit(eventBase(), nullptr);

        // Wait on the second event loop. This is the shutdown loop that exits
        // as soon as there are no events because it knows we're waiting on it.
        // Let's wait up to `Deadline` secs for that to happen because we want
        // to give pending tasks a chance to finish.
        auto const deadline = std::chrono::steady_clock::now() + Deadline;
        while (thread_id_ && (deadline > std::chrono::steady_clock::now()))
        {
            tr_wait_msec(20);
        }
        // The second event loop may have exited already, but let's make sure.
        event_base_loopexit(eventBase(), nullptr);

        // The thread closes right after the loop exits. Wait for it here.
        thread_.join();
    }

    [[nodiscard]] struct event_base* eventBase() noexcept override
    {
        return evbase_.get();
    }

    [[nodiscard]] bool amInSessionThread() const noexcept override
    {
        return thread_id_ && *thread_id_ == std::this_thread::get_id();
    }

    void run(std::function<void(void)>&& func) override
    {
        if (amInSessionThread())
        {
            func();
        }
        else
        {
            auto lock = std::unique_lock(work_queue_mutex_);
            work_queue_.emplace_back(std::move(func));
            lock.unlock();

            event_active(work_queue_event_, 0, {});
        }
    }

private:
    using callback = std::function<void(void)>;
    using work_queue_t = std::list<callback>;

    void sessionThreadFunc(struct event_base* evbase)
    {
#ifndef _WIN32
        /* Don't exit when writing on a broken socket */
        (void)signal(SIGPIPE, SIG_IGN);
#endif

        tr_evthread_init();

        // initialize the session struct's event fields
        work_queue_event_ = event_new(evbase, -1, 0, onWorkAvailableStatic, this);
        thread_id_ = std::this_thread::get_id();

        // tell the constructor that's waiting for us that the thread is ready
        work_queue_cv_.notify_one();

        // Start the first event loop. This is the steady-state loop that runs
        // continuously, even when there are no events. See: ~~tr_session_thread_impl()
        TR_ASSERT(!is_shutting_down_);
        event_base_loop(evbase, EVLOOP_NO_EXIT_ON_EMPTY);

        // Start the second event loop. This is the shutdown loop that exits
        // as soon as there are no events, since ~tr_session_thread_impl()
        // is waiting for us in another thread.
        TR_ASSERT(is_shutting_down_);
        event_base_loop(evbase, 0);

        // cleanup
        event_free(work_queue_event_);
        work_queue_event_ = nullptr;
        thread_id_.reset();
    }

    static void onWorkAvailableStatic(evutil_socket_t /*fd*/, short /*flags*/, void* vself)
    {
        static_cast<tr_session_thread_impl*>(vself)->onWorkAvailable();
    }
    void onWorkAvailable()
    {
        TR_ASSERT(amInSessionThread());

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

    std::unique_ptr<event_base, void (*)(event_base*)> const evbase_;

    work_queue_t work_queue_;
    std::condition_variable work_queue_cv_;
    std::mutex work_queue_mutex_;
    event* work_queue_event_ = nullptr;

    std::thread thread_;
    std::optional<std::thread::id> thread_id_;

    bool is_shutting_down_ = false;
    static constexpr std::chrono::seconds Deadline = 5s;
};

std::unique_ptr<tr_session_thread> tr_session_thread::create()
{
    return std::make_unique<tr_session_thread_impl>();
}
