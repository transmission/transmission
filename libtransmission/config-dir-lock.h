// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <string_view>

#include "libtransmission/file.h"

/**
 * Marks a config dir as in use for as long as this object is alive.
 *
 * A session's settings, resume files and stats are written as though they have
 * a single owner, so two sessions sharing a config dir overwrite each other's
 * state. Taking this lock lets the second one find out.
 *
 * The lock lives on an open file descriptor, so the kernel releases it however
 * the process ends. A killed session leaves nothing behind to clean up.
 *
 * Failing to acquire the lock is not proof that another session is running:
 * a read-only config dir, or one on a filesystem that does not support
 * locking, cannot be locked either. Treat the failure as "unknown" rather
 * than as "in use".
 */
class tr_config_dir_lock
{
public:
    tr_config_dir_lock() = default;
    tr_config_dir_lock(tr_config_dir_lock const&) = delete;
    tr_config_dir_lock& operator=(tr_config_dir_lock const&) = delete;
    tr_config_dir_lock(tr_config_dir_lock&& that) noexcept;
    tr_config_dir_lock& operator=(tr_config_dir_lock&& that) noexcept;
    ~tr_config_dir_lock();

    [[nodiscard]] static tr_config_dir_lock create(std::string_view config_dir);

    [[nodiscard]] bool is_held() const noexcept
    {
        return handle_ != TR_BAD_SYS_FILE;
    }

private:
    explicit tr_config_dir_lock(tr_sys_file_t handle) noexcept
        : handle_{ handle }
    {
    }

    void close() noexcept;

    tr_sys_file_t handle_ = TR_BAD_SYS_FILE;
};
