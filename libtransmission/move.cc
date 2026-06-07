// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstddef> // for std::size_t
#include <exception>
#include <iterator> // for std::empty()
#include <memory>
#include <mutex>
#include <thread>
#include <utility> // for std::move()

#include <fmt/format.h>

#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/move.h"

using namespace std::chrono_literals;

namespace
{
// module name attached to every log line emitted from the move worker, so that
// these can be filtered with `transmission-* --log-level=debug` and grepped for
auto constexpr LogName = "move";
} // namespace

void tr_move_worker::move_thread_func()
{
    tr_logAddInfo("Background move worker thread started", LogName);

    try
    {
        for (;;)
        {
            auto queue_depth = std::size_t{};

            {
                auto const lock = std::scoped_lock{ move_mutex_ };

                if (std::empty(todo_))
                {
                    // Reset the id atomically with the empty-queue check: a
                    // concurrent add() either sees the id still set (and lets
                    // this thread pick up its job) or sees it cleared (and
                    // spawns a fresh thread). Splitting these would strand a
                    // queued job with no thread to run it.
                    current_.reset();
                    move_thread_id_.reset();
                    tr_logAddDebug("Move queue drained; worker thread exiting normally", LogName);
                    return;
                }

                current_ = std::move(todo_.front());
                todo_.pop_front();
                queue_depth = std::size(todo_);
            }

            tr_logAddInfo(fmt::format("Starting a move job; {:d} more still queued", queue_depth), LogName);

            // Run the blocking relocation outside the lock so that add()/remove()
            // stay responsive while a (potentially long) copy is in progress.
            //
            // Guard the whole job: a throw escaping here would otherwise call
            // std::terminate() (killing the daemon) or leave move_thread_id_ set
            // with no running thread, silently wedging every future relocation.
            try
            {
                current_->on_move_started();

                auto error = tr_error{};
                auto const ok = current_->move(&error);

                if (ok)
                {
                    tr_logAddInfo("Move job completed successfully", LogName);
                }
                else
                {
                    tr_logAddError(
                        fmt::format(
                            "Move job failed: {:s} ({:d})",
                            error ? error.message() : "unknown error",
                            error ? error.code() : 0),
                        LogName);
                }

                current_->on_move_done(ok, error ? &error : nullptr);
            }
            catch (std::exception const& e)
            {
                tr_logAddError(fmt::format("Move job threw an exception and was abandoned: {:s}", e.what()), LogName);
            }
            catch (...)
            {
                tr_logAddError("Move job threw an unknown exception and was abandoned", LogName);
            }

            {
                auto const lock = std::scoped_lock{ move_mutex_ };
                current_.reset();
                done_current_cv_.notify_all();
            }
        }
    }
    catch (...)
    {
        // Last-resort guard: an exception escaped even the per-job handlers
        // (e.g. an allocation failure while touching the queue itself). Fall
        // through to the cleanup below rather than terminating the process.
        tr_logAddError("Background move worker thread aborting after an unexpected error", LogName);
    }

    // Reached only on the emergency path above. Make sure the worker can be
    // restarted by the next add() and that any remove() waiter is released.
    auto const lock = std::scoped_lock{ move_mutex_ };
    current_.reset();
    move_thread_id_.reset();
    done_current_cv_.notify_all();
    if (!std::empty(todo_))
    {
        tr_logAddWarn(
            fmt::format("Move worker exited abnormally with {:d} job(s) still queued; they will run after the next move is added", std::size(todo_)),
            LogName);
    }
}

void tr_move_worker::add(std::unique_ptr<Mediator> mediator)
{
    auto const lock = std::scoped_lock{ move_mutex_ };

    mediator->on_move_queued();
    todo_.emplace_back(std::move(mediator));

    if (!move_thread_id_)
    {
        tr_logAddDebug("No move worker running; spawning one", LogName);
        auto thread = std::thread(&tr_move_worker::move_thread_func, this);
        move_thread_id_ = thread.get_id();
        thread.detach();
    }
    else
    {
        tr_logAddDebug(fmt::format("Move worker already running; {:d} job(s) now queued", std::size(todo_)), LogName);
    }
}

void tr_move_worker::remove(tr_sha1_digest_t const& info_hash)
{
    auto lock = std::unique_lock{ move_mutex_ };

    // a move in progress can't be interrupted safely, so wait for it to finish
    if (current_ && current_->info_hash() == info_hash)
    {
        tr_logAddInfo("Waiting for an in-progress move to finish before removing the torrent", LogName);
    }
    done_current_cv_.wait(lock, [this, &info_hash]() { return !current_ || current_->info_hash() != info_hash; });

    // drop any not-yet-started job for this torrent
    auto const before = std::size(todo_);
    todo_.remove_if([&info_hash](auto const& mediator) { return mediator->info_hash() == info_hash; });
    if (auto const dropped = before - std::size(todo_); dropped != 0U)
    {
        tr_logAddDebug(fmt::format("Dropped {:d} queued move job(s) for the removed torrent", dropped), LogName);
    }
}

tr_move_worker::~tr_move_worker()
{
    {
        auto const lock = std::scoped_lock{ move_mutex_ };
        if (!std::empty(todo_))
        {
            tr_logAddWarn(fmt::format("Discarding {:d} queued move job(s) during shutdown", std::size(todo_)), LogName);
        }
        todo_.clear();
    }

    // wait for any in-progress move to drain and the worker thread to exit.
    // Read move_thread_id_ under the lock: the worker mutates it under the lock,
    // so an unlocked read here would be a data race.
    for (auto announced = false;;)
    {
        {
            auto const lock = std::scoped_lock{ move_mutex_ };
            if (!move_thread_id_.has_value())
            {
                break;
            }

            if (!announced)
            {
                tr_logAddInfo("Waiting for the in-progress move to finish before shutting down", LogName);
                announced = true;
            }
        }

        std::this_thread::sleep_for(20ms);
    }

    tr_logAddDebug("Background move worker shut down", LogName);
}
