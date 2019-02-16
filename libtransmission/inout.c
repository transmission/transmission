/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>
#include <stdlib.h> /* bsearch() */
#include <string.h> /* memcmp() */

#include "transmission.h"
#include "cache.h" /* tr_cacheReadBlock() */
#include "crypto-utils.h"
#include "error.h"
#include "fdlimit.h"
#include "file.h"
#include "inout.h"
#include "log.h"
#include "peer-common.h" /* MAX_BLOCK_SIZE */
#include "stats.h" /* tr_statsFileCreated() */
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"

/****
*****  Low-level IO functions
****/

enum
{
    TR_IO_READ,
    TR_IO_PREFETCH,
    /* Any operations that require write access must follow TR_IO_WRITE. */
    TR_IO_WRITE
};

/* returns 0 on success, or an errno on failure */
static int readOrWriteBytes(tr_session* session, tr_torrent* tor, int ioMode, tr_file_index_t fileIndex, uint64_t fileOffset,
    void* buf, size_t buflen)
{
    tr_sys_file_t fd;
    int err = 0;
    bool const doWrite = ioMode >= TR_IO_WRITE;
    tr_info const* const info = &tor->info;
    tr_file const* const file = &info->files[fileIndex];

    TR_ASSERT(fileIndex < info->fileCount);
    TR_ASSERT(file->length == 0 || fileOffset < file->length);
    TR_ASSERT(fileOffset + buflen <= file->length);

    if (file->length == 0)
    {
        return 0;
    }

    /***
    ****  Find the fd
    ***/

    fd = tr_fdFileGetCached(session, tr_torrentId(tor), fileIndex, doWrite);

    if (fd == TR_BAD_SYS_FILE)
    {
        /* it's not cached, so open/create it now */
        char* subpath;
        char const* base;

        /* see if the file exists... */
        if (!tr_torrentFindFile2(tor, fileIndex, &base, &subpath, NULL))
        {
            /* we can't read a file that doesn't exist... */
            if (!doWrite)
            {
                err = ENOENT;
            }

            /* figure out where the file should go, so we can create it */
            base = tr_torrentGetCurrentDir(tor);
            subpath = tr_sessionIsIncompleteFileNamingEnabled(tor->session) ? tr_torrentBuildPartial(tor, fileIndex) :
                tr_strdup(file->name);
        }

        if (err == 0)
        {
            /* open (and maybe create) the file */
            char* filename = tr_buildPath(base, subpath, NULL);
            int const prealloc = (file->dnd || !doWrite) ? TR_PREALLOCATE_NONE : tor->session->preallocationMode;

            if ((fd = tr_fdFileCheckout(session, tor->uniqueId, fileIndex, filename, doWrite, prealloc,
                file->length)) == TR_BAD_SYS_FILE)
            {
                err = errno;
                tr_logAddTorErr(tor, "tr_fdFileCheckout failed for \"%s\": %s", filename, tr_strerror(err));
            }
            else if (doWrite)
            {
                /* make a note that we just created a file */
                tr_statsFileCreated(tor->session);
            }

            tr_free(filename);
        }

        tr_free(subpath);
    }

    /***
    ****  Use the fd
    ***/

    if (err == 0)
    {
        tr_error* error = NULL;

        if (ioMode == TR_IO_READ)
        {
            if (!tr_sys_file_read_at(fd, buf, buflen, fileOffset, NULL, &error))
            {
                err = error->code;
                tr_logAddTorErr(tor, "read failed for \"%s\": %s", file->name, error->message);
                tr_error_free(error);
            }
        }
        else if (ioMode == TR_IO_WRITE)
        {
            if (!tr_sys_file_write_at(fd, buf, buflen, fileOffset, NULL, &error))
            {
                err = error->code;
                tr_logAddTorErr(tor, "write failed for \"%s\": %s", file->name, error->message);
                tr_error_free(error);
            }
        }
        else if (ioMode == TR_IO_PREFETCH)
        {
            tr_sys_file_advise(fd, fileOffset, buflen, TR_SYS_FILE_ADVICE_WILL_NEED, NULL);
        }
        else
        {
            abort();
        }
    }

    return err;
}

static int compareOffsetToFile(void const* a, void const* b)
{
    uint64_t const offset = *(uint64_t const*)a;
    tr_file const* file = b;

    if (offset < file->offset)
    {
        return -1;
    }

    if (offset >= file->offset + file->length)
    {
        return 1;
    }

    return 0;
}

