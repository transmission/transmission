#ifndef TRACKER_LIST_H
#define TRACKER_LIST_H

#include <gtk/gtkwidget.h>
#include "tr-torrent.h"

GtkWidget* tracker_list_new( TrTorrent        * gtor,
                             GtkPositionType    buttonsPosition );

/**
 * @return an array of tr_tracker_info's.  It's the caller's responsibility
 *         to g_free() every announce in the array, then the array itself.
 */
tr_tracker_info* tracker_list_get_trackers( GtkWidget * list,
                                            int       * trackerCount );

#endif
