/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <gtk/gtk.h>
#include <libtransmission/transmission.h>

void gtr_get_favicon(tr_session* session, char const* host, GFunc pixbuf_ready_func, gpointer pixbuf_ready_func_data);

void gtr_get_favicon_from_url(tr_session* session, char const* url, GFunc pixbuf_ready_func, gpointer pixbuf_ready_func_data);
