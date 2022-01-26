// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

#include <csignal>

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <event2/dns.h>
#include <event2/event.h>
#include <event2/thread.h>

#include "transmission.h"

#include "log.h"
#include "net.h"
#include "platform.h"
#include "session.h"
#include "tr-assert.h"
#include "trevent.h"
#include "utils.h"

/***
****
***/

namespace
{
namespace impl
{
struct std_lock
{
    std::mutex mutex;
    std::unique_lock<std::mutex> lock = std::unique_lock<std::mutex>(mutex);
};

void* std_lock_alloc(unsigned /*locktype*/)
{
    return new std_lock{};
}

void std_lock_free(void* lock_, unsigned /*locktype*/)
{
    delete static_cast<std_lock*>(lock_);
}

int std_lock_lock(unsigned mode, void* lock_)
{
    try
    {
        auto* lock = static_cast<std_lock*>(lock_);
        if (mode & EVTHREAD_TRY)
        {
            auto const success = lock->lock.try_lock();
            return success ? 0 : -1;
        }
        lock->lock.lock();
        return 0;
    }
    catch (std::system_error const& /*e*/)
    {
        return -1;
    }
}

int std_lock_unlock(unsigned /*mode*/, void* lock_)
{
    try
    {
        auto* lock = static_cast<std_lock*>(lock_);
        lock->lock.unlock();
        return 0;
    }
    catch (std::system_error const& /*e*/)
    {
        return -1;
    }
}

void* std_cond_alloc(unsigned /*condflags*/)
{
    return new std::condition_variable();
}

void std_cond_free(void* cond_)
{
    delete static_cast<std::condition_variable*>(cond_);
}

int std_cond_signal(void* cond_, int broadcast)
{
    auto* cond = static_cast<std::condition_variable*>(cond_);
    if (broadcast)
    {
        cond->notify_all();
    }
    else
    {
        cond->notify_one();
    }
    return 0;
}

int std_cond_wait(void* cond_, void* lock_, struct timeval const* tv)
{
    auto* cond = static_cast<std::condition_variable*>(cond_);
    auto* lock = static_cast<std_lock*>(lock_);
    if (tv == nullptr)
    {
        cond->wait(lock->lock);
        return 0;
    }

    auto duration = std::chrono::seconds(tv->tv_sec) + std::chrono::microseconds(tv->tv_usec);
    auto const success = cond->wait_for(lock->lock, duration);
    return success == std::cv_status::timeout ? 1 : 0;
}

unsigned long std_get_thread_id()
{
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

} // namespace impl

void tr_evthread_init()
{
    auto constexpr cbs = evthread_lock_callbacks{
        1 /* EVTHREAD_LOCK_API_VERSION */,
        EVTHREAD_LOCKTYPE_RECURSIVE,
        impl::std_lock_alloc,
        impl::std_lock_free,
        impl::std_lock_lock,
        impl::std_lock_unlock,
    };
    evthread_set_lock_callbacks(&cbs);

    auto constexpr cond_cbs = evthread_condition_callbacks{ 1 /* EVTHREAD_CONDITION_API_VERSION */,
                                                            impl::std_cond_alloc,
                                                            impl::std_cond_free,
                                                            impl::std_cond_signal,
                                                            impl::std_cond_wait };
    evthread_set_condition_callbacks(&cond_cbs);

    evthread_set_id_callback(impl::std_get_thread_id);
}

} // namespace

/***
****
***/

struct tr_event_handle
{
    // would it be more expensive to use std::function here?
    struct callback
    {
        callback(void (*func)(void*) = nullptr, void* user_data = nullptr)
            : func_{ func }
            , user_data_{ user_data }
        {
        }

        void invoke() const
        {
            if (func_ != nullptr)
            {
                func_(user_data_);
            }
        }

        void (*func_)(void*);
        void* user_data_;
    };

    using work_queue_t = std::list<callback>;
    work_queue_t work_queue;
    std::mutex work_queue_mutex;
    event* work_queue_event = nullptr;

    event_base* base = nullptr;
    tr_session* session = nullptr;
    tr_thread* thread = nullptr;
};

static void onWorkAvailable(evutil_socket_t /*fd*/, short /*flags*/, void* vsession)
{
    // invariant
    auto* const session = static_cast<tr_session*>(vsession);
    TR_ASSERT(tr_amInEventThread(session));

    // steal the work queue
    auto* events = session->events;
    auto work_queue_lock = std::unique_lock(events->work_queue_mutex);
    auto work_queue = tr_event_handle::work_queue_t{};
    std::swap(work_queue, events->work_queue);
    work_queue_lock.unlock();

    // process the work queue
    for (auto const& work : work_queue)
    {
        work.invoke();
    }
}

static void libeventThreadFunc(void* vevents)
{
    auto* const events = static_cast<tr_event_handle*>(vevents);

#ifndef _WIN32
    /* Don't exit when writing on a broken socket */
    signal(SIGPIPE, SIG_IGN);
#endif

    tr_evthread_init();

    // create the libevent base
    auto* const base = event_base_new();

    // initialize the session struct's event fields
    events->base = base;
    events->work_queue_event = evuser_new(base, onWorkAvailable, events->session);
    events->session->event_base = base;
    events->session->evdns_base = evdns_base_new(base, true);
    events->session->events = events;

    event_base_dispatch(base);

    // shut down the thread
    event_base_free(base);
    events->session->event_base = nullptr;
    events->session->evdns_base = nullptr;
    events->session->events = nullptr;
    delete events;
    tr_logAddDebug("Closing libevent thread");
}

void tr_eventInit(tr_session* session)
{
    session->events = nullptr;

    auto* const events = new tr_event_handle();
    events->session = session;
    events->thread = tr_threadNew(libeventThreadFunc, events);

    // wait until the libevent thread is running
    while (session->events == nullptr)
    {
        tr_wait_msec(100);
    }
}

void tr_eventClose(tr_session* session)
{
    TR_ASSERT(tr_isSession(session));

    auto* events = session->events;
    if (events == nullptr)
    {
        return;
    }

    event_base_loopexit(events->base, nullptr);

    if (tr_logGetDeepEnabled())
    {
        tr_logAddDeep(__FILE__, __LINE__, nullptr, "closing trevent pipe");
    }
}

/**
***
**/

bool tr_amInEventThread(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(session->events != nullptr);

    return tr_amInThread(session->events->thread);
}

/**
***
**/

void tr_runInEventThread(tr_session* session, void (*func)(void*), void* user_data)
{
    TR_ASSERT(tr_isSession(session));
    auto* events = session->events;
    TR_ASSERT(events != nullptr);

    if (tr_amInThread(events->thread))
    {
        (*func)(user_data);
    }
    else
    {
        auto const lock = std::scoped_lock(events->work_queue_mutex);
        events->work_queue.emplace_back(func, user_data);
        event_active(events->work_queue_event, 0, {});
    }
}
