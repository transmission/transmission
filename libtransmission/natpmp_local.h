// This file Copyright © 2007-2022 Mnemosyne LLC.
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

#include <ctime> // time_t

#include "natpmp.h"
#include "net.h" // tr_port

enum tr_natpmp_state
{
    TR_NATPMP_IDLE,
    TR_NATPMP_ERR,
    TR_NATPMP_DISCOVER,
    TR_NATPMP_RECV_PUB,
    TR_NATPMP_SEND_MAP,
    TR_NATPMP_RECV_MAP,
    TR_NATPMP_SEND_UNMAP,
    TR_NATPMP_RECV_UNMAP
};

struct tr_natpmp
{
    bool has_discovered;
    bool is_mapped;

    tr_port public_port;
    tr_port private_port;

    time_t renew_time;
    time_t command_time;
    tr_natpmp_state state;
    natpmp_t natpmp;
};

tr_natpmp* tr_natpmpInit(void);

void tr_natpmpClose(tr_natpmp*);

tr_port_forwarding tr_natpmpPulse(tr_natpmp*, tr_port port, bool is_enabled, tr_port* public_port, tr_port* real_private_port);

/* @} */
