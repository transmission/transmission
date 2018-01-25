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

#include "transmission.h"

/**
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

struct tr_bindsockets;

typedef struct tr_shared tr_shared;

tr_shared* tr_sharedInit(tr_session*);

void tr_sharedClose(tr_session*);

void tr_sharedPortChanged(tr_session*);

void tr_sharedTraversalEnable(tr_shared*, bool isEnabled);

tr_port tr_sharedGetPeerPort(tr_shared const* s);

bool tr_sharedTraversalIsEnabled(tr_shared const* s);

int tr_sharedTraversalStatus(tr_shared const*);

/** @} */
