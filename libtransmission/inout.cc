// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <optional>
#include <vector>

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

    bool const do_write = io_mode == IoMode::Write;
    auto const file_size = tor->fileSize(file_index);
    TR_ASSERT(file_size == 0 || file_offset < file_size);
    TR_ASSERT(file_offset + buflen <= file_size);

    if (file_size == 0)
    {
        return 0;
    }

    // --- Find the fd

    auto fd = session->openFiles().get(tor->id(), file_index, do_write);
    auto filename = tr_pathbuf{};
    if (!fd && !getFilename(filename, tor, file_index, io_mode))
    {
        return ENOENT;
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
        int const err = errno;
        tr_logAddErrorTor(
            tor,
            fmt::format(
                _("Couldn't get '{path}': {error} ({error_code})"),
                fmt::arg("path", filename),
                fmt::arg("error", tr_strerror(err)),
                fmt::arg("error_code", err)));
        return err;
    }

    switch (io_mode)
    {
    case IoMode::Read:
        if (tr_error* error = nullptr; !readEntireBuf(*fd, file_offset, buf, buflen, &error) && error != nullptr)
        {
            auto const err = error->code;
            tr_logAddErrorTor(
                tor,
                fmt::format(
                    _("Couldn't read '{path}': {error} ({error_code})"),
                    fmt::arg("path", tor->fileSubpath(file_index)),
                    fmt::arg("error", error->message),
                    fmt::arg("error_code", error->code)));
            tr_error_free(error);
            return err;
        }
        break;

    case IoMode::Write:
        if (tr_error* error = nullptr; !writeEntireBuf(*fd, file_offset, buf, buflen, &error) && error != nullptr)
        {
            auto const err = error->code;
            tr_logAddErrorTor(
                tor,
                fmt::format(
                    _("Couldn't save '{path}': {error} ({error_code})"),
                    fmt::arg("path", tor->fileSubpath(file_index)),
                    fmt::arg("error", error->message),
                    fmt::arg("error_code", error->code)));
            tr_error_free(error);
            return err;
        }
        break;

    case IoMode::Prefetch:
        tr_sys_file_advise(*fd, file_offset, buflen, TR_SYS_FILE_ADVICE_WILL_NEED);
        break;
    }

    return 0;
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
        if (buf != nullptr)
        {
            buf += bytes_this_pass;
        }
        buflen -= bytes_this_pass;

        if (err != 0 && io_mode == IoMode::Write && tor->error != TR_STAT_LOCAL_ERROR)
        {
            auto const path = tr_pathbuf{ tor->downloadDir(), '/', tor->fileSubpath(file_index) };
            tor->setLocalError(fmt::format(FMT_STRING("{:s} ({:s})"), tr_strerror(err), path));
            tr_torrentStop(tor);
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

    auto bytes_left = tor->pieceSize(piece);
    auto loc = tor->pieceLoc(piece);
    tr_ioPrefetch(tor, loc, bytes_left);

    auto sha = tr_sha1::create();
    auto buffer = std::vector<uint8_t>(tr_block_info::BlockSize);
    while (bytes_left != 0)
    {
        auto const len = static_cast<uint32_t>(std::min(static_cast<size_t>(bytes_left), std::size(buffer)));
        if (auto const success = tor->session->cache->readBlock(tor, loc, len, std::data(buffer)) == 0; !success)
        {
            return {};
        }

        sha->add(std::data(buffer), len);
        loc = tor->byteLoc(loc.byte + len);
        bytes_left -= len;
    }

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
