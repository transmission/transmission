// This file Copyright © 2005-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstdint> // uint8_t, uint64_t
#include <string_view>
#include <utility>

#include <fmt/core.h>

#include "transmission.h"

#include "error-types.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "open-files.h"
#include "tr-assert.h"
#include "tr-strbuf.h"
#include "utils.h" // _()

namespace
{

[[nodiscard]] auto isOpen(tr_sys_file_t fd) noexcept
{
    return fd != TR_BAD_SYS_FILE;
}

bool preallocate_file_sparse(tr_sys_file_t fd, uint64_t length, tr_error** error)
{
    tr_error* my_error = nullptr;

    if (length == 0)
    {
        return true;
    }

    if (tr_sys_file_preallocate(fd, length, TR_SYS_FILE_PREALLOC_SPARSE, &my_error))
    {
        return true;
    }

    tr_logAddDebug(fmt::format("Fast preallocation failed: {} ({})", my_error->message, my_error->code));

    if (!TR_ERROR_IS_ENOSPC(my_error->code))
    {
        char const zero = '\0';

        tr_error_clear(&my_error);

        /* fallback: the old-style seek-and-write */
        if (tr_sys_file_write_at(fd, &zero, 1, length - 1, nullptr, &my_error) && tr_sys_file_truncate(fd, length, &my_error))
        {
            return true;
        }

        tr_logAddDebug(fmt::format("Fast prellocation fallback failed: {} ({})", my_error->message, my_error->code));
    }

    tr_error_propagate(error, &my_error);
    return false;
}

bool preallocate_file_full(tr_sys_file_t fd, uint64_t length, tr_error** error)
{
    tr_error* my_error = nullptr;

    if (length == 0)
    {
        return true;
    }

    if (tr_sys_file_preallocate(fd, length, 0, &my_error))
    {
        return true;
    }

    tr_logAddDebug(fmt::format("Full preallocation failed: {} ({})", my_error->message, my_error->code));

    if (!TR_ERROR_IS_ENOSPC(my_error->code))
    {
        auto buf = std::array<uint8_t, 4096>{};
        bool success = true;

        tr_error_clear(&my_error);

        /* fallback: the old-fashioned way */
        while (success && length > 0)
        {
            uint64_t const this_pass = std::min(length, uint64_t{ std::size(buf) });
            uint64_t bytes_written = 0;
            success = tr_sys_file_write(fd, std::data(buf), this_pass, &bytes_written, &my_error);
            length -= bytes_written;
        }

        if (success)
        {
            return true;
        }

        tr_logAddDebug(fmt::format("Full preallocation fallback failed: {} ({})", my_error->message, my_error->code));
    }

    tr_error_propagate(error, &my_error);
    return false;
}

} // unnamed namespace

// ---

std::optional<tr_sys_file_t> tr_open_files::get(tr_torrent_id_t tor_id, tr_file_index_t file_num, bool writable)
{
    if (auto* const found = pool_.get(makeKey(tor_id, file_num)); found != nullptr)
    {
        if (writable && !found->writable_)
        {
            return {};
        }

        return found->fd_;
    }

    return {};
}

std::optional<tr_sys_file_t> tr_open_files::get(
    tr_torrent_id_t tor_id,
    tr_file_index_t file_num,
    bool writable,
    std::string_view filename_in,
    tr_preallocation_mode allocation,
    uint64_t file_size)
{
    // is there already an entry
    auto key = makeKey(tor_id, file_num);
    if (auto* const found = pool_.get(key); found != nullptr)
    {
        if (!writable || found->writable_)
        {
            return found->fd_;
        }

        pool_.erase(key); // close so we can re-open as writable
    }

    // create subfolders, if any
    auto const filename = tr_pathbuf{ filename_in };
    tr_error* error = nullptr;
    if (writable)
    {
        auto dir = tr_pathbuf{ filename.sv() };
        dir.popdir();
        if (!tr_sys_dir_create(dir, TR_SYS_DIR_CREATE_PARENTS, 0777, &error))
        {
            tr_logAddError(fmt::format(
                _("Couldn't create '{path}': {error} ({error_code})"),
                fmt::arg("path", dir),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
            tr_error_free(error);
            return {};
        }
    }

    auto const info = tr_sys_path_get_info(filename);
    bool const already_existed = info && info->isFile();

    // we need write permissions to resize the file
    bool const resize_needed = already_existed && (file_size < info->size);
    writable |= resize_needed;

    // open the file
    int flags = writable ? (TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE) : 0;
    flags |= TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL;
    auto const fd = tr_sys_file_open(filename, flags, 0666, &error);
    if (!isOpen(fd))
    {
        tr_logAddError(fmt::format(
            _("Couldn't open '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_free(error);
        return {};
    }

    if (writable && !already_existed && allocation != TR_PREALLOCATE_NONE)
    {
        bool success = false;
        char const* type = nullptr;

        if (allocation == TR_PREALLOCATE_FULL)
        {
            success = preallocate_file_full(fd, file_size, &error);
            type = "full";
        }
        else if (allocation == TR_PREALLOCATE_SPARSE)
        {
            success = preallocate_file_sparse(fd, file_size, &error);
            type = "sparse";
        }

        TR_ASSERT(type != nullptr);

        if (!success)
        {
            tr_logAddError(fmt::format(
                _("Couldn't preallocate '{path}': {error} ({error_code})"),
                fmt::arg("path", filename),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
            tr_sys_file_close(fd);
            tr_error_free(error);
            return {};
        }

        tr_logAddDebug(fmt::format("Preallocated file '{}' ({}, size: {})", filename, type, file_size));
    }

    // If the file already exists and it's too large, truncate it.
    // This is a fringe case that happens if a torrent's been updated
    // and one of the updated torrent's files is smaller.
    // https://trac.transmissionbt.com/ticket/2228
    // https://bugs.launchpad.net/ubuntu/+source/transmission/+bug/318249
    if (resize_needed && !tr_sys_file_truncate(fd, file_size, &error))
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't truncate '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_sys_file_close(fd);
        tr_error_free(error);
        return {};
    }

    // cache it
    auto& entry = pool_.add(std::move(key));
    entry.fd_ = fd;
    entry.writable_ = writable;

    return fd;
}

void tr_open_files::closeAll()
{
    pool_.clear();
}

void tr_open_files::closeTorrent(tr_torrent_id_t tor_id)
{
    return pool_.erase_if([&tor_id](Key const& key, Val const& /*unused*/) { return key.first == tor_id; });
}

void tr_open_files::closeFile(tr_torrent_id_t tor_id, tr_file_index_t file_num)
{
    pool_.erase(makeKey(tor_id, file_num));
}

tr_open_files::Val::~Val()
{
    if (isOpen(fd_))
    {
        tr_sys_file_close(fd_);
    }
}
