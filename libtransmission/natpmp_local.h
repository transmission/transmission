/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

/**
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

#include "natpmp.h"
typedef struct tr_natpmp tr_natpmp;

tr_natpmp* tr_natpmpInit(void);

void tr_natpmpClose(tr_natpmp*);

int tr_natpmpPulse(tr_natpmp*, tr_port port, bool isEnabled, tr_port* public_port, tr_port* real_private_port);

typedef enum
{
    TR_NATPMP_IDLE,
    TR_NATPMP_ERR,
    TR_NATPMP_DISCOVER,
    TR_NATPMP_RECV_PUB,
    TR_NATPMP_SEND_MAP,
    TR_NATPMP_RECV_MAP,
    TR_NATPMP_SEND_UNMAP,
    TR_NATPMP_RECV_UNMAP
}
tr_natpmp_state;

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

/* @} */
