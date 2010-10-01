/*
 * This file Copyright (C) 2007-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TRACKER_LIST_H
#define TRACKER_LIST_H

#include <gtk/gtk.h>
#include <libtransmission/transmission.h>

void tracker_list_set_torrent( GtkWidget*, int torrentId );

void tracker_list_clear( GtkWidget* );

GtkWidget* tracker_list_new( tr_session*, int torrentId, gboolean isNew );

/**
 * @return an array of tr_tracker_info's.  It's the caller's responsibility
 *         to g_free() every announce in the array, then the array itself.
 */
tr_tracker_info* tracker_list_get_trackers( GtkWidget * list,
                                            int       * trackerCount );

void tracker_list_add_trackers( GtkWidget             * list,
                                const tr_tracker_info * trackers,
                                int                     trackerCount );


#endif