void tr_ioFindFileLocation(tr_torrent const* tor, tr_piece_index_t pieceIndex, uint32_t pieceOffset, tr_file_index_t* fileIndex,
    uint64_t* fileOffset)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t const offset = tr_pieceOffset(tor, pieceIndex, pieceOffset, 0);

    TR_ASSERT(offset < tor->info.totalSize);

    tr_file const* file = bsearch(&offset, tor->info.files, tor->info.fileCount, sizeof(tr_file), compareOffsetToFile);

    TR_ASSERT(file != NULL);

    *fileIndex = file - tor->info.files;
    *fileOffset = offset - file->offset;

    TR_ASSERT(*fileIndex < tor->info.fileCount);
    TR_ASSERT(*fileOffset < file->length);
    TR_ASSERT(tor->info.files[*fileIndex].offset + *fileOffset == offset);
}

/* returns 0 on success, or an errno on failure */
static int readOrWritePiece(tr_torrent* tor, int ioMode, tr_piece_index_t pieceIndex, uint32_t pieceOffset, uint8_t* buf,
    size_t buflen)
{
    int err = 0;
    tr_file_index_t fileIndex;
    uint64_t fileOffset;
    tr_info const* info = &tor->info;

    if (pieceIndex >= tor->info.pieceCount)
    {
        return EINVAL;
    }

    tr_ioFindFileLocation(tor, pieceIndex, pieceOffset, &fileIndex, &fileOffset);

    while (buflen != 0 && err == 0)
    {
        tr_file const* file = &info->files[fileIndex];
        uint64_t const bytesThisPass = MIN(buflen, file->length - fileOffset);

        err = readOrWriteBytes(tor->session, tor, ioMode, fileIndex, fileOffset, buf, bytesThisPass);
        buf += bytesThisPass;
        buflen -= bytesThisPass;
        fileIndex++;
        fileOffset = 0;

        if (err != 0 && ioMode == TR_IO_WRITE && tor->error != TR_STAT_LOCAL_ERROR)
        {
            char* path = tr_buildPath(tor->downloadDir, file->name, NULL);
            tr_torrentSetLocalError(tor, "%s (%s)", tr_strerror(err), path);
            tr_free(path);
        }
    }

    return err;
}

int tr_ioRead(tr_torrent* tor, tr_piece_index_t pieceIndex, uint32_t begin, uint32_t len, uint8_t* buf)
{
    return readOrWritePiece(tor, TR_IO_READ, pieceIndex, begin, buf, len);
}

int tr_ioPrefetch(tr_torrent* tor, tr_piece_index_t pieceIndex, uint32_t begin, uint32_t len)
{
    return readOrWritePiece(tor, TR_IO_PREFETCH, pieceIndex, begin, NULL, len);
}

int tr_ioWrite(tr_torrent* tor, tr_piece_index_t pieceIndex, uint32_t begin, uint32_t len, uint8_t const* buf)
{
    return readOrWritePiece(tor, TR_IO_WRITE, pieceIndex, begin, (uint8_t*)buf, len);
}

/****
*****
****/

static bool recalculateHash(tr_torrent* tor, tr_piece_index_t pieceIndex, uint8_t* setme)
{
    TR_ASSERT(tor != NULL);
    TR_ASSERT(pieceIndex < tor->info.pieceCount);
    TR_ASSERT(setme != NULL);

    size_t bytesLeft;
    uint32_t offset = 0;
    bool success = true;
    size_t const buflen = tor->blockSize;
    void* buffer = tr_valloc(buflen);
    tr_sha1_ctx_t sha;

    TR_ASSERT(buffer != NULL);
    TR_ASSERT(buflen > 0);

    sha = tr_sha1_init();
    bytesLeft = tr_torPieceCountBytes(tor, pieceIndex);

    tr_ioPrefetch(tor, pieceIndex, offset, bytesLeft);

    while (bytesLeft != 0)
    {
        size_t const len = MIN(bytesLeft, buflen);
        success = tr_cacheReadBlock(tor->session->cache, tor, pieceIndex, offset, len, buffer) == 0;

        if (!success)
        {
            break;
        }

        tr_sha1_update(sha, buffer, len);
        offset += len;
        bytesLeft -= len;
    }

    tr_sha1_final(sha, success ? setme : NULL);

    tr_free(buffer);
    return success;
}

bool tr_ioTestPiece(tr_torrent* tor, tr_piece_index_t piece)
{
    uint8_t hash[SHA_DIGEST_LENGTH];

    return recalculateHash(tor, piece, hash) && memcmp(hash, tor->info.pieces[piece].hash, SHA_DIGEST_LENGTH) == 0;
}
