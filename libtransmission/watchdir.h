// This file Copyright 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

struct event_base;

using tr_watchdir_t = struct tr_watchdir*;

enum tr_watchdir_status
{
    TR_WATCHDIR_ACCEPT,
    TR_WATCHDIR_IGNORE,
    TR_WATCHDIR_RETRY
};

using tr_watchdir_cb = tr_watchdir_status (*)(tr_watchdir_t handle, char const* name, void* user_data);

/* ... */

tr_watchdir_t tr_watchdir_new(
    std::string_view path,
    tr_watchdir_cb callback,
    void* callback_user_data,
    struct event_base* event_base,
    bool force_generic);

void tr_watchdir_free(tr_watchdir_t handle);

char const* tr_watchdir_get_path(tr_watchdir_t handle);
