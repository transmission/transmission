/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef GTR_FILTER_H
#define GTR_FILTER_H

#include <gtk/gtk.h>
#include <libtransmission/transmission.h>

GtkWidget * gtr_filter_bar_new (tr_session     * session,
                                GtkTreeModel   * torrent_model,
                                GtkTreeModel  ** filter_model);

#endif
