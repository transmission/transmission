// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <optional>
#include <vector>

#include "transmission.h"

#include "cache.h" /* tr_cacheReadBlock() */
#include "crypto-utils.h"
#include "error.h"
#include "fdlimit.h"
#include "file.h"
#include "inout.h"
#include "log.h"
#include "stats.h" /* tr_statsFileCreated() */
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"

using namespace std::literals;

namespace
{

bool readEntireBuf(tr_sys_file_t fd, uint64_t file_offset, uint8_t* buf, uint64_t buflen, tr_error** error)
{
    while (buflen > 0)
    {
        auto n_read = uint64_t{};

        if (!tr_sys_file_read_at(fd, buf, buflen, file_offset, &n_read, error))
        {
            return false;
        }

        buf += n_read;
        buflen -= n_read;
        file_offset += n_read;
    }

    return true;
}

bool writeEntireBuf(tr_sys_file_t fd, uint64_t file_offset, uint8_t const* buf, uint64_t buflen, tr_error** error)
{
    while (buflen > 0)
    {
        auto n_written = uint64_t{};

        if (!tr_sys_file_write_at(fd, buf, buflen, file_offset, &n_written, error))
        {
            return false;
        }

        buf += n_written;
        buflen -= n_written;
        file_offset += n_written;
    }

    return true;
}

enum class IoMode
{
    Read,
    Prefetch,
    Write
};

/* returns 0 on success, or an errno on failure */
int readOrWriteBytes(
    tr_session* session,
    tr_torrent* tor,
    IoMode io_mode,
    tr_file_index_t file_index,
    uint64_t file_offset,
    uint8_t* buf,
    size_t buflen)
{
    TR_ASSERT(file_index < tor->fileCount());

    int err = 0;
    bool const doWrite = io_mode == IoMode::Write;
    auto const file_size = tor->fileSize(file_index);
    TR_ASSERT(file_size == 0 || file_offset < file_size);
    TR_ASSERT(file_offset + buflen <= file_size);

    if (file_size == 0)
    {
        return 0;
    }

    /***
    ****  Find the fd
    ***/

    auto fd = tr_fdFileGetCached(session, tr_torrentId(tor), file_index, doWrite);

    if (fd == TR_BAD_SYS_FILE) /* it's not cached, so open/create it now */
    {
        /* see if the file exists... */
        char const* base = nullptr;
        char* subpath = nullptr;
        if (!tr_torrentFindFile2(tor, file_index, &base, &subpath, nullptr))
        {
            /* we can't read a file that doesn't exist... */
            if (!doWrite)
            {
                err = ENOENT;
            }

            /* figure out where the file should go, so we can create it */
            base = tr_torrentGetCurrentDir(tor);
            subpath = tr_sessionIsIncompleteFileNamingEnabled(tor->session) ? tr_torrentBuildPartial(tor, file_index) :
                                                                              tr_strvDup(tor->fileSubpath(file_index));
        }

        if (err == 0)
        {
            /* open (and maybe create) the file */
            auto const filename = tr_strvPath(base, subpath);
            auto const prealloc = (!doWrite || !tor->fileIsWanted(file_index)) ? TR_PREALLOCATE_NONE :
                                                                                 tor->session->preallocationMode;

            fd = tr_fdFileCheckout(session, tor->uniqueId, file_index, filename.c_str(), doWrite, prealloc, file_size);
            if (fd == TR_BAD_SYS_FILE)
            {
                err = errno;
                tr_logErrorTor(tor, "tr_fdFileCheckout failed for \"%s\": %s", filename.c_str(), tr_strerror(err));
            }
            else if (doWrite)
            {
                /* make a note that we just created a file */
                tr_statsFileCreated(tor->session);
            }
        }

        tr_free(subpath);
    }

    if (err != 0)
    {
        return err;
    }

    /***
    ****  Use the fd
    ***/

    tr_error* error = nullptr;

    switch (io_mode)
    {
    case IoMode::Read:
        if (!readEntireBuf(fd, file_offset, buf, buflen, &error))
        {
            err = error->code;
            tr_logErrorTor(tor, "read failed for \"%s\": %s", tor->fileSubpath(file_index).c_str(), error->message);
            tr_error_free(error);
        }
        break;

    case IoMode::Write:
        if (!writeEntireBuf(fd, file_offset, buf, buflen, &error))
        {
            err = error->code;
            tr_logErrorTor(tor, "write failed for \"%s\": %s", tor->fileSubpath(file_index).c_str(), error->message);
            tr_error_free(error);
        }
        break;

    case IoMode::Prefetch:
        tr_sys_file_advise(fd, file_offset, buflen, TR_SYS_FILE_ADVICE_WILL_NEED, nullptr);
        break;
    }

    return err;
}

/* returns 0 on success, or an errno on failure */
int readOrWritePiece(tr_torrent* tor, IoMode io_mode, tr_block_info::Location loc, uint8_t* buf, size_t buflen)
{
    int err = 0;

    if (loc.piece >= tor->pieceCount())
    {
        return EINVAL;
    }

    auto [file_index, file_offset] = tor->fileOffset(loc);

    while (buflen != 0 && err == 0)
    {
        uint64_t const bytes_this_pass = std::min(uint64_t{ buflen }, uint64_t{ tor->fileSize(file_index) - file_offset });

        err = readOrWriteBytes(tor->session, tor, io_mode, file_index, file_offset, buf, bytes_this_pass);
        buf += bytes_this_pass;
        buflen -= bytes_this_pass;

        if (err != 0 && io_mode == IoMode::Write && tor->error != TR_STAT_LOCAL_ERROR)
        {
            auto const path = tr_strvPath(tor->downloadDir().sv(), tor->fileSubpath(file_index));
            tor->setLocalError(tr_strvJoin(tr_strerror(err), " ("sv, path, ")"sv));
        }

        ++file_index;
        file_offset = 0;
    }

    return err;
}

std::optional<tr_sha1_digest_t> recalculateHash(tr_torrent* tor, tr_piece_index_t piece)
{
    TR_ASSERT(tor != nullptr);
    TR_ASSERT(piece < tor->pieceCount());

    auto bytes_left = size_t(tor->pieceSize(piece));
    auto loc = tor->pieceLoc(piece);
    tr_ioPrefetch(tor, loc, bytes_left);

    auto sha = tr_sha1_init();
    auto buffer = std::vector<uint8_t>(tr_block_info::BlockSize);
    while (bytes_left != 0)
    {
        size_t const len = std::min(bytes_left, std::size(buffer));
        if (auto const success = tr_cacheReadBlock(tor->session->cache, tor, loc, len, std::data(buffer)) == 0; !success)
        {
            tr_sha1_final(sha);
            return {};
        }

        tr_sha1_update(sha, std::data(buffer), len);
        loc = tor->byteLoc(loc.byte + len);
        bytes_left -= len;
    }

    return tr_sha1_final(sha);
}

} // namespace

int tr_ioRead(tr_torrent* tor, tr_block_info::Location loc, uint32_t len, uint8_t* buf)
{
    return readOrWritePiece(tor, IoMode::Read, loc, buf, len);
}

int tr_ioPrefetch(tr_torrent* tor, tr_block_info::Location loc, uint32_t len)
{
    return readOrWritePiece(tor, IoMode::Prefetch, loc, nullptr, len);
}

int tr_ioWrite(tr_torrent* tor, tr_block_info::Location loc, uint32_t len, uint8_t const* buf)
{
    return readOrWritePiece(tor, IoMode::Write, loc, (uint8_t*)buf, len);
}

bool tr_ioTestPiece(tr_torrent* tor, tr_piece_index_t piece)
{
    auto const hash = recalculateHash(tor, piece);
    return hash && *hash == tor->pieceHash(piece);
}
