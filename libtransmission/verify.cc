// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <ctime>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include <fmt/core.h>

#include "transmission.h"

#include "completion.h"
#include "crypto-utils.h"
#include "file.h"
#include "log.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h" // tr_time(), tr_wait()
#include "verify.h"

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
    if (pa != pb)
    {
        return pa > pb ? -1 : 1;
    }

    // smaller torrents come before larger ones because they verify faster
    if (current_size != that.current_size)
    {
        return current_size < that.current_size ? -1 : 1;
    }

    // tertiary compare just to ensure they don't compare equal
    if (torrent->id() != that.torrent->id())
    {
        return torrent->id() < that.torrent->id() ? -1 : 1;
    }

    return 0;
}

bool tr_verify_worker::verifyTorrent(tr_torrent* tor, std::atomic<bool> const& stop_flag)
{
    auto const begin = tr_time();

    tr_sys_file_t fd = TR_BAD_SYS_FILE;
    uint64_t file_pos = 0;
    bool changed = false;
    bool had_piece = false;
    time_t last_slept_at = 0;
    uint32_t piece_pos = 0;
    tr_file_index_t file_index = 0;
    tr_file_index_t prev_file_index = ~file_index;
    tr_piece_index_t piece = 0;
    auto buffer = std::vector<std::byte>(1024 * 256);
    auto sha = tr_sha1::create();

    tr_logAddDebugTor(tor, "verifying torrent...");

    while (!stop_flag && piece < tor->pieceCount())
    {
        auto const file_length = tor->fileSize(file_index);

        /* if we're starting a new piece... */
        if (piece_pos == 0)
        {
            had_piece = tor->hasPiece(piece);
        }

        /* if we're starting a new file... */
        if (file_pos == 0 && fd == TR_BAD_SYS_FILE && file_index != prev_file_index)
        {
            auto const found = tor->findFile(file_index);
            fd = !found ? TR_BAD_SYS_FILE : tr_sys_file_open(found->filename(), TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0);
            prev_file_index = file_index;
        }

        /* figure out how much we can read this pass */
        uint64_t left_in_piece = tor->pieceSize(piece) - piece_pos;
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
            if (auto const has_piece = sha->finish() == tor->pieceHash(piece); has_piece || had_piece)
            {
                tor->setHasPiece(piece, has_piece);
                changed |= has_piece != had_piece;
            }

            tor->checked_pieces_.set(piece, true);
            tor->markChanged();

            /* sleeping even just a few msec per second goes a long
             * way towards reducing IO load... */
            if (auto const now = tr_time(); last_slept_at != now)
            {
                last_slept_at = now;
                tr_wait(SleepPerSecondDuringVerify);
            }

            sha->clear();
            ++piece;
            tor->setVerifyProgress(piece / float(tor->pieceCount()));
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
    tr_logAddDebugTor(
        tor,
        fmt::format(
            "Verification is done. It took {} seconds to verify {} bytes ({} bytes per second)",
            end - begin,
            tor->totalSize(),
            tor->totalSize() / (1 + (end - begin))));

    return changed;
}

void tr_verify_worker::verifyThreadFunc()
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
        tor->setVerifyState(TR_VERIFY_NOW);
        auto const changed = verifyTorrent(tor, stop_current_);
        tor->setVerifyState(TR_VERIFY_NONE);
        TR_ASSERT(tr_isTorrent(tor));

        if (!stop_current_ && changed)
        {
            tor->setDirty();
        }

        callCallback(tor, stop_current_);
    }
}

void tr_verify_worker::add(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    tr_logAddTraceTor(tor, "Queued for verification");

    auto node = Node{};
    node.torrent = tor;
    node.current_size = tor->hasTotal();

    auto const lock = std::lock_guard(verify_mutex_);
    tor->setVerifyState(TR_VERIFY_WAIT);
    todo_.insert(node);

    if (!verify_thread_id_)
    {
        auto thread = std::thread(&tr_verify_worker::verifyThreadFunc, this);
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

        tor->setVerifyState(TR_VERIFY_NONE);

        if (iter != std::end(todo_))
        {
            callCallback(tor, true);
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
        tr_wait(20ms);
    }
}
