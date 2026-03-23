// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include <fmt/format.h>

#include "libtransmission/block-info.h" // tr_block_info
#include "libtransmission/crypto-utils.h"
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/inout.h"
#include "libtransmission/session.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/torrent.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h" // tr_pathbuf
#include "libtransmission/types.h"
#include "libtransmission/utils.h"

using namespace std::literals;

namespace
{

bool read_entire_buf(tr_sys_file_t const fd, uint64_t file_offset, std::span<uint8_t> buf, tr_error& error)
{
    while (!std::empty(buf))
    {
        auto n_read = uint64_t{};

        if (!tr_sys_file_read_at(fd, std::data(buf), std::size(buf), file_offset, &n_read, &error))
        {
            return false;
        }

        buf = buf.subspan(n_read);
        file_offset += n_read;
    }

    return true;
}

bool write_entire_buf(tr_sys_file_t const fd, uint64_t file_offset, std::span<uint8_t const> buf, tr_error& error)
{
    while (!std::empty(buf))
    {
        auto n_written = uint64_t{};

        if (!tr_sys_file_write_at(fd, std::data(buf), std::size(buf), file_offset, &n_written, &error))
        {
            return false;
        }

        buf = buf.subspan(n_written);
        file_offset += n_written;
    }

    return true;
}

[[nodiscard]] std::optional<tr_sys_file_t> get_fd(
    tr_session& session,
    tr_open_files& open_files,
    tr_torrent const& tor,
    bool const writable,
    tr_file_index_t const file_index,
    tr_error& error)
{
    auto const tor_id = tor.id();

    // is the file already open in the fd pool?
    if (auto const fd = open_files.get(tor_id, file_index, writable); fd)
    {
        return fd;
    }

    // does the file exist?
    auto const file_size = tor.file_size(file_index);
    auto const prealloc = writable && tor.file_is_wanted(file_index) ? session.preallocationMode() :
                                                                       tr_open_files::Preallocation::None;
    if (auto const found = tor.find_file(file_index); found)
    {
        return open_files.get(tor_id, file_index, writable, found->filename(), prealloc, file_size);
    }

    // do we want to create it?
    auto err = ENOENT;
    if (writable)
    {
        auto const base = tor.current_dir();
        auto const suffix = session.isIncompleteFileNamingEnabled() ? tr_torrent_files::PartialFileSuffix : ""sv;
        auto const filename = tr_pathbuf{ base, '/', tor.file_subpath(file_index), suffix };
        if (auto const fd = open_files.get(tor_id, file_index, writable, filename, prealloc, file_size); fd)
        {
            // make a note that we just created a file
            session.add_file_created();
            return fd;
        }

        err = errno;
    }

    error.set(
        err,
        fmt::format(
            fmt::runtime(_("Couldn't get '{path}': {error} ({error_code})")),
            fmt::arg("path", tor.file_subpath(file_index)),
            fmt::arg("error", tr_strerror(err)),
            fmt::arg("error_code", err)));
    return {};
}

void read_bytes(
    tr_session& session,
    tr_open_files& open_files,
    tr_torrent const& tor,
    tr_file_index_t const file_index,
    uint64_t const file_offset,
    std::span<uint8_t> buf,
    tr_error& error)
{
    TR_ASSERT(file_index < tor.file_count());
    auto const file_size = tor.file_size(file_index);
    TR_ASSERT(file_size == 0U || file_offset < file_size);
    TR_ASSERT(file_offset + std::size(buf) <= file_size);
    if (file_size == 0U)
    {
        return;
    }

    auto const fd = get_fd(session, open_files, tor, false, file_index, error);
    if (!fd || error)
    {
        return;
    }

    read_entire_buf(*fd, file_offset, buf, error);

    if (error)
    {
        tr_logAddErrorTor(
            &tor,
            fmt::format(
                fmt::runtime(_("Couldn't read '{path}': {error} ({error_code})")),
                fmt::arg("path", tor.file_subpath(file_index)),
                fmt::arg("error", error.message()),
                fmt::arg("error_code", error.code())));
    }
}

void write_bytes(
    tr_session& session,
    tr_open_files& open_files,
    tr_torrent const& tor,
    tr_file_index_t const file_index,
    uint64_t const file_offset,
    std::span<uint8_t const> buf,
    tr_error& error)
{
    TR_ASSERT(file_index < tor.file_count());
    auto const file_size = tor.file_size(file_index);
    TR_ASSERT(file_size == 0U || file_offset < file_size);
    TR_ASSERT(file_offset + std::size(buf) <= file_size);
    if (file_size == 0U)
    {
        return;
    }

    auto const fd = get_fd(session, open_files, tor, true, file_index, error);
    if (!fd || error)
    {
        return;
    }

    write_entire_buf(*fd, file_offset, buf, error);

    if (error)
    {
        tr_logAddErrorTor(
            &tor,
            fmt::format(
                fmt::runtime(_("Couldn't save '{path}': {error} ({error_code})")),
                fmt::arg("path", tor.file_subpath(file_index)),
                fmt::arg("error", error.message()),
                fmt::arg("error_code", error.code())));
    }
}

std::optional<tr_sha1_digest_t> recalculate_hash(tr_torrent const& tor, tr_piece_index_t const piece)
{
    TR_ASSERT(piece < tor.piece_count());

    auto sha = tr_sha1{};
    auto buffer = std::array<uint8_t, tr_block_info::BlockSize>{};

    auto const [begin_byte, end_byte] = tor.block_info().byte_span_for_piece(piece);
    auto const [begin_block, end_block] = tor.block_span_for_piece(piece);
    [[maybe_unused]] auto n_bytes_checked = size_t{};
    for (auto block = begin_block; block < end_block; ++block)
    {
        auto const block_loc = tor.block_loc(block);
        auto const block_len = tor.block_size(block);
        auto contents = std::span{ std::data(buffer), block_len };
        if (auto const success = tr_ioRead(tor, block_loc, contents) == 0; !success)
        {
            return {};
        }

        // Handle edge cases where blocks aren't perfectly aligned on piece boundaries.
        // `std::max` ensures we don't start hashing before the piece begins (for the first block).
        // `std::min` ensures we don't hash past the end of the piece (for the last block).
        auto const start = std::max(begin_byte, block_loc.byte);
        auto const end = std::min(end_byte, block_loc.byte + block_len);
        auto const piece_data = contents.subspan(start - block_loc.byte, static_cast<size_t>(end - start));

        sha.add(std::data(piece_data), std::size(piece_data));
        n_bytes_checked += std::size(piece_data);
    }

    TR_ASSERT(tor.piece_size(piece) == n_bytes_checked);
    return sha.finish();
}

} // namespace

