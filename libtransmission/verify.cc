// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

#include "libtransmission/verify.h"

using namespace std::chrono_literals;

namespace
{
auto constexpr SleepPerSecondDuringVerify = 100ms;

[[nodiscard]] auto current_time_secs()
{
    return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::steady_clock::now());
}
} // namespace

void tr_verify_worker::verify_torrent(Mediator& verify_mediator, bool const abort_flag)
{
    verify_mediator.on_verify_started();

    auto last_slept_at = current_time_secs();
    for (tr_piece_index_t piece = 0U, n = verify_mediator.metainfo().piece_count(); !abort_flag && piece < n; ++piece)
    {
        verify_mediator.on_piece_checked(piece, verify_mediator.check_piece(piece));

        /* sleeping even just a few msec per second goes a long
         * way towards reducing IO load... */
        if (auto const now = current_time_secs(); last_slept_at != now)
        {
            last_slept_at = now;
            std::this_thread::sleep_for(SleepPerSecondDuringVerify);
        }
    }

    verify_mediator.on_verify_done(abort_flag);
}

void tr_verify_worker::verify_thread_func()
{
    for (;;)
    {
        {
            auto const lock = std::lock_guard{ verify_mutex_ };

            if (stop_current_)
            {
                stop_current_ = false;
                stop_current_cv_.notify_one();
            }

            if (std::empty(todo_))
            {
                current_node_.reset();
                verify_thread_id_.reset();
                return;
            }

            current_node_ = std::move(todo_.extract(std::begin(todo_)).value());
        }

        verify_torrent(*current_node_->mediator_, stop_current_);
    }
}

void tr_verify_worker::add(std::unique_ptr<Mediator> mediator, tr_priority_t priority)
{
    auto const lock = std::lock_guard{ verify_mutex_ };

    mediator->on_verify_queued();
    todo_.emplace(std::move(mediator), priority);

    if (!verify_thread_id_)
    {
        auto thread = std::thread(&tr_verify_worker::verify_thread_func, this);
        verify_thread_id_ = thread.get_id();
        thread.detach();
    }
}

void tr_verify_worker::remove(tr_sha1_digest_t const& info_hash)
{
    auto lock = std::unique_lock(verify_mutex_);

    if (current_node_ && current_node_->matches(info_hash))
    {
        stop_current_ = true;
        stop_current_cv_.wait(lock, [this]() { return !stop_current_; });
    }
    else if (auto const iter = std::find_if(
                 std::begin(todo_),
                 std::end(todo_),
                 [&info_hash](auto const& node) { return node.matches(info_hash); });
             iter != std::end(todo_))
    {
        iter->mediator_->on_verify_done(true /*aborted*/);
        todo_.erase(iter);
    }
}

tr_verify_worker::~tr_verify_worker()
{
    {
        auto const lock = std::lock_guard{ verify_mutex_ };
        stop_current_ = true;
        todo_.clear();
    }

    while (verify_thread_id_.has_value())
    {
        std::this_thread::sleep_for(20ms);
    }
}

int tr_verify_worker::Node::compare(Node const& that) const noexcept
{
    // prefer higher-priority torrents
    if (priority_ != that.priority_)
    {
        return priority_ > that.priority_ ? -1 : 1;
    }

    // prefer smaller torrents, since they will verify faster
    auto const& metainfo = mediator_->metainfo();
    auto const& that_metainfo = that.mediator_->metainfo();
    if (metainfo.total_size() != that_metainfo.total_size())
    {
        return metainfo.total_size() < that_metainfo.total_size() ? -1 : 1;
    }

    // uniqueness check
    auto const& this_hash = metainfo.info_hash();
    auto const& that_hash = that_metainfo.info_hash();
    if (this_hash != that_hash)
    {
        return this_hash < that_hash ? -1 : 1;
    }

    return 0;
}
