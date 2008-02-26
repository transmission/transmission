/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#ifndef TR_GTK_OPEN_DIALOG_H
#define TR_GTK_OPEN_DIALOG_H

#include <gtk/gtkwindow.h>
#include "tr-core.h"

GtkWidget* makeaddwind( GtkWindow  * parent,
                        TrCore     * core,
                        tr_ctor    * ctor );

#endif /* TR_GTK_OPEN_DIALOG */
