// This file Copyright © 2007-2022 Mnemosyne LLC.
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
#include "utils.h" // tr_time(), tr_wait_msec()
#include "verify.h"

namespace
{

auto constexpr MsecToSleepPerSecondDuringVerify = int{ 100 };

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

bool tr_verify_worker::verifyTorrent(tr_torrent* tor, bool const* stop_flag)
{
    fmt::print(stderr, FMT_STRING("{:s}:{:d} worker thread {:s}\n"), __FILE__, __LINE__, tor->name());
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

    fmt::print(stderr, FMT_STRING("{:s}:{:d} starting the loop\n"), __FILE__, __LINE__);
    while (!*stop_flag && piece < tor->pieceCount())
    {
        fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
        auto const file_length = tor->fileSize(file_index);

        /* if we're starting a new piece... */
        if (piece_pos == 0)
        {
            fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
            had_piece = tor->hasPiece(piece);
        }

        /* if we're starting a new file... */
        fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
        if (file_pos == 0 && fd == TR_BAD_SYS_FILE && file_index != prev_file_index)
        {
            fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
            auto const found = tor->findFile(file_index);
            fd = !found ? TR_BAD_SYS_FILE : tr_sys_file_open(found->filename(), TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0);
            prev_file_index = file_index;
        }

        /* figure out how much we can read this pass */
        fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
        uint64_t left_in_piece = tor->pieceSize(piece) - piece_pos;
        uint64_t left_in_file = file_length - file_pos;
        uint64_t bytes_this_pass = std::min(left_in_file, left_in_piece);
        bytes_this_pass = std::min(bytes_this_pass, uint64_t(std::size(buffer)));

        /* read a bit */
        fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
        if (fd != TR_BAD_SYS_FILE)
        {
            fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
            auto num_read = uint64_t{};
            if (tr_sys_file_read_at(fd, std::data(buffer), bytes_this_pass, file_pos, &num_read) && num_read > 0)
            {
                fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
                bytes_this_pass = num_read;
                sha->add(std::data(buffer), bytes_this_pass);
                tr_sys_file_advise(fd, file_pos, bytes_this_pass, TR_SYS_FILE_ADVICE_DONT_NEED);
            }
        }

        /* move our offsets */
        fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
        left_in_piece -= bytes_this_pass;
        left_in_file -= bytes_this_pass;
        piece_pos += bytes_this_pass;
        file_pos += bytes_this_pass;

        /* if we're finishing a piece... */
        fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
        if (left_in_piece == 0)
        {
            fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
            auto const has_piece = sha->finish() == tor->pieceHash(piece);

            if (has_piece || had_piece)
            {
                tor->setHasPiece(piece, has_piece);
                changed |= has_piece != had_piece;
            }

            fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
            tor->checked_pieces_.set(piece, true);
            tor->markChanged();

            /* sleeping even just a few msec per second goes a long
             * way towards reducing IO load... */
            fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
            if (auto const now = tr_time(); last_slept_at != now)
            {
                fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
                last_slept_at = now;
                tr_wait_msec(MsecToSleepPerSecondDuringVerify);
            }

            fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
            sha->clear();
            ++piece;
            tor->setVerifyProgress(piece / float(tor->pieceCount()));
            piece_pos = 0;
        }

        /* if we're finishing a file... */
        fmt::print(stderr, FMT_STRING("{:s}:{:d} worker thread done verifying {:s}\n"), __FILE__, __LINE__, tor->name());
        if (left_in_file == 0)
        {
            fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
            if (fd != TR_BAD_SYS_FILE)
            {
                fmt::print(stderr, FMT_STRING("{:s}:{:d} closing file\n"), __FILE__, __LINE__);
                tr_sys_file_close(fd);
                fd = TR_BAD_SYS_FILE;
            }

            ++file_index;
            file_pos = 0;
        }
    }

    /* cleanup */
    fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
    if (fd != TR_BAD_SYS_FILE)
    {
        fmt::print(stderr, FMT_STRING("{:s}:{:d} cleanup\n"), __FILE__, __LINE__);
        tr_sys_file_close(fd);
    }

    /* stopwatch */
    fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
    time_t const end = tr_time();
    tr_logAddDebugTor(
        tor,
        fmt::format(
            "Verification is done. It took {} seconds to verify {} bytes ({} bytes per second)",
            end - begin,
            tor->totalSize(),
            tor->totalSize() / (1 + (end - begin))));

    fmt::print(stderr, FMT_STRING("{:s}:{:d} verify() exiting\n"), __FILE__, __LINE__);
    return changed;
}

void tr_verify_worker::verifyThreadFunc()
{
    fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
    for (;;)
    {
        {
            fmt::print(stderr, FMT_STRING("{:s}:{:d} worker thread getting lock\n"), __FILE__, __LINE__);
            auto const lock = std::lock_guard(verify_mutex_);
            fmt::print(stderr, FMT_STRING("{:s}:{:d} worker thread getting next todo\n"), __FILE__, __LINE__);

            stop_current_ = false;
            if (std::empty(todo_))
            {
                fmt::print(stderr, FMT_STRING("{:s}:{:d} worker thread empty todo; exiting\n"), __FILE__, __LINE__);
                current_node_.reset();
                verify_thread_id_.reset();
                return;
            }

            fmt::print(stderr, FMT_STRING("{:s}:{:d} worker thread found a todo item\n"), __FILE__, __LINE__);
            auto const it = std::begin(todo_);
            current_node_ = *it;
            todo_.erase(it);
        }

        fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
        auto* const tor = current_node_->torrent;
        tr_logAddTraceTor(tor, "Verifying torrent");
        tor->setVerifyState(TR_VERIFY_NOW);
        auto const changed = verifyTorrent(tor, &stop_current_);
        tor->setVerifyState(TR_VERIFY_NONE);
        TR_ASSERT(tr_isTorrent(tor));

        fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
        if (!stop_current_ && changed)
        {
            fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
            tor->setDirty();
        }

        fmt::print(stderr, FMT_STRING("{:s}:{:d} calling callbacks\n"), __FILE__, __LINE__);
        callCallback(tor, stop_current_);
        fmt::print(stderr, FMT_STRING("{:s}:{:d} done calling callbacks\n"), __FILE__, __LINE__);
    }
}

void tr_verify_worker::add(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    tr_logAddTraceTor(tor, "Queued for verification");

    auto node = Node{};
    node.torrent = tor;
    node.current_size = tor->hasTotal();

    fmt::print(stderr, FMT_STRING("{:s}:{:d} adding [{:s}]\n"), __FILE__, __LINE__, tor->name());
    auto const lock = std::lock_guard(verify_mutex_);
    fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
    tor->setVerifyState(TR_VERIFY_WAIT);
    fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
    todo_.insert(node);
    fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);

    if (!verify_thread_id_)
    {
        fmt::print(stderr, FMT_STRING("{:s}:{:d} starting worker thread\n"), __FILE__, __LINE__);
        auto thread = std::thread(&tr_verify_worker::verifyThreadFunc, this);
        fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
        verify_thread_id_ = thread.get_id();
        fmt::print(stderr, FMT_STRING("{:s}:{:d}\n"), __FILE__, __LINE__);
        thread.detach();
    }
}

void tr_verify_worker::remove(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    verify_mutex_.lock();

    if (current_node_ && current_node_->torrent == tor)
    {
        stop_current_ = true;

        while (stop_current_)
        {
            verify_mutex_.unlock();
            tr_wait_msec(100);
            verify_mutex_.lock();
        }
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

    verify_mutex_.unlock();
}

tr_verify_worker::~tr_verify_worker()
{
    auto const lock = std::lock_guard(verify_mutex_);

    stop_current_ = true;
    todo_.clear();

    while (verify_thread_id_.has_value())
    {
        tr_wait_msec(20);
    }
}
