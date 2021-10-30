/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cstdlib> /* free() */
#include <cstring> /* memcmp() */
#include <set>

#include "transmission.h"
#include "completion.h"
#include "crypto-utils.h"
#include "file.h"
#include "log.h"
#include "platform.h" /* tr_lock() */
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h" /* tr_malloc(), tr_free() */
#include "verify.h"

/***
****
***/

static auto constexpr MsecToSleepPerSecondDuringVerify = int{ 100 };

static bool verifyTorrent(tr_torrent* tor, bool* stopFlag)
{
    tr_sys_file_t fd = TR_BAD_SYS_FILE;
    uint64_t filePos = 0;
    bool changed = false;
    bool hadPiece = false;
    time_t lastSleptAt = 0;
    uint32_t piecePos = 0;
    tr_file_index_t fileIndex = 0;
    tr_file_index_t prevFileIndex = !fileIndex;
    tr_piece_index_t pieceIndex = 0;
    time_t const begin = tr_time();
    size_t const buflen = 1024 * 128; // 128 KiB buffer
    auto* const buffer = static_cast<uint8_t*>(tr_malloc(buflen));

    tr_sha1_ctx_t sha = tr_sha1_init();

    tr_logAddTorDbg(tor, "%s", "verifying torrent...");
    tor->verify_progress = 0;

    while (!*stopFlag && pieceIndex < tor->info.pieceCount)
    {
        tr_file const* file = &tor->info.files[fileIndex];

        /* if we're starting a new piece... */
        if (piecePos == 0)
        {
            hadPiece = tr_torrentPieceIsComplete(tor, pieceIndex);
        }

        /* if we're starting a new file... */
        if (filePos == 0 && fd == TR_BAD_SYS_FILE && fileIndex != prevFileIndex)
        {
            char* filename = tr_torrentFindFile(tor, fileIndex);
            fd = filename == nullptr ? TR_BAD_SYS_FILE :
                                       tr_sys_file_open(filename, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, nullptr);
            tr_free(filename);
            prevFileIndex = fileIndex;
        }

        /* figure out how much we can read this pass */
        uint64_t leftInPiece = tr_torPieceCountBytes(tor, pieceIndex) - piecePos;
        uint64_t leftInFile = file->length - filePos;
        uint64_t bytesThisPass = std::min(leftInFile, leftInPiece);
        bytesThisPass = std::min(bytesThisPass, uint64_t{ buflen });

        /* read a bit */
        if (fd != TR_BAD_SYS_FILE)
        {
            auto numRead = uint64_t{};
            if (tr_sys_file_read_at(fd, buffer, bytesThisPass, filePos, &numRead, nullptr) && numRead > 0)
            {
                bytesThisPass = numRead;
                tr_sha1_update(sha, buffer, bytesThisPass);
                tr_sys_file_advise(fd, filePos, bytesThisPass, TR_SYS_FILE_ADVICE_DONT_NEED, nullptr);
            }
        }

        /* move our offsets */
        leftInPiece -= bytesThisPass;
        leftInFile -= bytesThisPass;
        piecePos += bytesThisPass;
        filePos += bytesThisPass;

        /* if we're finishing a piece... */
        if (leftInPiece == 0)
        {
            auto hash = tr_sha1_digest_t{};
            tr_sha1_final(sha, std::data(hash));
            bool const hasPiece = hash == tor->info.pieces[pieceIndex];

            if (hasPiece || hadPiece)
            {
                tr_torrentSetHasPiece(tor, pieceIndex, hasPiece);
                changed |= hasPiece != hadPiece;
            }

            time_t const now = tr_time();
            tor->anyDate = now;

            /* sleeping even just a few msec per second goes a long
             * way towards reducing IO load... */
            if (lastSleptAt != now)
            {
                lastSleptAt = now;
                tr_wait_msec(MsecToSleepPerSecondDuringVerify);
            }

            sha = tr_sha1_init();
            ++pieceIndex;
            tor->verify_progress = pieceIndex / double(tor->info.pieceCount);
            piecePos = 0;
        }

        /* if we're finishing a file... */
        if (leftInFile == 0)
        {
            if (fd != TR_BAD_SYS_FILE)
            {
                tr_sys_file_close(fd, nullptr);
                fd = TR_BAD_SYS_FILE;
            }

            fileIndex++;
            filePos = 0;
        }
    }

    /* cleanup */
    if (fd != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(fd, nullptr);
    }

    tor->verify_progress.reset();
    tr_sha1_final(sha, nullptr);
    free(buffer);

    /* stopwatch */
    time_t const end = tr_time();
    tr_logAddTorDbg(
        tor,
        "Verification is done. It took %d seconds to verify %" PRIu64 " bytes (%" PRIu64 " bytes per second)",
        (int)(end - begin),
        tor->info.totalSize,
        (uint64_t)(tor->info.totalSize / (1 + (end - begin))));

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

    int compare(verify_node const& that) const
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

        return 0;
    }

    bool operator<(verify_node const& that) const
    {
        return compare(that) < 0;
    }
};