tr_error_code_t tr_ioRead(tr_torrent const& tor, tr_block_info::Location const& loc, std::span<uint8_t> const setme)
{
    auto error = tr_error{};
    if (loc.piece >= tor.piece_count())
    {
        error.set_from_errno(EINVAL);
        return error.code();
    }

    auto [file_index, file_offset] = tor.file_offset(loc);
    auto& session = *tor.session;
    auto& open_files = session.openFiles();
    auto buf = setme;
    while (!std::empty(buf) && !error)
    {
        auto const bytes_this_pass = std::min<uint64_t>(std::size(buf), tor.file_size(file_index) - file_offset);
        read_bytes(session, open_files, tor, file_index, file_offset, buf.first(bytes_this_pass), error);
        buf = buf.subspan(bytes_this_pass);
        ++file_index;
        file_offset = 0U;
    }

    return error.code();
}

tr_error_code_t tr_ioWrite(tr_torrent& tor, tr_block_info::Location const& loc, std::span<uint8_t const> const writeme)
{
    auto error = tr_error{};
    if (loc.piece >= tor.piece_count())
    {
        error.set_from_errno(EINVAL);
    }
    else
    {
        auto [file_index, file_offset] = tor.file_offset(loc);
        auto& session = *tor.session;
        auto& open_files = session.openFiles();
        auto buf = writeme;
        while (!std::empty(buf) && !error)
        {
            auto const bytes_this_pass = std::min<uint64_t>(std::size(buf), tor.file_size(file_index) - file_offset);
            write_bytes(session, open_files, tor, file_index, file_offset, buf.first(bytes_this_pass), error);
            buf = buf.subspan(bytes_this_pass);
            ++file_index;
            file_offset = 0U;
        }
    }

    // if IO failed, set torrent's error if not already set
    if (error && !tor.error().is_local_error())
    {
        tor.error().set_local_error(error.message());
        tr_torrentStop(&tor);
    }

    return error.code();
}

bool tr_ioTestPiece(tr_torrent const& tor, tr_piece_index_t const piece)
{
    auto const hash = recalculate_hash(tor, piece);
    return hash && *hash == tor.piece_hash(piece);
}
