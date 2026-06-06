// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <iterator> // for std::empty()
#include <memory>
#include <mutex>
#include <thread>
#include <utility> // for std::move()

#include "libtransmission/error.h"
#include "libtransmission/move.h"

using namespace std::chrono_literals;

void tr_move_worker::move_thread_func()
{
    for (;;)
    {
        {
            auto const lock = std::scoped_lock{ move_mutex_ };

            if (std::empty(todo_))
            {
                current_.reset();
                move_thread_id_.reset();
                return;
            }

            current_ = std::move(todo_.front());
            todo_.pop_front();
        }

        // run the blocking relocation outside the lock so that add()/remove()
        // stay responsive while a (potentially long) copy is in progress
        current_->on_move_started();
        auto error = tr_error{};
        auto const ok = current_->move(&error);
        current_->on_move_done(ok, error ? &error : nullptr);

        {
            auto const lock = std::scoped_lock{ move_mutex_ };
            current_.reset();
            done_current_cv_.notify_all();
        }
    }
}

void tr_move_worker::add(std::unique_ptr<Mediator> mediator)
{
    auto const lock = std::scoped_lock{ move_mutex_ };

    mediator->on_move_queued();
    todo_.emplace_back(std::move(mediator));

    if (!move_thread_id_)
    {
        auto thread = std::thread(&tr_move_worker::move_thread_func, this);
        move_thread_id_ = thread.get_id();
        thread.detach();
    }
}

void tr_move_worker::remove(tr_sha1_digest_t const& info_hash)
{
    auto lock = std::unique_lock{ move_mutex_ };

    // a move in progress can't be interrupted safely, so wait for it to finish
    done_current_cv_.wait(lock, [this, &info_hash]() { return !current_ || current_->info_hash() != info_hash; });

    // drop any not-yet-started job for this torrent
    todo_.remove_if([&info_hash](auto const& mediator) { return mediator->info_hash() == info_hash; });
}

tr_move_worker::~tr_move_worker()
{
    {
        auto const lock = std::scoped_lock{ move_mutex_ };
        todo_.clear();
    }

    // wait for any in-progress move to drain and the worker thread to exit
    while (move_thread_id_.has_value())
    {
        std::this_thread::sleep_for(20ms);
    }
}
