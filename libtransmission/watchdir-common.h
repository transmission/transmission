// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_WATCHDIR_MODULE
#error only the libtransmission watchdir module should #include this header.
#endif

#include <string>
#include <unordered_set>

struct tr_watchdir_backend
{
    void (*free_func)(struct tr_watchdir_backend*);
};

#define BACKEND_DOWNCAST(b) (reinterpret_cast<tr_watchdir_backend*>(b))

/* ... */

tr_watchdir_backend* tr_watchdir_get_backend(tr_watchdir_t handle);

struct event_base* tr_watchdir_get_event_base(tr_watchdir_t handle);

/* ... */

void tr_watchdir_process(tr_watchdir_t handle, char const* name);

void tr_watchdir_scan(tr_watchdir_t handle, std::unordered_set<std::string>* dir_entries);

/* ... */

tr_watchdir_backend* tr_watchdir_generic_new(tr_watchdir_t handle);

#ifdef WITH_INOTIFY
tr_watchdir_backend* tr_watchdir_inotify_new(tr_watchdir_t handle);
#endif
#ifdef WITH_KQUEUE
tr_watchdir_backend* tr_watchdir_kqueue_new(tr_watchdir_t handle);
#endif
#ifdef _WIN32
tr_watchdir_backend* tr_watchdir_win32_new(tr_watchdir_t handle);
#endif
