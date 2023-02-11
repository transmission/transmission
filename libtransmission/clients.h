// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t

#include "tr-macros.h" // tr_peer_id_t

/**
 * @brief parse a peer-id into a human-readable client name and version number
 * @ingroup utils
 */
void tr_clientForId(char* buf, size_t buflen, tr_peer_id_t peer_id);
