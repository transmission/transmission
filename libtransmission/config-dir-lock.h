// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <string_view>

#include "libtransmission/lock-file.h"

/**
 * Marks a config dir as in use for as long as this object is alive.
 *
 * Everything a session writes to its config dir assumes a single owner.
 * A second session takes this lock to find out the dir is not its own,
 * and stops writing there when it finds the lock held.
 * See tr_session::mayWriteConfigDir().
 */
class tr_config_dir_lock
{
public:
    using Status = tr_lock_file::Status;

    explicit tr_config_dir_lock(std::string_view config_dir);
    tr_config_dir_lock(tr_config_dir_lock const&) = delete;
    tr_config_dir_lock& operator=(tr_config_dir_lock const&) = delete;
    tr_config_dir_lock(tr_config_dir_lock&&) = delete;
    tr_config_dir_lock& operator=(tr_config_dir_lock&&) = delete;
    ~tr_config_dir_lock() = default;

    [[nodiscard]] bool is_held() const noexcept
    {
        return lock_.is_held();
    }

    [[nodiscard]] constexpr Status status() const noexcept
    {
        return lock_.status();
    }

private:
    tr_lock_file lock_;
};
