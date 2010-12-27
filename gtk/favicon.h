/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef GTR_FAVICON_CACHE_H
#define GTR_FAVICON_CACHE_H

#include <gtk/gtk.h>
#include <libtransmission/transmission.h>

void gtr_get_favicon( tr_session  * session,
                      const char  * host,
                      GFunc         pixbuf_ready_func,
                      gpointer      pixbuf_ready_func_data );

void gtr_get_favicon_from_url( tr_session  * session,
                               const char  * url,
                               GFunc         pixbuf_ready_func,
                               gpointer      pixbuf_ready_func_data );


#endif
