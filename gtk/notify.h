/*
 * This file Copyright (C) 2008-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <glibmm.h>

typedef struct _TrCore TrCore;

void gtr_notify_init();

void gtr_notify_torrent_added(Glib::ustring const& name);

void gtr_notify_torrent_completed(TrCore* core, int torrent_id);
