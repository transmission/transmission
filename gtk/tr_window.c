/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>

#include "actions.h"
#include "hig.h"
#include "torrent-cell-renderer.h"
#include "tr_core.h"
#include "tr_torrent.h"
#include "tr_window.h"
#include "util.h"

typedef struct
{
    GtkWidget * scroll;
    GtkWidget * view;
    GtkWidget * status;
    GtkWidget * ul_lb;
    GtkWidget * dl_lb;
    GtkTreeSelection * selection;
}
PrivateData;

#define PRIVATE_DATA_KEY "private-data"

PrivateData*
get_private_data( TrWindow * w )
{
    return (PrivateData*) g_object_get_data (G_OBJECT(w), PRIVATE_DATA_KEY);
}

/***
****
***/

static void
on_popup_menu ( GtkWidget * self UNUSED, GdkEventButton * event )
{
    GtkWidget * menu = action_get_widget ( "/main-window-popup" );
    gtk_menu_popup (GTK_MENU(menu), NULL, NULL, NULL, NULL,
                    (event ? event->button : 0),
                    (event ? event->time : 0));
}

static void
view_row_activated ( GtkTreeView       * tree_view  UNUSED,
                     GtkTreePath       * path       UNUSED,
                     GtkTreeViewColumn * column     UNUSED,
                     gpointer            user_data  UNUSED )
{
    action_activate( "show-torrent-details" );
}

static GtkWidget*
makeview( PrivateData * p )
{
    GtkWidget         * view;
    GtkTreeViewColumn * col;
    GtkTreeSelection  * sel;
    GtkCellRenderer   * r;

    view = gtk_tree_view_new();
    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(view), FALSE );

    p->selection = gtk_tree_view_get_selection( GTK_TREE_VIEW(view) );

    r = torrent_cell_renderer_new( );
    col = gtk_tree_view_column_new_with_attributes( _("Torrent"), r, "torrent", MC_TORRENT_RAW, NULL );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_AUTOSIZE );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );
    g_object_set( r, "xpad", GUI_PAD_SMALL, "ypad", GUI_PAD_SMALL, NULL );

    gtk_tree_view_set_rules_hint( GTK_TREE_VIEW( view ), TRUE );
    sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );
    gtk_tree_selection_set_mode( GTK_TREE_SELECTION( sel ),
                                 GTK_SELECTION_MULTIPLE );

    g_signal_connect( view, "popup-menu",
                      G_CALLBACK(on_popup_menu), NULL );
    g_signal_connect( view, "button-press-event",
                      G_CALLBACK(on_tree_view_button_pressed), (void *) on_popup_menu);
    g_signal_connect( view, "row-activated",
                      G_CALLBACK(view_row_activated), NULL);

    return view;
}

static void
realized_cb ( GtkWidget * wind, gpointer unused UNUSED )
{
    PrivateData * p = get_private_data( GTK_WINDOW( wind ) );
    sizingmagic( GTK_WINDOW(wind), GTK_SCROLLED_WINDOW( p->scroll ),
                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS );
}

/***
****  PUBLIC
***/

GtkWidget *
tr_window_new( GtkUIManager * ui_manager )
{
    PrivateData * p = g_new( PrivateData, 1 );
    GtkWidget *vbox, *w, *self, *h;

    /* make the window */
    self = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    g_object_set_data_full(G_OBJECT(self), PRIVATE_DATA_KEY, p, g_free );
    gtk_window_set_title( GTK_WINDOW( self ), g_get_application_name());
    gtk_window_set_role( GTK_WINDOW( self ), "tr-main" );
    gtk_window_add_accel_group (GTK_WINDOW(self),
                                gtk_ui_manager_get_accel_group (ui_manager));
    g_signal_connect( self, "realize", G_CALLBACK(realized_cb), NULL);

    /* window's main container */
    vbox = gtk_vbox_new (FALSE, 0);
    gtk_container_add (GTK_CONTAINER(self), vbox);

    /* main menu */
    w = action_get_widget( "/main-window-menu" );
    gtk_box_pack_start( GTK_BOX(vbox), w, FALSE, FALSE, 0 ); 

    /* toolbar */
    w = action_get_widget( "/main-window-toolbar" );
    gtk_box_pack_start( GTK_BOX(vbox), w, FALSE, FALSE, 0 ); 

    /* workarea */
    p->view = makeview( p );
    w = p->scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(w), p->view );
    gtk_box_pack_start_defaults( GTK_BOX(vbox), w );
    gtk_container_set_focus_child( GTK_CONTAINER( vbox ), w );

    /* spacer */
    w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
    gtk_widget_set_usize (w, 0u, 6u);
    gtk_box_pack_start( GTK_BOX(vbox), w, FALSE, FALSE, 0 ); 

    /* statusbar */
    h = gtk_hbox_new( FALSE, 0 );
    w = p->ul_lb = gtk_label_new( NULL );
    gtk_box_pack_end( GTK_BOX(h), w, FALSE, FALSE, GUI_PAD );
    w = gtk_vseparator_new( );
    gtk_box_pack_end( GTK_BOX(h), w, FALSE, FALSE, GUI_PAD );
    w = p->dl_lb = gtk_label_new( NULL );
    gtk_box_pack_end( GTK_BOX(h), w, FALSE, FALSE, GUI_PAD );
    gtk_box_pack_start( GTK_BOX(vbox), h, FALSE, FALSE, 0 );

    /* show all but the window */
    gtk_widget_show_all( vbox );

    return self;
}

void
tr_window_update( TrWindow * self, float downspeed, float upspeed )
{
    PrivateData * p = get_private_data( self );
    char speedStr[64];
    char buf[128];

    tr_strlspeed( speedStr, downspeed, sizeof(speedStr) );
    g_snprintf( buf, sizeof(buf), _("Total DL: %s"), speedStr );
    gtk_label_set_text( GTK_LABEL(p->dl_lb), buf );

    tr_strlspeed( speedStr, upspeed, sizeof(speedStr) );
    g_snprintf( buf, sizeof(buf), _("Total UL: %s"), speedStr );
    gtk_label_set_text( GTK_LABEL(p->ul_lb), buf );
}

GtkTreeSelection*
tr_window_get_selection ( TrWindow * w )
{
    return get_private_data(w)->selection;
}
