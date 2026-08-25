// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#include "libtransmission/config-dir-lock.h"
#include "libtransmission/file.h"
#include "libtransmission/tr-strbuf.h"

using namespace std::literals;

namespace
{
// Sessions find each other by this name, so every version has to agree on it.
auto constexpr LockFilename = "lock"sv;
} // namespace

tr_config_dir_lock::tr_config_dir_lock(std::string_view config_dir)
{
    auto const filename = tr_pathbuf{ config_dir, '/', LockFilename };

    auto const handle = tr_sys_file_open(filename, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);
    if (handle == TR_BAD_SYS_FILE)
    {
        return;
    }

    if (!tr_sys_file_lock(handle, TR_SYS_FILE_LOCK_EX | TR_SYS_FILE_LOCK_NB))
    {
        tr_sys_file_close(handle);
        return;
    }

    handle_ = handle;
}

tr_config_dir_lock::~tr_config_dir_lock()
{
    if (handle_ != TR_BAD_SYS_FILE)
    {
        // Closing the descriptor releases the lock.
        tr_sys_file_close(handle_);
    }
}
