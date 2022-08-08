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

/***
****
***/

static auto constexpr MsecToSleepPerSecondDuringVerify = int{ 100 };

static bool verifyTorrent(tr_torrent* tor, bool const* stopFlag)
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

    while (!*stopFlag && piece < tor->pieceCount())
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
            auto const has_piece = sha->final() == tor->pieceHash(piece);

            if (has_piece || had_piece)
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
                tr_wait_msec(MsecToSleepPerSecondDuringVerify);
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

/***
****
***/

struct verify_node
{
    tr_torrent* torrent;
    tr_verify_done_func callback_func;
    void* callback_data;
    uint64_t current_size;

    [[nodiscard]] int compare(verify_node const& that) const
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
        if (torrent->infoHash() != that.torrent->infoHash())
        {
            return torrent->infoHash() < that.torrent->infoHash() ? -1 : 1;
        }

        return 0;
    }

    bool operator<(verify_node const& that) const
    {
        return compare(that) < 0;
    }
};

static struct verify_node currentNode;
// TODO: refactor s.t. this doesn't leak
static auto& verify_list{ *new std::set<verify_node>{} };
static std::optional<std::thread::id> verify_thread_id;
static bool stopCurrent = false;

static std::mutex verify_mutex_;

static void verifyThreadFunc()
{
    for (;;)
    {
        {
            auto const lock = std::lock_guard(verify_mutex_);

            stopCurrent = false;
            if (std::empty(verify_list))
            {
                currentNode.torrent = nullptr;
                verify_thread_id.reset();
                return;
            }

            auto const it = std::begin(verify_list);
            currentNode = *it;
            verify_list.erase(it);
        }

        tr_torrent* tor = currentNode.torrent;
        tr_logAddTraceTor(tor, "Verifying torrent");
        tor->setVerifyState(TR_VERIFY_NOW);
        auto const changed = verifyTorrent(tor, &stopCurrent);
        tor->setVerifyState(TR_VERIFY_NONE);
        TR_ASSERT(tr_isTorrent(tor));

        if (!stopCurrent && changed)
        {
            tor->setDirty();
        }

        if (currentNode.callback_func != nullptr)
        {
            (*currentNode.callback_func)(tor, stopCurrent, currentNode.callback_data);
        }
    }
}

void tr_verifyAdd(tr_torrent* tor, tr_verify_done_func callback_func, void* callback_data)
{
    TR_ASSERT(tr_isTorrent(tor));
    tr_logAddTraceTor(tor, "Queued for verification");

    auto node = verify_node{};
    node.torrent = tor;
    node.callback_func = callback_func;
    node.callback_data = callback_data;
    node.current_size = tor->hasTotal();

    auto const lock = std::lock_guard(verify_mutex_);
    tor->setVerifyState(TR_VERIFY_WAIT);
    verify_list.insert(node);

    if (!verify_thread_id)
    {
        auto thread = std::thread(verifyThreadFunc);
        verify_thread_id = thread.get_id();
        thread.detach();
    }
}

void tr_verifyRemove(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    verify_mutex_.lock();

    if (tor == currentNode.torrent)
    {
        stopCurrent = true;

        while (stopCurrent)
        {
            verify_mutex_.unlock();
            tr_wait_msec(100);
            verify_mutex_.lock();
        }
    }
    else
    {
        auto const it = std::find_if(
            std::begin(verify_list),
            std::end(verify_list),
            [tor](auto const& task) { return tor == task.torrent; });

        tor->setVerifyState(TR_VERIFY_NONE);

        if (it != std::end(verify_list))
        {
            if (it->callback_func != nullptr)
            {
                (*it->callback_func)(tor, true, it->callback_data);
            }

            verify_list.erase(it);
        }
    }

    verify_mutex_.unlock();
}

void tr_verifyClose(tr_session* /*session*/)
{
    auto const lock = std::lock_guard(verify_mutex_);

    stopCurrent = true;
    verify_list.clear();
}
