/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* memcmp() */
#include <stdlib.h> /* free() */

#include "transmission.h"
#include "completion.h"
#include "crypto-utils.h"
#include "file.h"
#include "list.h"
#include "log.h"
#include "platform.h" /* tr_lock() */
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h" /* tr_valloc(), tr_free() */
#include "verify.h"

/***
****
***/

enum
{
    MSEC_TO_SLEEP_PER_SECOND_DURING_VERIFY = 100
};

static bool verifyTorrent(tr_torrent* tor, bool* stopFlag)
{
    time_t end;
    tr_sha1_ctx_t sha;
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
    size_t const buflen = 1024 * 128; /* 128 KiB buffer */
    uint8_t* buffer = tr_valloc(buflen);

    sha = tr_sha1_init();

    tr_logAddTorDbg(tor, "%s", "verifying torrent...");
    tr_torrentSetChecked(tor, 0);

    while (!*stopFlag && pieceIndex < tor->info.pieceCount)
    {
        uint64_t leftInPiece;
        uint64_t bytesThisPass;
        uint64_t leftInFile;
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
            fd = filename == NULL ? TR_BAD_SYS_FILE : tr_sys_file_open(filename, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0,
                NULL);
            tr_free(filename);
            prevFileIndex = fileIndex;
        }

        /* figure out how much we can read this pass */
        leftInPiece = tr_torPieceCountBytes(tor, pieceIndex) - piecePos;
        leftInFile = file->length - filePos;
        bytesThisPass = MIN(leftInFile, leftInPiece);
        bytesThisPass = MIN(bytesThisPass, buflen);

        /* read a bit */
        if (fd != TR_BAD_SYS_FILE)
        {
            uint64_t numRead;

            if (tr_sys_file_read_at(fd, buffer, bytesThisPass, filePos, &numRead, NULL) && numRead > 0)
            {
                bytesThisPass = numRead;
                tr_sha1_update(sha, buffer, bytesThisPass);
                tr_sys_file_advise(fd, filePos, bytesThisPass, TR_SYS_FILE_ADVICE_DONT_NEED, NULL);
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
            time_t now;
            bool hasPiece;
            uint8_t hash[SHA_DIGEST_LENGTH];

            tr_sha1_final(sha, hash);
            hasPiece = memcmp(hash, tor->info.pieces[pieceIndex].hash, SHA_DIGEST_LENGTH) == 0;

            if (hasPiece || hadPiece)
            {
                tr_torrentSetHasPiece(tor, pieceIndex, hasPiece);
                changed |= hasPiece != hadPiece;
            }

            tr_torrentSetPieceChecked(tor, pieceIndex);
            now = tr_time();
            tor->anyDate = now;

            /* sleeping even just a few msec per second goes a long
             * way towards reducing IO load... */
            if (lastSleptAt != now)
            {
                lastSleptAt = now;
                tr_wait_msec(MSEC_TO_SLEEP_PER_SECOND_DURING_VERIFY);
            }

            sha = tr_sha1_init();
            pieceIndex++;
            piecePos = 0;
        }

        /* if we're finishing a file... */
        if (leftInFile == 0)
        {
            if (fd != TR_BAD_SYS_FILE)
            {
                tr_sys_file_close(fd, NULL);
                fd = TR_BAD_SYS_FILE;
            }

            fileIndex++;
            filePos = 0;
        }
    }

    /* cleanup */
    if (fd != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(fd, NULL);
    }

    tr_sha1_final(sha, NULL);
    free(buffer);

    /* stopwatch */
    end = tr_time();
    tr_logAddTorDbg(tor, "Verification is done. It took %d seconds to verify %" PRIu64 " bytes (%" PRIu64 " bytes per second)",
        (int)(end - begin), tor->info.totalSize, (uint64_t)(tor->info.totalSize / (1 + (end - begin))));

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
};

