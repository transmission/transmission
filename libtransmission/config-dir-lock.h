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
 * is_held() is false for two causes that cannot be told apart: another session
 * holds the dir, or it could not be locked at all -- a read-only config dir, or
 * one on a filesystem without locking.
 */
class tr_config_dir_lock
{
public:
    explicit tr_config_dir_lock(std::string_view config_dir);
    tr_config_dir_lock(tr_config_dir_lock const&) = delete;
    tr_config_dir_lock& operator=(tr_config_dir_lock const&) = delete;
    tr_config_dir_lock(tr_config_dir_lock&&) = delete;
    tr_config_dir_lock& operator=(tr_config_dir_lock&&) = delete;
    ~tr_config_dir_lock();

    // Not constexpr: on Windows TR_BAD_SYS_FILE is a cast that no constant
    // expression may perform.
    [[nodiscard]] bool is_held() const noexcept
    {
        return handle_ != TR_BAD_SYS_FILE;
    }

private:
    tr_sys_file_t handle_ = TR_BAD_SYS_FILE;
};
