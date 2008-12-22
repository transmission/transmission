/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: details.c 5987 2008-06-01 01:40:32Z charles $
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_httpIsValidURL */

#include "actions.h"
#include "details.h"
#include "file-list.h"
#include "tracker-list.h"
#include "tr-torrent.h"
#include "hig.h"
#include "util.h"

#define PAGE_KEY "page"

struct tracker_page
{
    TrTorrent *         gtor;

    GtkTreeView *       view;
    GtkListStore *      store;
    GtkTreeSelection *  sel;

    GtkWidget *         add_button;
    GtkWidget *         remove_button;
    GtkWidget *         save_button;
    GtkWidget *         revert_button;

    GtkWidget *         last_scrape_time_lb;
    GtkWidget *         last_scrape_response_lb;
    GtkWidget *         next_scrape_countdown_lb;

    GtkWidget *         last_announce_time_lb;
    GtkWidget *         last_announce_response_lb;
    GtkWidget *         next_announce_countdown_lb;
    GtkWidget *         manual_announce_countdown_lb;
};

enum
{
    TR_COL_TIER,
    TR_COL_ANNOUNCE,
    TR_N_COLS
};

static void
setTrackerChangeState( struct tracker_page * page,
                       gboolean              changed )
{
    if( page->save_button )
        gtk_widget_set_sensitive( page->save_button, changed );

    if( page->revert_button )
        gtk_widget_set_sensitive( page->revert_button, changed );
}

static GtkTreeModel*
tracker_model_new( tr_torrent * tor )
{
    int             i;
    const tr_info * inf = tr_torrentInfo( tor );
    GtkListStore *  store = gtk_list_store_new( TR_N_COLS, G_TYPE_INT,
                                                G_TYPE_STRING );

    for( i = 0; inf && i < inf->trackerCount; ++i )
    {
        GtkTreeIter             iter;
        const tr_tracker_info * tinf = inf->trackers + i;
        gtk_list_store_append( store, &iter );
        gtk_list_store_set( store, &iter,
                            TR_COL_TIER, tinf->tier + 1,
                            TR_COL_ANNOUNCE, tinf->announce,
                            -1 );
    }

    gtk_tree_sortable_set_sort_column_id( GTK_TREE_SORTABLE( store ),
                                          TR_COL_TIER,
                                          GTK_SORT_ASCENDING );

    return GTK_TREE_MODEL( store );
}

static void
onTrackerSelectionChanged( GtkTreeSelection * sel,
                           gpointer           gpage )
{
    struct tracker_page * page = gpage;
    const gboolean has_selection =
        gtk_tree_selection_count_selected_rows( sel ) > 0;
    GtkTreeModel * model = GTK_TREE_MODEL( page->store );
    const int trackerCount = gtk_tree_model_iter_n_children( model, NULL );
    const gboolean ok_to_remove = !page->gtor || ( trackerCount > 1 );
    gtk_widget_set_sensitive( page->remove_button, has_selection && ok_to_remove );
}

static void
onTrackerRemoveClicked( GtkButton * w UNUSED,
                        gpointer      gpage )
{
    struct tracker_page * page = gpage;
    GtkTreeModel * model;
    GList * l;
    GList * list = gtk_tree_selection_get_selected_rows( page->sel, &model );

    /* convert the list to references */
    for( l=list; l; l=l->next ) {
        GtkTreePath * path = l->data;
        l->data = gtk_tree_row_reference_new( model, path );
        gtk_tree_path_free( path );
    }

    /* remove the selected rows */
    for( l=list; l; l=l->next ) {
        GtkTreePath * path = gtk_tree_row_reference_get_path( l->data );
        GtkTreeIter iter;
        gtk_tree_model_get_iter( model, &iter, path );
        gtk_list_store_remove( page->store, &iter );
        gtk_tree_path_free( path );
        gtk_tree_row_reference_free( l->data );
    }

    setTrackerChangeState( page, TRUE );

    /* cleanup */
    g_list_free( list );
}

