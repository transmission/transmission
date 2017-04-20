/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "tr-core.h"

void gtr_notify_init(void);

void gtr_notify_torrent_added(char const* name);

void gtr_notify_torrent_completed(TrCore* core, int torrent_id);
