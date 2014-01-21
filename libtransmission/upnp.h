/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_UPNP_H
#define TR_UPNP_H 1

/**
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

typedef struct tr_upnp tr_upnp;

tr_upnp * tr_upnpInit (void);

void      tr_upnpClose (tr_upnp *);

int       tr_upnpPulse (      tr_upnp *,
                            int port,
                            int isEnabled,
                            int doPortCheck);
/* @} */
#endif
