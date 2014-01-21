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

#ifndef TR_WEBSEED_H
#define TR_WEBSEED_H

typedef struct tr_webseed tr_webseed;

#include "peer-common.h"

tr_webseed* tr_webseedNew (struct tr_torrent * torrent,
                           const char        * url,
                           tr_peer_callback    callback,
                           void              * callback_data);

#endif
