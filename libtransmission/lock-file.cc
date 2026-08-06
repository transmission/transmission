// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/lock-file.h"
#include "libtransmission/error.h"
#include "libtransmission/file.h"

bool tr_lock_file::try_acquire()
{
    if (is_held())
    {
        return true;
    }

    auto const handle = tr_sys_file_open(filename_, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600);
    if (handle == TR_BAD_SYS_FILE)
    {
        status_ = Status::Unavailable;
        return false;
    }

    auto error = tr_error{};
    if (!tr_sys_file_lock(handle, TR_SYS_FILE_LOCK_EX | TR_SYS_FILE_LOCK_NB, &error))
    {
        // Close what this attempt opened. The descriptor holds no lock,
        // so closing it cannot disturb the one its holder owns.
        status_ = tr_sys_file_lock_error_is_contended(error) ? Status::Contended : Status::Unavailable;
        tr_sys_file_close(handle);
        return false;
    }

    handle_ = handle;
    status_ = Status::Acquired;
    return true;
}

void tr_lock_file::release()
{
    if (handle_ != TR_BAD_SYS_FILE)
    {
        // Closing the descriptor releases the lock.
        tr_sys_file_close(handle_);
        handle_ = TR_BAD_SYS_FILE;
    }
}
