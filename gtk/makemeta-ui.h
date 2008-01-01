/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#ifndef MAKE_META_UI__H
#define MAKE_META_UI__H

#include <gtk/gtk.h>
#include <libtransmission/transmission.h>

GtkWidget* make_meta_ui( GtkWindow * parent, tr_handle * handle );

#endif
