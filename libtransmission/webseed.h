/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

typedef struct tr_webseed tr_webseed;

#include <string_view>

#include "peer-common.h"

tr_peer* tr_webseedNew(struct tr_torrent* torrent, std::string_view, tr_peer_callback callback, void* callback_data);
