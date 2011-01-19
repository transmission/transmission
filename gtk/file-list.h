/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
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

GtkWidget * gtr_file_list_new         ( TrCore *, int torrent_id );
void        gtr_file_list_clear       ( GtkWidget * );
void        gtr_file_list_set_torrent ( GtkWidget *, int torrent_id );

#endif
