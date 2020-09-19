/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <libtransmission/transmission.h>
#include <gtk/gtk.h>

GtkWidget* gtr_filter_bar_new(tr_session* session, GtkTreeModel* torrent_model,
                              GtkTreeModel** filter_model);
