/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef GTR_TORRENT_DETAILS_H
#define GTR_TORRENT_DETAILS_H

#include <gtk/gtk.h>
#include "tr-core.h"

GtkWidget*  gtr_torrent_details_dialog_new        (GtkWindow * parent,
                                                      TrCore    * core);

void        gtr_torrent_details_dialog_set_torrents (GtkWidget * details_dialog,
                                                      GSList    * torrent_ids);

#endif /* GTR_TORRENT_DETAILS_H */
