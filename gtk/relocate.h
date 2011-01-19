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

#ifndef GTR_RELOCATE_H
#define GTR_RELOCATE_H

#include <gtk/gtk.h>
#include "tr-core.h"

GtkWidget * gtr_relocate_dialog_new( GtkWindow * parent,
                                     TrCore    * core,
                                     GSList    * torrentIds );

#endif
