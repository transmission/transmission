#ifndef TRACKER_LIST_H
#define TRACKER_LIST_H

#include <gtk/gtk.h>
#include "tr-torrent.h"

GtkWidget*       tracker_list_new( TrTorrent * gtor );

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
