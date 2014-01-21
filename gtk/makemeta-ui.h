/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef GTR_TORRENT_CREATION_DIALOG_H
#define GTR_TORRENT_CREATION_DIALOG_H

#include <gtk/gtk.h>
#include "tr-core.h"

GtkWidget* gtr_torrent_creation_dialog_new (GtkWindow * parent, TrCore * core);

#endif
