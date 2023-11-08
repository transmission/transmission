// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <optional>
#include <string_view>
#include <utility> // std::move

#include <fmt/core.h>

#include "libtransmission/transmission.h"

#include "libtransmission/block-info.h" // tr_block_info
#include "libtransmission/crypto-utils.h"
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/inout.h"
#include "libtransmission/session.h"
#include "libtransmission/torrent.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-macros.h" // tr_sha1_digest_t
#include "libtransmission/tr-strbuf.h" // tr_pathbuf
#include "libtransmission/utils.h"

using namespace std::literals;

namespace
{

bool readEntireBuf(tr_sys_file_t fd, uint64_t file_offset, uint8_t* buf, uint64_t buflen, tr_error* error)
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

bool writeEntireBuf(tr_sys_file_t fd, uint64_t file_offset, uint8_t const* buf, uint64_t buflen, tr_error* error)
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
    if (auto found = tor->find_file(file_index); found)
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
    auto const base = tor->current_dir();
    auto const suffix = tor->session->isIncompleteFileNamingEnabled() ? tr_torrent_files::PartialFileSuffix : ""sv;
    setme.assign(base, '/', tor->file_subpath(file_index), suffix);
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
    tr_error* error)
{
    TR_ASSERT(file_index < tor->file_count());

    bool const do_write = io_mode == IoMode::Write;
    auto const file_size = tor->file_size(file_index);
    TR_ASSERT(file_size == 0 || file_offset < file_size);
    TR_ASSERT(file_offset + buflen <= file_size);

    if (file_size == 0)
    {
        return;
    }

    auto local_error = tr_error{};
    if (error == nullptr)
    {
        error = &local_error;
    }

    // --- Find the fd

    auto fd_top = session->openFiles().get(tor->id(), file_index, do_write);
    auto filename = tr_pathbuf{};
    if (!fd_top && !getFilename(filename, tor, file_index, io_mode))
    {
        auto const err = ENOENT;
        error->set(
            err,
            fmt::format(
                _("Couldn't get '{path}': {error} ({error_code})"),
                fmt::arg("path", tor->file_subpath(file_index)),
                fmt::arg("error", tr_strerror(err)),
                fmt::arg("error_code", err)));
        return;
    }

    if (!fd_top) // not in the cache, so open or create it now
    {
        // open (and maybe create) the file
        auto const prealloc = (!do_write || !tor->file_is_wanted(file_index)) ? TR_PREALLOCATE_NONE :
                                                                                tor->session->preallocationMode();
        fd_top = session->openFiles().get(tor->id(), file_index, do_write, filename, prealloc, file_size);
        if (fd_top && do_write)
        {
            // make a note that we just created a file
            tor->session->add_file_created();
        }
    }

    if (!fd_top) // couldn't create/open it either
    {
        auto const errnum = errno;
        error->set(
            errnum,
            fmt::format(
                _("Couldn't get '{path}': {error} ({error_code})"),
                fmt::arg("path", filename),
                fmt::arg("error", tr_strerror(errnum)),
                fmt::arg("error_code", errnum)));
        tr_logAddErrorTor(tor, std::string{ error->message() });
        return;
    }

    auto const& [fd, tag] = *fd_top;
    switch (io_mode)
    {
    case IoMode::Read:
        if (!readEntireBuf(fd, file_offset, buf, buflen, error) && *error)
        {
            tr_logAddErrorTor(
                tor,
                fmt::format(
                    _("Couldn't read '{path}': {error} ({error_code})"),
                    fmt::arg("path", tor->file_subpath(file_index)),
                    fmt::arg("error", error->message()),
                    fmt::arg("error_code", error->code())));
            return;
        }
        break;

    case IoMode::Write:
        if (!writeEntireBuf(fd, file_offset, buf, buflen, error) && *error)
        {
            tr_logAddErrorTor(
                tor,
                fmt::format(
                    _("Couldn't save '{path}': {error} ({error_code})"),
                    fmt::arg("path", tor->file_subpath(file_index)),
                    fmt::arg("error", error->message()),
                    fmt::arg("error_code", error->code())));
            return;
        }
        break;

    case IoMode::Prefetch:
        tr_sys_file_advise(fd, file_offset, buflen, TR_SYS_FILE_ADVICE_WILL_NEED);
        break;
    }
}

/* returns 0 on success, or an errno on failure */
int readOrWritePiece(tr_torrent* tor, IoMode io_mode, tr_block_info::Location loc, uint8_t* buf, size_t buflen)
{
    if (loc.piece >= tor->piece_count())
    {
        return EINVAL;
    }

    auto [file_index, file_offset] = tor->file_offset(loc, false);

    while (buflen != 0)
    {
        uint64_t const bytes_this_pass = std::min(uint64_t{ buflen }, uint64_t{ tor->file_size(file_index) - file_offset });

        auto error = tr_error{};
        readOrWriteBytes(tor->session, tor, io_mode, file_index, file_offset, buf, bytes_this_pass, &error);
        if (error) // if IO failed, set torrent's error if not already set
        {
            if (io_mode == IoMode::Write && tor->error().error_type() != TR_STAT_LOCAL_ERROR)
            {
                tor->error().set_local_error(error.message());
                tr_torrentStop(tor);
            }

            return error.code();
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
    TR_ASSERT(piece < tor->piece_count());

    auto sha = tr_sha1::create();
    auto buffer = std::array<uint8_t, tr_block_info::BlockSize>{};

    auto& cache = tor->session->cache;
    auto const [begin_byte, end_byte] = tor->block_info().byte_span_for_piece(piece);
    auto const [begin_block, end_block] = tor->block_span_for_piece(piece);
    [[maybe_unused]] auto n_bytes_checked = size_t{};
    for (auto block = begin_block; block < end_block; ++block)
    {
        auto const block_loc = tor->block_loc(block);
        auto const block_len = tor->block_size(block);
        if (auto const success = cache->read_block(tor, block_loc, block_len, std::data(buffer)) == 0; !success)
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

    TR_ASSERT(tor->piece_size(piece) == n_bytes_checked);
    return sha->finish();
}

} // namespace

int tr_ioRead(tr_torrent* tor, tr_block_info::Location const& loc, size_t len, uint8_t* setme)
{
    return readOrWritePiece(tor, IoMode::Read, loc, setme, len);
}

int tr_ioPrefetch(tr_torrent* tor, tr_block_info::Location const& loc, size_t len)
{
    return readOrWritePiece(tor, IoMode::Prefetch, loc, nullptr, len);
}

int tr_ioWrite(tr_torrent* tor, tr_block_info::Location const& loc, size_t len, uint8_t const* writeme)
{
    return readOrWritePiece(tor, IoMode::Write, loc, const_cast<uint8_t*>(writeme), len);
}

bool tr_ioTestPiece(tr_torrent* tor, tr_piece_index_t piece)
{
    auto const hash = recalculateHash(tor, piece);
    return hash && *hash == tor->piece_hash(piece);
}
