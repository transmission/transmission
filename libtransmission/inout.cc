// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <optional>

#include <fmt/core.h>

#include "transmission.h"

#include "cache.h" /* tr_cacheReadBlock() */
#include "crypto-utils.h"
#include "error.h"
#include "file.h"
#include "inout.h"
#include "log.h"
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

bool getFilename(tr_pathbuf& setme, tr_torrent const* tor, tr_file_index_t file_index, IoMode io_mode)
{
    if (auto found = tor->findFile(file_index); found)
    {
        setme.assign(found->filename());
        return true;
    }

    if (io_mode != IoMode::Write)
    {
        return false;
    }

    // We didn't find the file that we want to write to.
    // Let's figure out where it goes so that we can create it.
    auto const base = tor->currentDir();
    auto const suffix = tor->session->isIncompleteFileNamingEnabled() ? tr_torrent_files::PartialFileSuffix : ""sv;
    setme.assign(base, '/', tor->fileSubpath(file_index), suffix);
    return true;
}

void readOrWriteBytes(
    tr_session* session,
    tr_torrent* tor,
    IoMode io_mode,
    tr_file_index_t file_index,
    uint64_t file_offset,
    uint8_t* buf,
    size_t buflen,
    tr_error** error)
{
    TR_ASSERT(file_index < tor->fileCount());

    bool const do_write = io_mode == IoMode::Write;
    auto const file_size = tor->fileSize(file_index);
    TR_ASSERT(file_size == 0 || file_offset < file_size);
    TR_ASSERT(file_offset + buflen <= file_size);

    if (file_size == 0)
    {
        return;
    }

    // --- Find the fd

    auto fd = session->openFiles().get(tor->id(), file_index, do_write);
    auto filename = tr_pathbuf{};
    if (!fd && !getFilename(filename, tor, file_index, io_mode))
    {
        auto const err = ENOENT;
        auto const msg = fmt::format(
            _("Couldn't get '{path}': {error} ({error_code})"),
            fmt::arg("path", tor->fileSubpath(file_index)),
            fmt::arg("error", tr_strerror(err)),
            fmt::arg("error_code", err));
        tr_error_set(error, err, msg);
        return;
    }

    if (!fd) // not in the cache, so open or create it now
    {
        // open (and maybe create) the file
        auto const prealloc = (!do_write || !tor->fileIsWanted(file_index)) ? TR_PREALLOCATE_NONE :
                                                                              tor->session->preallocationMode();
        fd = session->openFiles().get(tor->id(), file_index, do_write, filename, prealloc, file_size);
        if (fd && do_write)
        {
            // make a note that we just created a file
            tor->session->addFileCreated();
        }
    }

    if (!fd) // couldn't create/open it either
    {
        auto const err = errno;
        auto const msg = fmt::format(
            _("Couldn't get '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", tr_strerror(err)),
            fmt::arg("error_code", err));
        tr_error_set(error, err, msg);
        tr_logAddErrorTor(tor, msg);
        return;
    }

    switch (io_mode)
    {
    case IoMode::Read:
        if (tr_error* my_error = nullptr; !readEntireBuf(*fd, file_offset, buf, buflen, &my_error) && my_error != nullptr)
        {
            tr_logAddErrorTor(
                tor,
                fmt::format(
                    _("Couldn't read '{path}': {error} ({error_code})"),
                    fmt::arg("path", tor->fileSubpath(file_index)),
                    fmt::arg("error", my_error->message),
                    fmt::arg("error_code", my_error->code)));
            tr_error_propagate(error, &my_error);
            return;
        }
        break;

    case IoMode::Write:
        if (tr_error* my_error = nullptr; !writeEntireBuf(*fd, file_offset, buf, buflen, &my_error) && my_error != nullptr)
        {
            tr_logAddErrorTor(
                tor,
                fmt::format(
                    _("Couldn't save '{path}': {error} ({error_code})"),
                    fmt::arg("path", tor->fileSubpath(file_index)),
                    fmt::arg("error", my_error->message),
                    fmt::arg("error_code", my_error->code)));
            tr_error_propagate(error, &my_error);
            return;
        }
        break;

    case IoMode::Prefetch:
        tr_sys_file_advise(*fd, file_offset, buflen, TR_SYS_FILE_ADVICE_WILL_NEED);
        break;
    }
}

