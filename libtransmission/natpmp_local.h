/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: natpmp.h 12204 2011-03-22 15:19:54Z jordan $
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_NATPMP_H
#define TR_NATPMP_H 1

/**
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

typedef struct tr_natpmp tr_natpmp;

tr_natpmp * tr_natpmpInit( void );

void tr_natpmpClose( tr_natpmp * );

int tr_natpmpPulse( tr_natpmp *, tr_port port, bool isEnabled, tr_port * public_port );

/* @} */
#endif
