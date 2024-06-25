// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef> // std::byte
#include <cstdint> // uint64_t, uint32_t
#include <memory>
#include <mutex>
#include <thread>
#include <utility> // for std::move()
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/crypto-utils.h"
#include "libtransmission/file.h"
#include "libtransmission/tr-macros.h"
#include "libtransmission/verify.h"

using namespace std::chrono_literals;

namespace
{
[[nodiscard]] auto current_time_secs()
{
    return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::steady_clock::now());
}
} // namespace

void tr_verify_worker::verify_torrent(
    Mediator& verify_mediator,
    std::atomic<bool> const& abort_flag,
    std::chrono::milliseconds const sleep_per_seconds_during_verify)
{
    verify_mediator.on_verify_started();

    tr_sys_file_t fd = TR_BAD_SYS_FILE;
    uint64_t file_pos = 0U;
    uint32_t piece_pos = 0U;
    tr_file_index_t file_index = 0U;
    tr_file_index_t prev_file_index = ~file_index;
    tr_piece_index_t piece = 0U;
    auto buffer = std::vector<std::byte>(1024U * 256U);
    auto sha = tr_sha1{};
    auto last_slept_at = current_time_secs();

    auto const& metainfo = verify_mediator.metainfo();
    while (!abort_flag && piece < metainfo.piece_count())
    {
        auto const file_length = metainfo.file_size(file_index);

        /* if we're starting a new file... */
        if (file_pos == 0U && fd == TR_BAD_SYS_FILE && file_index != prev_file_index)
        {
            auto const found = verify_mediator.find_file(file_index);
            fd = !found ? TR_BAD_SYS_FILE : tr_sys_file_open(found->c_str(), TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0);
            prev_file_index = file_index;
        }

        /* figure out how much we can read this pass */
        uint64_t left_in_piece = metainfo.piece_size(piece) - piece_pos;
        uint64_t left_in_file = file_length - file_pos;
        uint64_t bytes_this_pass = std::min(left_in_file, left_in_piece);
        bytes_this_pass = std::min(bytes_this_pass, uint64_t(std::size(buffer)));

        /* read a bit */
        if (fd != TR_BAD_SYS_FILE)
        {
            auto num_read = uint64_t{};
            if (tr_sys_file_read_at(fd, std::data(buffer), bytes_this_pass, file_pos, &num_read) && num_read > 0U)
            {
                bytes_this_pass = num_read;
                sha.add(std::data(buffer), bytes_this_pass);
            }
        }

        /* move our offsets */
        left_in_piece -= bytes_this_pass;
        left_in_file -= bytes_this_pass;
        piece_pos += bytes_this_pass;
        file_pos += bytes_this_pass;

        /* if we're finishing a piece... */
        if (left_in_piece == 0U)
        {
            auto const has_piece = sha.finish() == metainfo.piece_hash(piece);
            verify_mediator.on_piece_checked(piece, has_piece);

            if (sleep_per_seconds_during_verify > std::chrono::milliseconds::zero())
            {
                /* sleeping even just a few msec per second goes a long
                 * way towards reducing IO load... */
                if (auto const now = current_time_secs(); last_slept_at != now)
                {
                    last_slept_at = now;
                    std::this_thread::sleep_for(sleep_per_seconds_during_verify);
                }
            }

            sha.clear();
            ++piece;
            piece_pos = 0U;
        }

        /* if we're finishing a file... */
        if (left_in_file == 0U)
        {
            if (fd != TR_BAD_SYS_FILE)
            {
                tr_sys_file_close(fd);
                fd = TR_BAD_SYS_FILE;
            }

            ++file_index;
            file_pos = 0U;
        }
    }

    /* cleanup */
    if (fd != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(fd);
    }

    verify_mediator.on_verify_done(abort_flag);
}

void tr_verify_worker::verify_thread_func()
{
    for (;;)
    {
        {
            auto const lock = std::scoped_lock{ verify_mutex_ };

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

        verify_torrent(*current_node_->mediator_, stop_current_, sleep_per_seconds_during_verify_);
    }
}

void tr_verify_worker::add(std::unique_ptr<Mediator> mediator, tr_priority_t priority)
{
    auto const lock = std::scoped_lock{ verify_mutex_ };

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
        auto const lock = std::scoped_lock{ verify_mutex_ };
        stop_current_ = true;
        todo_.clear();
    }

    while (verify_thread_id_.has_value())
    {
        std::this_thread::sleep_for(20ms);
    }
}

void tr_verify_worker::set_sleep_per_seconds_during_verify(std::chrono::milliseconds const sleep_per_seconds_during_verify)
{
    sleep_per_seconds_during_verify_ = sleep_per_seconds_during_verify;
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
