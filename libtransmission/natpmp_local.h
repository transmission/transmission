// This file Copyright (C) 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

/**
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

struct tr_natpmp;

tr_natpmp* tr_natpmpInit(void);

void tr_natpmpClose(tr_natpmp*);

tr_port_forwarding tr_natpmpPulse(tr_natpmp*, tr_port port, bool isEnabled, tr_port* public_port);

/* @} */