/* returns 0 on success, or an errno on failure */
int readOrWritePiece(tr_torrent* tor, IoMode io_mode, tr_block_info::Location loc, uint8_t* buf, size_t buflen)
{
    if (loc.piece >= tor->pieceCount())
    {
        return EINVAL;
    }

    auto [file_index, file_offset] = tor->fileOffset(loc);

    while (buflen != 0)
    {
        uint64_t const bytes_this_pass = std::min(uint64_t{ buflen }, uint64_t{ tor->fileSize(file_index) - file_offset });

        tr_error* error = nullptr;
        readOrWriteBytes(tor->session, tor, io_mode, file_index, file_offset, buf, bytes_this_pass, &error);

        if (error != nullptr)
        {
            if (io_mode == IoMode::Write && tor->error != TR_STAT_LOCAL_ERROR)
            {
                tor->setLocalError(error->message);
                tr_torrentStop(tor);
            }

            auto const error_code = error->code;
            tr_error_clear(&error);
            return error_code;
        }

        if (buf != nullptr)
        {
            buf += bytes_this_pass;
        }
        buflen -= bytes_this_pass;

        ++file_index;
        file_offset = 0;
    }

    return 0;
}

std::optional<tr_sha1_digest_t> recalculateHash(tr_torrent* tor, tr_piece_index_t piece)
{
    TR_ASSERT(tor != nullptr);
    TR_ASSERT(piece < tor->pieceCount());

    auto sha = tr_sha1::create();
    auto buffer = std::array<uint8_t, tr_block_info::BlockSize>{};

    auto& cache = tor->session->cache;
    auto const [begin_byte, end_byte] = tor->blockInfo().byteSpanForPiece(piece);
    auto const [begin_block, end_block] = tor->blockSpanForPiece(piece);
    auto n_bytes_checked = size_t{};
    for (auto block = begin_block; block < end_block; ++block)
    {
        auto const block_loc = tor->blockLoc(block);
        auto const block_len = tor->blockSize(block);
        if (auto const success = cache->readBlock(tor, block_loc, block_len, std::data(buffer)) == 0; !success)
        {
            return {};
        }

        auto begin = std::data(buffer);
        auto end = begin + block_len;

        // handle edge case where blocks aren't on piece boundaries:
        if (block == begin_block) // `block` may begin before `piece` does
        {
            begin += (begin_byte - block_loc.byte);
        }
        if (block + 1 == end_block) // `block` may end after `piece` does
        {
            end -= (block_loc.byte + block_len - end_byte);
        }

        sha->add(begin, end - begin);
        n_bytes_checked += (end - begin);
    }

    TR_ASSERT(tor->pieceSize(piece) == n_bytes_checked);
    return sha->finish();
}

} // namespace

int tr_ioRead(tr_torrent* tor, tr_block_info::Location loc, size_t len, uint8_t* setme)
{
    return readOrWritePiece(tor, IoMode::Read, loc, setme, len);
}

int tr_ioPrefetch(tr_torrent* tor, tr_block_info::Location loc, size_t len)
{
    return readOrWritePiece(tor, IoMode::Prefetch, loc, nullptr, len);
}

int tr_ioWrite(tr_torrent* tor, tr_block_info::Location loc, size_t len, uint8_t const* writeme)
{
    return readOrWritePiece(tor, IoMode::Write, loc, const_cast<uint8_t*>(writeme), len);
}

bool tr_ioTestPiece(tr_torrent* tor, tr_piece_index_t piece)
{
    auto const hash = recalculateHash(tor, piece);
    return hash && *hash == tor->pieceHash(piece);
}
