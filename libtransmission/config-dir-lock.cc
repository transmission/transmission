// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>
#include <utility>

#include "libtransmission/config-dir-lock.h"
#include "libtransmission/file.h"
#include "libtransmission/tr-strbuf.h"

using namespace std::literals;

namespace
{
// Only the file's identity matters, never its contents, so every version of
// Transmission agreeing on the name is the whole requirement.
auto constexpr LockFilename = "lock"sv;
} // namespace

tr_config_dir_lock tr_config_dir_lock::create(std::string_view config_dir)
{
    auto const filename = tr_pathbuf{ config_dir, '/', LockFilename };

    auto const handle = tr_sys_file_open(filename, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);
    if (handle == TR_BAD_SYS_FILE)
    {
        return {};
    }

    if (!tr_sys_file_lock(handle, TR_SYS_FILE_LOCK_EX | TR_SYS_FILE_LOCK_NB))
    {
        tr_sys_file_close(handle);
        return {};
    }

    return tr_config_dir_lock{ handle };
}

tr_config_dir_lock::tr_config_dir_lock(tr_config_dir_lock&& that) noexcept
    : handle_{ std::exchange(that.handle_, TR_BAD_SYS_FILE) }
{
}

tr_config_dir_lock& tr_config_dir_lock::operator=(tr_config_dir_lock&& that) noexcept
{
    if (this != &that)
    {
        close();
        handle_ = std::exchange(that.handle_, TR_BAD_SYS_FILE);
    }

    return *this;
}

tr_config_dir_lock::~tr_config_dir_lock()
{
    close();
}

void tr_config_dir_lock::close() noexcept
{
    if (handle_ != TR_BAD_SYS_FILE)
    {
        // Closing the descriptor releases the lock.
        tr_sys_file_close(handle_);
        handle_ = TR_BAD_SYS_FILE;
    }
}
