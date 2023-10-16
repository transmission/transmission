// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <ctime>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <fmt/core.h>

#include "libtransmission/transmission.h"

#include "libtransmission/completion.h"
#include "libtransmission/crypto-utils.h"
#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/torrent.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/verify.h"

using namespace std::chrono_literals;

namespace
{

auto constexpr SleepPerSecondDuringVerify = 100ms;

}

int tr_verify_worker::Node::compare(tr_verify_worker::Node const& that) const
{
    // higher priority comes before lower priority
    auto const pa = tr_torrentGetPriority(torrent);
    auto const pb = tr_torrentGetPriority(that.torrent);
    if (auto const val = tr_compare_3way(pa, pb); val != 0)
    {
        return -val;
    }

    // smaller torrents come before larger ones because they verify faster
    if (auto const val = tr_compare_3way(current_size, that.current_size); val != 0)
    {
        return val;
    }

    // tertiary compare just to ensure they don't compare equal
    return tr_compare_3way(torrent->id(), that.torrent->id());
}

void tr_verify_worker::verify_torrent(VerifyMediator& verify_mediator, std::atomic<bool> const& abort_flag)
{
    auto const& metainfo = verify_mediator.metainfo();
    auto const begin = verify_mediator.current_time();
    verify_mediator.on_verify_started();

    tr_sys_file_t fd = TR_BAD_SYS_FILE;
    uint64_t file_pos = 0;
    time_t last_slept_at = 0;
    uint32_t piece_pos = 0;
    tr_file_index_t file_index = 0;
    tr_file_index_t prev_file_index = ~file_index;
    tr_piece_index_t piece = 0;
    auto buffer = std::vector<std::byte>(1024 * 256);
    auto sha = tr_sha1::create();

    tr_logAddDebugMetainfo(metainfo, "verifying torrent...");

    while (!abort_flag && piece < metainfo.piece_count())
    {
        auto const file_length = metainfo.file_size(file_index);

        /* if we're starting a new file... */
        if (file_pos == 0 && fd == TR_BAD_SYS_FILE && file_index != prev_file_index)
        {
            auto const found = verify_mediator.find_file(file_index);
            fd = !found ? TR_BAD_SYS_FILE : tr_sys_file_open(found->filename(), TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0);
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
            if (tr_sys_file_read_at(fd, std::data(buffer), bytes_this_pass, file_pos, &num_read) && num_read > 0)
            {
                bytes_this_pass = num_read;
                sha->add(std::data(buffer), bytes_this_pass);
                tr_sys_file_advise(fd, file_pos, bytes_this_pass, TR_SYS_FILE_ADVICE_DONT_NEED);
            }
        }

        /* move our offsets */
        left_in_piece -= bytes_this_pass;
        left_in_file -= bytes_this_pass;
        piece_pos += bytes_this_pass;
        file_pos += bytes_this_pass;

        /* if we're finishing a piece... */
        if (left_in_piece == 0)
        {
            auto const has_piece = sha->finish() == metainfo.piece_hash(piece);
            verify_mediator.on_piece_checked(piece, has_piece);

            /* sleeping even just a few msec per second goes a long
             * way towards reducing IO load... */
            if (auto const now = verify_mediator.current_time(); last_slept_at != now)
            {
                last_slept_at = now;
                std::this_thread::sleep_for(SleepPerSecondDuringVerify);
            }

            sha->clear();
            ++piece;
            piece_pos = 0;
        }

        /* if we're finishing a file... */
        if (left_in_file == 0)
        {
            if (fd != TR_BAD_SYS_FILE)
            {
                tr_sys_file_close(fd);
                fd = TR_BAD_SYS_FILE;
            }

            ++file_index;
            file_pos = 0;
        }
    }

    /* cleanup */
    if (fd != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(fd);
    }

    /* stopwatch */
    time_t const end = tr_time();
    tr_logAddDebugMetainfo(
        metainfo,
        fmt::format(
            "Verification is done. It took {} seconds to verify {} bytes ({} bytes per second)",
            end - begin,
            metainfo.total_size(),
            metainfo.total_size() / (1 + (end - begin))));

    verify_mediator.on_verify_done(abort_flag);
}

void tr_verify_worker::verify_thread_func()
{
    for (;;)
    {
        {
            auto const lock = std::lock_guard(verify_mutex_);

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

            auto const it = std::begin(todo_);
            current_node_ = *it;
            todo_.erase(it);
        }

        auto* const tor = current_node_->torrent;
        tr_logAddTraceTor(tor, "Verifying torrent");
        auto verify_mediator = tr_torrent::VerifyMediator{ tor };
        verify_torrent(verify_mediator, stop_current_);
    }
}

void tr_verify_worker::add(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    tr_logAddTraceTor(tor, "Queued for verification");

    auto node = Node{};
    node.torrent = tor;
    node.current_size = tor->has_total();

    auto const lock = std::lock_guard(verify_mutex_);
    tor->set_verify_state(TR_VERIFY_WAIT);
    todo_.insert(node);

    if (!verify_thread_id_)
    {
        auto thread = std::thread(&tr_verify_worker::verify_thread_func, this);
        verify_thread_id_ = thread.get_id();
        thread.detach();
    }
}

void tr_verify_worker::remove(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    auto lock = std::unique_lock(verify_mutex_);

    if (current_node_ && current_node_->torrent == tor)
    {
        stop_current_ = true;
        stop_current_cv_.wait(lock, [this]() { return !stop_current_; });
    }
    else
    {
        auto const iter = std::find_if(
            std::begin(todo_),
            std::end(todo_),
            [tor](auto const& task) { return tor == task.torrent; });

        tor->set_verify_state(TR_VERIFY_NONE);

        if (iter != std::end(todo_))
        {
            tr_torrent::VerifyMediator{ tor }.on_verify_done(true);
            todo_.erase(iter);
        }
    }
}

tr_verify_worker::~tr_verify_worker()
{
    {
        auto const lock = std::lock_guard(verify_mutex_);
        stop_current_ = true;
        todo_.clear();
    }

    while (verify_thread_id_.has_value())
    {
        std::this_thread::sleep_for(20ms);
    }
}
