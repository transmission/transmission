/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef SHARED_H
#define SHARED_H 1

#include "transmission.h"
#include "net.h"

/**
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

struct tr_bindsockets;

typedef struct tr_shared tr_shared;

tr_shared* tr_sharedInit( tr_session* );

void       tr_sharedClose( tr_session * );

void       tr_sharedPortChanged( tr_session * );

void       tr_sharedTraversalEnable( tr_shared *, tr_bool isEnabled );

tr_port    tr_sharedGetPeerPort( const tr_shared * s );

tr_bool    tr_sharedTraversalIsEnabled( const tr_shared * s );

int        tr_sharedTraversalStatus( const tr_shared * );

/** @} */
#endif
