// This file Copyright 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

struct tr_session;
struct tr_torrent;

/**
 * @addtogroup file_io File IO
 * @{
 */

using tr_verify_done_func = void (*)(tr_torrent*, bool aborted, void* user_data);

void tr_verifyAdd(tr_torrent* tor, tr_verify_done_func callback_func, void* callback_data);

void tr_verifyRemove(tr_torrent* tor);

void tr_verifyClose(tr_session*);

/* @} */
