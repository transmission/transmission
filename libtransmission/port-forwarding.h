/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef SHARED_H
#define SHARED_H 1

#include "transmission.h"

/**
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

struct tr_bindsockets;

typedef struct tr_shared tr_shared;

tr_shared* tr_sharedInit (tr_session*);

void       tr_sharedClose (tr_session *);

void       tr_sharedPortChanged (tr_session *);

void       tr_sharedTraversalEnable (tr_shared *, bool isEnabled);

tr_port    tr_sharedGetPeerPort (const tr_shared * s);

bool       tr_sharedTraversalIsEnabled (const tr_shared * s);

int        tr_sharedTraversalStatus (const tr_shared *);

/** @} */
#endif
