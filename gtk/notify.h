/*
 * This file Copyright (C) 2008-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <glibmm.h>

class TrCore;

void gtr_notify_init();

void gtr_notify_torrent_added(Glib::RefPtr<TrCore> const& core, int torrent_id);

void gtr_notify_torrent_completed(Glib::RefPtr<TrCore> const& core, int torrent_id);
