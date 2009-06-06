/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef GTK_TORRENT_FILE_LIST_H
#define GTK_TORRENT_FILE_LIST_H

#include <gtk/gtk.h>
#include "tr-core.h"

GtkWidget * file_list_new( TrCore *, int torrentId );

void        file_list_clear( GtkWidget * );

void        file_list_set_torrent( GtkWidget *, int torrentId );

#endif