static void
onTrackerAddClicked( GtkButton * w UNUSED,
                     gpointer      gpage )
{
    GtkTreeIter           iter;
    struct tracker_page * page = gpage;
    GtkTreePath *         path;

    gtk_list_store_append( page->store, &iter );
    setTrackerChangeState( page, TRUE );
    gtk_list_store_set( page->store, &iter,
                        TR_COL_TIER, 1,
                        TR_COL_ANNOUNCE, "",
                        -1 );
    path = gtk_tree_model_get_path( GTK_TREE_MODEL( page->store ), &iter );
    gtk_tree_view_set_cursor( page->view, path,
                              gtk_tree_view_get_column( page->view,
                                                        TR_COL_ANNOUNCE ),
                              TRUE );
    gtk_tree_path_free( path );
}

static void
onTrackerSaveClicked( GtkButton * w UNUSED,
                      gpointer      gpage )
{
    struct tracker_page * page = gpage;
    GtkTreeModel *        model = GTK_TREE_MODEL( page->store );
    const int             n = gtk_tree_model_iter_n_children( model, NULL );

    if( n > 0 ) /* must have at least one tracker */
    {
        int               i = 0;
        GtkTreeIter       iter;
        tr_tracker_info * trackers;

        /* build the tracker list */
        trackers = g_new0( tr_tracker_info, n );
        if( gtk_tree_model_get_iter_first( model, &iter ) ) do
            {
                gtk_tree_model_get( model, &iter, TR_COL_TIER,
                                    &trackers[i].tier,
                                    TR_COL_ANNOUNCE, &trackers[i].announce,
                                    -1 );
                ++i;
            }
            while( gtk_tree_model_iter_next( model, &iter ) );

        g_assert( i == n );

        /* set the tracker list */
        tr_torrentSetAnnounceList( tr_torrent_handle( page->gtor ),
                                   trackers, n );


        setTrackerChangeState( page, FALSE );

        /* cleanup */
        for( i = 0; i < n; ++i )
            g_free( trackers[i].announce );
        g_free( trackers );
    }
}

static void
onTrackerRevertClicked( GtkButton * w UNUSED,
                        gpointer      gpage )
{
    struct tracker_page * page = gpage;
    GtkTreeModel *        model =
        tracker_model_new( tr_torrent_handle( page->gtor ) );

    gtk_tree_view_set_model( page->view, model );
    page->store = GTK_LIST_STORE( model );
    g_object_unref( G_OBJECT( model ) );
    setTrackerChangeState( page, FALSE );
}

static void
onAnnounceEdited( GtkCellRendererText * renderer UNUSED,
                  gchar *                        path_string,
                  gchar *                        new_text,
                  gpointer                       gpage )
{
    struct tracker_page * page = gpage;
    GtkTreeModel *        model = GTK_TREE_MODEL( page->store );
    GtkTreeIter           iter;
    GtkTreePath *         path = gtk_tree_path_new_from_string( path_string );

    if( gtk_tree_model_get_iter( model, &iter, path ) )
    {
        char * old_text;
        gtk_tree_model_get( model, &iter, TR_COL_ANNOUNCE, &old_text, -1 );
        if( tr_httpIsValidURL( new_text ) )
        {
            if( strcmp( old_text, new_text ) )
            {
                gtk_list_store_set( page->store, &iter, TR_COL_ANNOUNCE,
                                    new_text,
                                    -1 );
                setTrackerChangeState( page, TRUE );
            }
        }
        else if( !tr_httpIsValidURL( old_text ) )
        {
            /* both old and new are invalid...
               they must've typed in an invalid URL
               after hitting the "Add" button */
            onTrackerRemoveClicked( NULL, page );
            setTrackerChangeState( page, TRUE );
        }
        g_free( old_text );
    }
    gtk_tree_path_free( path );
}

