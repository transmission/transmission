// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <condition_variable>
#include <list>
#include <mutex>

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

#ifdef _WIN32
    evthread_use_windows_threads();
#else
    signal(SIGPIPE, SIG_IGN); // don't exit when writing on a broken socket
    evthread_use_pthreads();
#endif

    // create the libevent base
    auto* const base = event_base_new();

    // initialize the session struct's event fields
    events->base = base;
    events->work_queue_event = event_new(base, -1, 0, onWorkAvailable, events->session);
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
        auto lock = std::unique_lock(events->work_queue_mutex);
        events->work_queue.emplace_back(func, user_data);
        lock.unlock();

        event_active(events->work_queue_event, 0, {});
    }
}
