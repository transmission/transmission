/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
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

#ifndef TR_WEBSEED_H
#define TR_WEBSEED_H

typedef struct tr_webseed tr_webseed;

#include "peer-common.h"

tr_webseed* tr_webseedNew( struct tr_torrent * torrent,
                           const char *        url,
                           tr_delivery_func    delivery_func,
                           void *              delivery_userdata );

void        tr_webseedFree( tr_webseed * );

tr_addreq_t tr_webseedAddRequest( tr_webseed *     w,
                                  uint32_t         index,
                                  uint32_t         offset,
                                  uint32_t         length );

/** @return true if a request is being processed, or false if idle */
int         tr_webseedGetSpeed( const tr_webseed * w,
                                float *            setme_KiBs );

/** @return true if a request is being processed, or false if idle */
int         tr_webseedIsActive( const tr_webseed * w );


#endif