static void
onTierEdited( GtkCellRendererText  * renderer UNUSED,
              gchar *                         path_string,
              gchar *                         new_text,
              gpointer                        gpage )
{
    struct tracker_page * page = gpage;
    GtkTreeModel *        model = GTK_TREE_MODEL( page->store );
    GtkTreeIter           iter;
    GtkTreePath *         path;
    char *                end;
    int                   new_tier;

    errno = 0;
    new_tier = strtol( new_text, &end, 10 );
    if( new_tier < 1 || *end || errno )
        return;

    path = gtk_tree_path_new_from_string( path_string );
    if( gtk_tree_model_get_iter( model, &iter, path ) )
    {
        int old_tier;
        gtk_tree_model_get( model, &iter, TR_COL_TIER, &old_tier, -1 );
        if( old_tier != new_tier )
        {
            gtk_list_store_set( page->store, &iter, TR_COL_TIER, new_tier,
                                -1 );
            setTrackerChangeState( page, TRUE );
        }
    }
    gtk_tree_path_free( path );
}

GtkWidget*
tracker_list_new( TrTorrent * gtor )
{
    GtkWidget *           w;
    GtkWidget *           buttons;
    GtkWidget *           box;
    GtkWidget *           top;
    GtkWidget *           fr;
    GtkTreeModel *        m;
    GtkCellRenderer *     r;
    GtkTreeViewColumn *   c;
    GtkTreeSelection *    sel;
    struct tracker_page * page = g_new0( struct tracker_page, 1 );

    page->gtor = gtor;

    top = gtk_hbox_new( FALSE, GUI_PAD );
    box = gtk_vbox_new( FALSE, GUI_PAD );
    buttons = gtk_vbox_new( TRUE, GUI_PAD );

    m = tracker_model_new( tr_torrent_handle( gtor ) );
    page->store = GTK_LIST_STORE( m );
    w = gtk_tree_view_new_with_model( m );
    g_signal_connect( w, "button-release-event",
                      G_CALLBACK( on_tree_view_button_released ), NULL );
    page->view = GTK_TREE_VIEW( w );
    gtk_tree_view_set_enable_search( page->view, FALSE );
    r = gtk_cell_renderer_text_new( );
    g_object_set( G_OBJECT( r ),
                  "editable", TRUE,
                  NULL );
    g_signal_connect( r, "edited",
                      G_CALLBACK( onTierEdited ), page );
    c = gtk_tree_view_column_new_with_attributes( _( "Tier" ), r,
                                                  "text", TR_COL_TIER,
                                                  NULL );
    gtk_tree_view_column_set_sort_column_id( c, TR_COL_TIER );
    gtk_tree_view_append_column( page->view, c );
    r = gtk_cell_renderer_text_new( );
    g_object_set( G_OBJECT( r ),
                  "editable", TRUE,
                  "ellipsize", PANGO_ELLIPSIZE_END,
                  NULL );
    g_signal_connect( r, "edited",
                      G_CALLBACK( onAnnounceEdited ), page );
    c = gtk_tree_view_column_new_with_attributes( _( "Announce URL" ), r,
                                                  "text", TR_COL_ANNOUNCE,
                                                  NULL );
    gtk_tree_view_column_set_sort_column_id( c, TR_COL_ANNOUNCE );
    gtk_tree_view_append_column( page->view, c );
    w = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( w ),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC );
    sel = gtk_tree_view_get_selection( page->view );
    page->sel = sel;
    g_signal_connect( sel, "changed",
                      G_CALLBACK( onTrackerSelectionChanged ), page );
    gtk_tree_selection_set_mode( sel, GTK_SELECTION_MULTIPLE );
    gtk_container_add( GTK_CONTAINER( w ), GTK_WIDGET( page->view ) );
    gtk_widget_set_size_request( w, -1, 133 );
    fr = gtk_frame_new( NULL );
    gtk_frame_set_shadow_type( GTK_FRAME( fr ), GTK_SHADOW_IN );
    gtk_container_add( GTK_CONTAINER( fr ), w );
    g_object_unref( G_OBJECT( m ) );

    w = gtk_button_new_from_stock( GTK_STOCK_ADD );
    g_signal_connect( w, "clicked", G_CALLBACK( onTrackerAddClicked ), page );
    gtk_box_pack_start( GTK_BOX( buttons ), w, TRUE, TRUE, 0 );
    page->add_button = w;
    w = gtk_button_new_from_stock( GTK_STOCK_REMOVE );
    g_signal_connect( w, "clicked", G_CALLBACK(
                          onTrackerRemoveClicked ), page );
    gtk_box_pack_start( GTK_BOX( buttons ), w, TRUE, TRUE, 0 );
    page->remove_button = w;
    if( gtor )
    {
        w = gtk_button_new_from_stock( GTK_STOCK_SAVE );
        g_signal_connect( w, "clicked", G_CALLBACK(
                              onTrackerSaveClicked ), page );
        gtk_widget_set_sensitive( w, FALSE );
        gtk_box_pack_start( GTK_BOX( buttons ), w, TRUE, TRUE, 0 );
        page->save_button = w;

        w = gtk_button_new_from_stock( GTK_STOCK_REVERT_TO_SAVED );
        g_signal_connect( w, "clicked", G_CALLBACK(
                              onTrackerRevertClicked ), page );
        gtk_widget_set_sensitive( w, FALSE );
        gtk_box_pack_start( GTK_BOX( buttons ), w, TRUE, TRUE, 0 );
        page->revert_button = w;
    }

    gtk_box_pack_start( GTK_BOX( box ), buttons, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( top ), fr, TRUE, TRUE, 0 );
    gtk_box_pack_start( GTK_BOX( top ), box, FALSE, FALSE, 0 );

    onTrackerSelectionChanged( sel, page );

    g_object_set_data_full( G_OBJECT( top ), PAGE_KEY, page, g_free );
    return top;
}