static struct verify_node currentNode;
// TODO: refactor s.t. this doesn't leak
static auto& verifyList{ *new std::set<verify_node>{} };
static tr_thread* verifyThread = nullptr;
static bool stopCurrent = false;

static tr_lock* getVerifyLock(void)
{
    static tr_lock* lock = nullptr;

    if (lock == nullptr)
    {
        lock = tr_lockNew();
    }

    return lock;
}

static void verifyThreadFunc(void* /*user_data*/)
{
    for (;;)
    {
        bool changed = false;

        tr_lockLock(getVerifyLock());
        stopCurrent = false;
        if (std::empty(verifyList))
        {
            currentNode.torrent = nullptr;
            break; // FIXME: unbalanced lock?
        }

        auto const it = std::begin(verifyList);
        currentNode = *it;
        verifyList.erase(it);
        tr_lockUnlock(getVerifyLock());

        tr_torrent* tor = currentNode.torrent;
        tr_logAddTorInfo(tor, "%s", _("Verifying torrent"));
        tr_torrentSetVerifyState(tor, TR_VERIFY_NOW);
        changed = verifyTorrent(tor, &stopCurrent);
        tr_torrentSetVerifyState(tor, TR_VERIFY_NONE);
        TR_ASSERT(tr_isTorrent(tor));

        if (!stopCurrent && changed)
        {
            tr_torrentSetDirty(tor);
        }

        if (currentNode.callback_func != nullptr)
        {
            (*currentNode.callback_func)(tor, stopCurrent, currentNode.callback_data);
        }
    }

    verifyThread = nullptr;
    tr_lockUnlock(getVerifyLock());
}

void tr_verifyAdd(tr_torrent* tor, tr_verify_done_func callback_func, void* callback_data)
{
    TR_ASSERT(tr_isTorrent(tor));
    tr_logAddTorInfo(tor, "%s", _("Queued for verification"));

    auto node = verify_node{};
    node.torrent = tor;
    node.callback_func = callback_func;
    node.callback_data = callback_data;
    node.current_size = tr_torrentGetCurrentSizeOnDisk(tor);

    tr_lockLock(getVerifyLock());
    tr_torrentSetVerifyState(tor, TR_VERIFY_WAIT);
    verifyList.insert(node);

    if (verifyThread == nullptr)
    {
        verifyThread = tr_threadNew(verifyThreadFunc, nullptr);
    }

    tr_lockUnlock(getVerifyLock());
}

void tr_verifyRemove(tr_torrent* tor)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_lock* lock = getVerifyLock();
    tr_lockLock(lock);

    if (tor == currentNode.torrent)
    {
        stopCurrent = true;

        while (stopCurrent)
        {
            tr_lockUnlock(lock);
            tr_wait_msec(100);
            tr_lockLock(lock);
        }
    }
    else
    {
        auto const it = std::find_if(
            std::begin(verifyList),
            std::end(verifyList),
            [tor](auto const& task) { return tor == task.torrent; });

        tr_torrentSetVerifyState(tor, TR_VERIFY_NONE);

        if (it != std::end(verifyList))
        {
            if (it->callback_func != nullptr)
            {
                (*it->callback_func)(tor, true, it->callback_data);
            }

            verifyList.erase(it);
        }
    }

    tr_lockUnlock(lock);
}

void tr_verifyClose(tr_session* /*session*/)
{
    tr_lockLock(getVerifyLock());

    stopCurrent = true;
    verifyList.clear();

    tr_lockUnlock(getVerifyLock());
}
