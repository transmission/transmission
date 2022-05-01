// This file Copyright © 2010 Johannes Lieder.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

int tr_lpdInit(tr_session*, tr_address*);
void tr_lpdUninit(tr_session*);
bool tr_lpdSendAnnounce(tr_torrent const*);

/**
* @} */