tr_tracker_info*
tracker_list_get_trackers( GtkWidget * list,
                           int *       trackerCount )
{
    struct tracker_page * page = g_object_get_data( G_OBJECT(
                                                        list ), PAGE_KEY );
    GtkTreeModel *        model = GTK_TREE_MODEL( page->store );
    const int             n = gtk_tree_model_iter_n_children( model, NULL );
    tr_tracker_info *     trackers;
    int                   i = 0;
    GtkTreeIter           iter;

    /* build the tracker list */
    trackers = g_new0( tr_tracker_info, n );
    if( gtk_tree_model_get_iter_first( model, &iter ) ) do
        {
            int tier;
            gtk_tree_model_get( model, &iter,
                                TR_COL_TIER, &tier,
                                TR_COL_ANNOUNCE, &trackers[i].announce,
                                -1 );
            /* tracker_info.tier is zero-based, but the display is 1-based */
            trackers[i].tier = tier - 1;
            ++i;
        }
        while( gtk_tree_model_iter_next( model, &iter ) );

    g_assert( i == n );

    *trackerCount = n;

    return trackers;
}

void
tracker_list_add_trackers( GtkWidget             * list,
                           const tr_tracker_info * trackers,
                           int                     trackerCount )
{
    int i;
    struct tracker_page * page = g_object_get_data( G_OBJECT( list ), PAGE_KEY );
    GtkListStore * store = page->store;

    for( i=0; i<trackerCount; ++i )
    {
        GtkTreeIter             iter;
        gtk_list_store_append( store, &iter );
        gtk_list_store_set( store, &iter,
                            TR_COL_TIER, trackers[i].tier + 1,
                            TR_COL_ANNOUNCE, trackers[i].announce,
                            -1 );
    }
}
