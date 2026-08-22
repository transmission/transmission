// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <string_view>

#include "libtransmission/file.h"
#include "libtransmission/tr-strbuf.h"

/**
 * A nonblocking exclusive lock on one named file, created if absent.
 *
 * The lock lives on an open file descriptor, so the kernel releases it however the process ends.
 * A killed holder leaves nothing to clean up, and the next process can lock the file it left behind.
 */
class tr_lock_file
{
public:
    enum class Status : uint8_t
    {
        Acquired,
        Contended, // another process holds it
        Unavailable // could not be locked at all: unwritable, or a filesystem without locking
    };

    explicit tr_lock_file(std::string_view const filename)
        : filename_{ filename }
    {
    }

    tr_lock_file(tr_lock_file const&) = delete;
    tr_lock_file& operator=(tr_lock_file const&) = delete;
    tr_lock_file(tr_lock_file&&) = delete;
    tr_lock_file& operator=(tr_lock_file&&) = delete;

    ~tr_lock_file()
    {
        release();
    }

    // Safe to call again after a failure or a release.
    [[nodiscard]] bool try_acquire();

    void release();

    [[nodiscard]] bool is_held() const noexcept
    {
        return handle_ != TR_BAD_SYS_FILE;
    }

    // The outcome of the most recent try_acquire(). release() does not change it.
    [[nodiscard]] constexpr Status status() const noexcept
    {
        return status_;
    }

private:
    tr_pathbuf const filename_;
    tr_sys_file_t handle_ = TR_BAD_SYS_FILE;
    Status status_ = Status::Unavailable;
};