static struct verify_node currentNode;
static tr_list* verifyList = NULL;
static tr_thread* verifyThread = NULL;
static bool stopCurrent = false;

static tr_lock* getVerifyLock(void)
{
    static tr_lock* lock = NULL;

    if (lock == NULL)
    {
        lock = tr_lockNew();
    }

    return lock;
}

static void verifyThreadFunc(void* unused UNUSED)
{
    for (;;)
    {
        bool changed = false;
        tr_torrent* tor;
        struct verify_node* node;

        tr_lockLock(getVerifyLock());
        stopCurrent = false;
        node = verifyList != NULL ? verifyList->data : NULL;

        if (node == NULL)
        {
            currentNode.torrent = NULL;
            break;
        }

        currentNode = *node;
        tor = currentNode.torrent;
        tr_list_remove_data(&verifyList, node);
        tr_free(node);
        tr_lockUnlock(getVerifyLock());

        tr_logAddTorInfo(tor, "%s", _("Verifying torrent"));
        tr_torrentSetVerifyState(tor, TR_VERIFY_NOW);
        changed = verifyTorrent(tor, &stopCurrent);
        tr_torrentSetVerifyState(tor, TR_VERIFY_NONE);
        TR_ASSERT(tr_isTorrent(tor));

        if (!stopCurrent && changed)
        {
            tr_torrentSetDirty(tor);
        }

        if (currentNode.callback_func != NULL)
        {
            (*currentNode.callback_func)(tor, stopCurrent, currentNode.callback_data);
        }
    }

    verifyThread = NULL;
    tr_lockUnlock(getVerifyLock());
}

static int compareVerifyByPriorityAndSize(void const* va, void const* vb)
{
    struct verify_node const* a = va;
    struct verify_node const* b = vb;

    /* higher priority comes before lower priority */
    tr_priority_t const pa = tr_torrentGetPriority(a->torrent);
    tr_priority_t const pb = tr_torrentGetPriority(b->torrent);

    if (pa != pb)
    {
        return pa > pb ? -1 : 1;
    }

    /* smaller torrents come before larger ones because they verify faster */
    if (a->current_size < b->current_size)
    {
        return -1;
    }

    if (a->current_size > b->current_size)
    {
        return 1;
    }

    return 0;
}

void tr_verifyAdd(tr_torrent* tor, tr_verify_done_func callback_func, void* callback_data)
{
    TR_ASSERT(tr_isTorrent(tor));
    tr_logAddTorInfo(tor, "%s", _("Queued for verification"));

    struct verify_node* node = tr_new(struct verify_node, 1);
    node->torrent = tor;
    node->callback_func = callback_func;
    node->callback_data = callback_data;
    node->current_size = tr_torrentGetCurrentSizeOnDisk(tor);

    tr_lockLock(getVerifyLock());
    tr_torrentSetVerifyState(tor, TR_VERIFY_WAIT);
    tr_list_insert_sorted(&verifyList, node, compareVerifyByPriorityAndSize);

    if (verifyThread == NULL)
    {
        verifyThread = tr_threadNew(verifyThreadFunc, NULL);
    }

    tr_lockUnlock(getVerifyLock());
}

static int compareVerifyByTorrent(void const* va, void const* vb)
{
    struct verify_node const* a = va;
    tr_torrent const* b = vb;
    return a->torrent - b;
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
        struct verify_node* node = tr_list_remove(&verifyList, tor, compareVerifyByTorrent);

        tr_torrentSetVerifyState(tor, TR_VERIFY_NONE);

        if (node != NULL)
        {
            if (node->callback_func != NULL)
            {
                (*node->callback_func)(tor, true, node->callback_data);
            }

            tr_free(node);
        }
    }

    tr_lockUnlock(lock);
}

void tr_verifyClose(tr_session* session UNUSED)
{
    tr_lockLock(getVerifyLock());

    stopCurrent = true;
    tr_list_free(&verifyList, tr_free);

    tr_lockUnlock(getVerifyLock());
}
