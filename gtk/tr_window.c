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
#include "tr_cell_renderer_progress.h"
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
    GtkCellRenderer * namerend;
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

/* kludge to have the progress bars notice theme changes */
static void
stylekludge( GObject * obj, GParamSpec * spec, gpointer data )
{
    if( 0 == strcmp( "style", spec->name ) )
    {
        tr_cell_renderer_progress_reset_style(
            TR_CELL_RENDERER_PROGRESS( data ) );
        gtk_widget_queue_draw( GTK_WIDGET( obj ) );
    }
}

static void
formatname( GtkTreeViewColumn * col SHUTUP, GtkCellRenderer * rend,
            GtkTreeModel * model, GtkTreeIter * iter, gpointer data SHUTUP )
{
    TrTorrent * gtor;
    char  * name, * mb, * str, * top, * bottom;
    const char * fmt;
    guint64 size;
    int     status, err, eta, tpeers, upeers, dpeers;

    gtk_tree_model_get( model, iter, MC_NAME, &name, MC_STAT, &status,
                        MC_ERR, &err, MC_SIZE, &size,
                        MC_ETA, &eta, MC_PEERS, &tpeers, MC_UPEERS, &upeers,
                        MC_DPEERS, &dpeers, MC_TORRENT, &gtor, -1 );

    tpeers = MAX( tpeers, 0 );
    upeers = MAX( upeers, 0 );
    dpeers = MAX( dpeers, 0 );
    mb = readablesize(size);

    top = tr_torrent_status_str ( gtor );

    if( TR_OK != err )
    {
        char * terr;
        gtk_tree_model_get( model, iter, MC_TERR, &terr, -1 );
        bottom = g_strconcat( _("Error: "), terr, NULL );
        g_free( terr );
    }
    else if( TR_STATUS_DOWNLOAD & status )
    {
        bottom = g_strdup_printf( ngettext( "Downloading from %i of %i peer",
                                            "Downloading from %i of %i peers",
                                            tpeers ), dpeers, tpeers );
    }
    else
    {
        bottom = NULL;
    }

    fmt = err==TR_OK
        ? "<b>%s (%s)</b>\n<small>%s\n%s</small>"
        : "<span color='red'><b>%s (%s)</b>\n<small>%s\n%s</small></span>";
    str = g_markup_printf_escaped( fmt, name, mb, top, (bottom ? bottom : "") );
    g_object_set( rend, "markup", str, NULL );
    g_free( name );
    g_free( mb );
    g_free( str );
    g_free( top );
    g_free( bottom );
    g_object_unref( gtor );
}

static void
formatprog( GtkTreeViewColumn * col SHUTUP, GtkCellRenderer * rend,
            GtkTreeModel * model, GtkTreeIter * iter, gpointer data SHUTUP )
{
    char  * dlstr, * ulstr, * str, * marked;
    gfloat  prog, dl, ul;
    guint64 down, up;

    gtk_tree_model_get( model, iter, MC_PROG_D, &prog, MC_DRATE, &dl,
                        MC_URATE, &ul, MC_DOWN, &down, MC_UP, &up, -1 );
    prog = MAX( prog, 0.0 );
    prog = MIN( prog, 1.0 );

    ulstr = readablespeed (ul);
    if( 1.0 == prog )
    {
        dlstr = ratiostr( down, up );
        str = g_strdup_printf( _("Ratio: %s\nUL: %s"), dlstr, ulstr );
    }
    else
    {
        dlstr = readablespeed( dl );
        str = g_strdup_printf( _("DL: %s\nUL: %s"), dlstr, ulstr );
    }
    marked = g_markup_printf_escaped( "<small>%s</small>", str );
    g_object_set( rend, "markup", str, "progress", prog, NULL );
    g_free( dlstr );
    g_free( ulstr );
    g_free( str );
    g_free( marked );
}

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
    action_activate( "show-torrent-properties" );
}

static GtkWidget*
makeview( PrivateData * p )
{
    GtkWidget         * view;
    GtkTreeViewColumn * col;
    GtkTreeSelection  * sel;
    GtkCellRenderer   * namerend, * progrend;
    char              * str;

    view     = gtk_tree_view_new();

    p->selection = gtk_tree_view_get_selection( GTK_TREE_VIEW(view) );
    namerend = gtk_cell_renderer_text_new();
    p->namerend = namerend;
    /* note that this renderer is set to ellipsize, just not here */
    col = gtk_tree_view_column_new_with_attributes( _("Name"), namerend,
                                                    NULL );
    gtk_tree_view_column_set_cell_data_func( col, namerend, formatname,
                                             NULL, NULL );
    gtk_tree_view_column_set_expand( col, TRUE );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_AUTOSIZE );
    gtk_tree_view_column_set_sort_column_id( col, MC_NAME );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );

    progrend = tr_cell_renderer_progress_new();
    /* this string is only used to determine the size of the progress bar */
    str = g_markup_printf_escaped( "<big>%s</big>", _("  fnord    fnord  ") );
    g_object_set( progrend, "bar-sizing", str, NULL );
    g_free(str);
    col = gtk_tree_view_column_new_with_attributes( _("Progress"), progrend,
                                                    NULL);
    gtk_tree_view_column_set_cell_data_func( col, progrend, formatprog,
                                             NULL, NULL );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_AUTOSIZE );
    gtk_tree_view_column_set_sort_column_id( col, MC_PROG_D );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );

    /* XXX this shouldn't be necessary */
    g_signal_connect( view, "notify::style",
                      G_CALLBACK( stylekludge ), progrend );

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
    g_object_set( p->namerend, "ellipsize", PANGO_ELLIPSIZE_END, NULL );
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
    char *tmp1, *tmp2;

    tmp1 = readablespeed( downspeed );
    tmp2 = g_strdup_printf( _("Total DL: %s"), tmp1 );
    gtk_label_set_text( GTK_LABEL(p->dl_lb), tmp2 );
    g_free( tmp2 );
    g_free( tmp1 );

    tmp1 = readablespeed( upspeed );
    tmp2 = g_strdup_printf( _("Total UL: %s"), tmp1 );
    gtk_label_set_text( GTK_LABEL(p->ul_lb), tmp2 );
    g_free( tmp2 );
    g_free( tmp1 );
}

GtkTreeSelection*
tr_window_get_selection ( TrWindow * w )
{
    return get_private_data(w)->selection;
}
