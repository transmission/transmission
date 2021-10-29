/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cerrno>
#include <cstdlib> /* bsearch() */
#include <cstring> /* memcmp() */
#include <optional>

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
static int readOrWriteBytes(
    tr_session* session,
    tr_torrent* tor,
    int ioMode,
    tr_file_index_t fileIndex,
    uint64_t fileOffset,
    void* buf,
    size_t buflen)
{
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

    tr_sys_file_t fd = tr_fdFileGetCached(session, tr_torrentId(tor), fileIndex, doWrite);

    if (fd == TR_BAD_SYS_FILE) /* it's not cached, so open/create it now */
    {
        /* see if the file exists... */
        char const* base = nullptr;
        char* subpath = nullptr;
        if (!tr_torrentFindFile2(tor, fileIndex, &base, &subpath, nullptr))
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
            char* filename = tr_buildPath(base, subpath, nullptr);
            tr_preallocation_mode const prealloc = (file->dnd || !doWrite) ? TR_PREALLOCATE_NONE :
                                                                             tor->session->preallocationMode;

            fd = tr_fdFileCheckout(session, tor->uniqueId, fileIndex, filename, doWrite, prealloc, file->length);
            if (fd == TR_BAD_SYS_FILE)
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
        tr_error* error = nullptr;

        if (ioMode == TR_IO_READ)
        {
            if (!tr_sys_file_read_at(fd, buf, buflen, fileOffset, nullptr, &error))
            {
                err = error->code;
                tr_logAddTorErr(tor, "read failed for \"%s\": %s", file->name, error->message);
                tr_error_free(error);
            }
        }
        else if (ioMode == TR_IO_WRITE)
        {
            if (!tr_sys_file_write_at(fd, buf, buflen, fileOffset, nullptr, &error))
            {
                err = error->code;
                tr_logAddTorErr(tor, "write failed for \"%s\": %s", file->name, error->message);
                tr_error_free(error);
            }
        }
        else if (ioMode == TR_IO_PREFETCH)
        {
            tr_sys_file_advise(fd, fileOffset, buflen, TR_SYS_FILE_ADVICE_WILL_NEED, nullptr);
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
    auto const offset = *static_cast<uint64_t const*>(a);
    auto const* file = static_cast<tr_file const*>(b);

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

void tr_ioFindFileLocation(
    tr_torrent const* tor,
    tr_piece_index_t pieceIndex,
    uint32_t pieceOffset,
    tr_file_index_t* fileIndex,
    uint64_t* fileOffset)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t const offset = tr_pieceOffset(tor, pieceIndex, pieceOffset, 0);
    TR_ASSERT(offset < tor->info.totalSize);

    auto const* file = static_cast<tr_file const*>(
        bsearch(&offset, tor->info.files, tor->info.fileCount, sizeof(tr_file), compareOffsetToFile));
    TR_ASSERT(file != nullptr);

    if (file != nullptr)
    {
        *fileIndex = file - tor->info.files;
        *fileOffset = offset - file->offset;
        TR_ASSERT(*fileIndex < tor->info.fileCount);
        TR_ASSERT(*fileOffset < file->length);
        TR_ASSERT(tor->info.files[*fileIndex].offset + *fileOffset == offset);
    }
}

/* returns 0 on success, or an errno on failure */
static int readOrWritePiece(
    tr_torrent* tor,
    int ioMode,
    tr_piece_index_t pieceIndex,
    uint32_t pieceOffset,
    uint8_t* buf,
    size_t buflen)
{
    int err = 0;
    tr_info const* info = &tor->info;

    if (pieceIndex >= tor->info.pieceCount)
    {
        return EINVAL;
    }

    auto fileIndex = tr_file_index_t{};
    auto fileOffset = uint64_t{};
    tr_ioFindFileLocation(tor, pieceIndex, pieceOffset, &fileIndex, &fileOffset);

    while (buflen != 0 && err == 0)
    {
        tr_file const* file = &info->files[fileIndex];
        uint64_t const bytesThisPass = std::min(uint64_t{ buflen }, uint64_t{ file->length - fileOffset });

        err = readOrWriteBytes(tor->session, tor, ioMode, fileIndex, fileOffset, buf, bytesThisPass);
        buf += bytesThisPass;
        buflen -= bytesThisPass;
        fileIndex++;
        fileOffset = 0;

        if (err != 0 && ioMode == TR_IO_WRITE && tor->error != TR_STAT_LOCAL_ERROR)
        {
            char* path = tr_buildPath(tor->downloadDir, file->name, nullptr);
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
    return readOrWritePiece(tor, TR_IO_PREFETCH, pieceIndex, begin, nullptr, len);
}

int tr_ioWrite(tr_torrent* tor, tr_piece_index_t pieceIndex, uint32_t begin, uint32_t len, uint8_t const* buf)
{
    return readOrWritePiece(tor, TR_IO_WRITE, pieceIndex, begin, (uint8_t*)buf, len);
}

/****
*****
****/

static std::optional<tr_sha1_digest_t> recalculateHash(tr_torrent* tor, tr_piece_index_t piece)
{
    TR_ASSERT(tor != nullptr);
    TR_ASSERT(piece < tor->info.pieceCount);

    auto bytes_left = size_t{ tr_torPieceCountBytes(tor, piece) };
    auto offset = uint32_t{};
    tr_ioPrefetch(tor, piece, offset, bytes_left);

    auto sha = tr_sha1_init();
    auto buffer = std::vector<uint8_t>(tor->blockSize);
    while (bytes_left != 0)
    {
        size_t const len = std::min(bytes_left, std::size(buffer));
        auto const success = tr_cacheReadBlock(tor->session->cache, tor, piece, offset, len, std::data(buffer)) == 0;
        if (!success)
        {
            tr_sha1_final(sha, nullptr);
            return {};
        }

        tr_sha1_update(sha, std::data(buffer), len);
        offset += len;
        bytes_left -= len;
    }

    auto digest = tr_sha1_digest_t{};
    tr_sha1_final(sha, std::data(digest));
    return digest;
}

bool tr_ioTestPiece(tr_torrent* tor, tr_piece_index_t piece)
{
    auto const hash = recalculateHash(tor, piece);
    return hash && *hash == tor->info.pieces[piece];
}
