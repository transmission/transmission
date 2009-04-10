/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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
#include "conf.h"
#include "hig.h"
#include "sexy-icon-entry.h"
#include "torrent-cell-renderer.h"
#include "tr-prefs.h"
#include "tr-torrent.h"
#include "tr-window.h"
#include "util.h"

#if !GTK_CHECK_VERSION( 2, 8, 0 )
static void
gtk_tree_view_column_queue_resize( GtkTreeViewColumn * column ) /* yuck */
{
    const int spacing = gtk_tree_view_column_get_spacing( column );

    gtk_tree_view_column_set_spacing( column, spacing + 1 );
    gtk_tree_view_column_set_spacing( column, spacing );
}

#endif

typedef enum
{
    FILTER_TEXT_MODE_NAME,
    FILTER_TEXT_MODE_FILES,
    FILTER_TEXT_MODE_TRACKER,
    FILTER_TEXT_MODE_QTY
}
filter_text_mode_t;

typedef enum
{
    FILTER_MODE_ALL,
    FILTER_MODE_ACTIVE,
    FILTER_MODE_DOWNLOADING,
    FILTER_MODE_SEEDING,
    FILTER_MODE_PAUSED,
    FILTER_MODE_QTY
}
filter_mode_t;

typedef struct
{
    GtkWidget *           scroll;
    GtkWidget *           view;
    GtkWidget *           toolbar;
    GtkWidget *           filter;
    GtkWidget *           status;
    GtkWidget *           status_menu;
    GtkWidget *           ul_lb;
    GtkWidget *           dl_lb;
    GtkWidget *           stats_lb;
    GtkWidget *           gutter_lb;
    GtkTreeSelection *    selection;
    GtkCellRenderer *     renderer;
    GtkTreeViewColumn *   column;
    GtkTreeModel *        filter_model;
    TrCore *              core;
    gulong                pref_handler_id;
    filter_mode_t         filter_mode;
    filter_text_mode_t    filter_text_mode;
    char *                filter_text;
    GtkToggleButton     * filter_toggles[FILTER_MODE_QTY];
}
PrivateData;

#define PRIVATE_DATA_KEY "private-data"

#define FILTER_TEXT_MODE_KEY "tr-filter-text-mode"

static PrivateData*
get_private_data( TrWindow * w )
{
    return g_object_get_data ( G_OBJECT( w ), PRIVATE_DATA_KEY );
}

/***
****
***/

static void
on_popup_menu( GtkWidget * self UNUSED,
               GdkEventButton * event )
{
    GtkWidget * menu = action_get_widget ( "/main-window-popup" );

    gtk_menu_popup ( GTK_MENU( menu ), NULL, NULL, NULL, NULL,
                    ( event ? event->button : 0 ),
                    ( event ? event->time : 0 ) );
}

static void
view_row_activated( GtkTreeView       * tree_view UNUSED,
                    GtkTreePath       * path      UNUSED,
                    GtkTreeViewColumn * column    UNUSED,
                    gpointer            user_data UNUSED )
{
    action_activate( "show-torrent-properties" );
}

static gboolean is_row_visible( GtkTreeModel *,
                                GtkTreeIter  *,
                                gpointer );

static GtkWidget*
makeview( PrivateData * p,
          TrCore *      core )
{
    GtkWidget *         view;
    GtkTreeViewColumn * col;
    GtkTreeSelection *  sel;
    GtkCellRenderer *   r;
    GtkTreeModel *      filter_model;

    view = gtk_tree_view_new( );
    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW( view ), FALSE );
    gtk_tree_view_set_fixed_height_mode( GTK_TREE_VIEW( view ), TRUE );

    p->selection = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );

    p->column = col = GTK_TREE_VIEW_COLUMN (g_object_new (GTK_TYPE_TREE_VIEW_COLUMN,		      
        "title", _("Torrent"),
        "resizable", TRUE,
        "sizing", GTK_TREE_VIEW_COLUMN_FIXED,
        NULL));

    p->renderer = r = torrent_cell_renderer_new( );
    gtk_tree_view_column_pack_start( col, r, FALSE );
    gtk_tree_view_column_add_attribute( col, r, "torrent", MC_TORRENT_RAW );
    
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );
    g_object_set( r, "xpad", GUI_PAD_SMALL, "ypad", GUI_PAD_SMALL, NULL );

    gtk_tree_view_set_rules_hint( GTK_TREE_VIEW( view ), TRUE );
    sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );
    gtk_tree_selection_set_mode( GTK_TREE_SELECTION( sel ),
                                 GTK_SELECTION_MULTIPLE );

    g_signal_connect( view, "popup-menu",
                      G_CALLBACK( on_popup_menu ), NULL );
    g_signal_connect( view, "button-press-event",
                      G_CALLBACK( on_tree_view_button_pressed ),
                      (void *) on_popup_menu );
    g_signal_connect( view, "button-release-event",
                      G_CALLBACK( on_tree_view_button_released ), NULL );
    g_signal_connect( view, "row-activated",
                      G_CALLBACK( view_row_activated ), NULL );


    filter_model = p->filter_model = gtk_tree_model_filter_new(
                       tr_core_model( core ), NULL );

    gtk_tree_model_filter_set_visible_func( GTK_TREE_MODEL_FILTER(
                                                filter_model ),
                                            is_row_visible,
                                            p, NULL );

    gtk_tree_view_set_model( GTK_TREE_VIEW( view ), filter_model );

    return view;
}

static void
prefsChanged( TrCore * core UNUSED,
              const char *  key,
              gpointer      wind )
{
    PrivateData * p = get_private_data( GTK_WINDOW( wind ) );

    if( !strcmp( key, PREF_KEY_MINIMAL_VIEW ) )
    {
        g_object_set( p->renderer, "minimal", pref_flag_get( key ), NULL );
        /* since the cell size has changed, we need gtktreeview to revalidate
         * its fixed-height mode values.  Unfortunately there's not an API call
         * for that, but it *does* revalidate when it thinks the style's been tweaked */
        g_signal_emit_by_name( p->view, "style-set", NULL, NULL );
    }
    else if( !strcmp( key, PREF_KEY_STATUSBAR ) )
    {
        const gboolean isEnabled = pref_flag_get( key );
        g_object_set( p->status, "visible", isEnabled, NULL );
    }
    else if( !strcmp( key, PREF_KEY_FILTERBAR ) )
    {
        const gboolean isEnabled = pref_flag_get( key );
        g_object_set( p->filter, "visible", isEnabled, NULL );
    }
    else if( !strcmp( key, PREF_KEY_TOOLBAR ) )
    {
        const gboolean isEnabled = pref_flag_get( key );
        g_object_set( p->toolbar, "visible", isEnabled, NULL );
    }
    else if( !strcmp( key, PREF_KEY_STATUSBAR_STATS ) )
    {
        tr_window_update( (TrWindow*)wind );
    }
}

static void
privateFree( gpointer vprivate )
{
    PrivateData * p = vprivate;

    g_signal_handler_disconnect( p->core, p->pref_handler_id );
    g_free( p->filter_text );
    g_free( p );
}

static void
onYinYangReleased( GtkWidget * w           UNUSED,
                   GdkEventButton * button UNUSED,
                   gpointer                vprivate )
{
    PrivateData * p = vprivate;

    gtk_menu_popup( GTK_MENU(
                       p->status_menu ), NULL, NULL, NULL, NULL, 0,
                   gtk_get_current_event_time( ) );
}

#define STATS_MODE "stats-mode"

static struct
{
    const char *  val, *i18n;
} stats_modes[] = {
    { "total-ratio",      N_( "Total Ratio" )                },
    { "session-ratio",    N_( "Session Ratio" )              },
    { "total-transfer",   N_( "Total Transfer" )             },
    { "session-transfer", N_( "Session Transfer" )           }
};

static void
status_menu_toggled_cb( GtkCheckMenuItem * menu_item,
                        gpointer           vprivate )
{
    if( gtk_check_menu_item_get_active( menu_item ) )
    {
        PrivateData * p = vprivate;
        const char *  val = g_object_get_data( G_OBJECT(
                                                   menu_item ), STATS_MODE );
        tr_core_set_pref( p->core, PREF_KEY_STATUSBAR_STATS, val );
    }
}

/***
****  FILTER
***/

static int
checkFilterText( filter_text_mode_t    filter_text_mode,
                 const tr_info       * torInfo,
                 const char          * text )
{
    tr_file_index_t i;
    int             ret = 0;
    char *          pch;

    switch( filter_text_mode )
    {
        case FILTER_TEXT_MODE_FILES:
            for( i = 0; i < torInfo->fileCount && !ret; ++i )
            {
                pch = g_utf8_casefold( torInfo->files[i].name, -1 );
                ret = !text || strstr( pch, text ) != NULL;
                g_free( pch );
            }
            break;

        case FILTER_TEXT_MODE_TRACKER:
            pch = g_utf8_casefold( torInfo->trackers[0].announce, -1 );
            ret = !text || ( strstr( pch, text ) != NULL );
            g_free( pch );
            break;

        default: /* NAME */
            pch = g_utf8_casefold( torInfo->name, -1 );
            ret = !text || ( strstr( pch, text ) != NULL );
            g_free( pch );
            break;
    }

    return ret;
}

static int
checkFilterMode( filter_mode_t filter_mode,
                 tr_torrent *  tor )
{
    int ret = 0;

    switch( filter_mode )
    {
        case FILTER_MODE_DOWNLOADING:
            ret = tr_torrentGetActivity( tor ) == TR_STATUS_DOWNLOAD;
            break;

        case FILTER_MODE_SEEDING:
            ret = tr_torrentGetActivity( tor ) == TR_STATUS_SEED;
            break;

        case FILTER_MODE_PAUSED:
            ret = tr_torrentGetActivity( tor ) == TR_STATUS_STOPPED;
            break;

        case FILTER_MODE_ACTIVE:
        {
            const tr_stat * s = tr_torrentStatCached( tor );
            ret = s->peersSendingToUs > 0
               || s->peersGettingFromUs > 0
               || tr_torrentGetActivity( tor ) == TR_STATUS_CHECK;
            break;
        }

        default: /* all */
            ret = 1;
    }

    return ret;
}

static gboolean
is_row_visible( GtkTreeModel * model,
                GtkTreeIter *  iter,
                gpointer       vprivate )
{
    PrivateData * p = vprivate;
    tr_torrent *  tor;

    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );

    return checkFilterMode( p->filter_mode, tor )
           && checkFilterText( p->filter_text_mode, tr_torrentInfo( tor ), p->filter_text );
}

static void updateTorrentCount( PrivateData * p );

static void
refilter( PrivateData * p )
{
    gtk_tree_model_filter_refilter( GTK_TREE_MODEL_FILTER( p->filter_model ) );

    updateTorrentCount( p );
}

static void
filter_text_toggled_cb( GtkCheckMenuItem * menu_item,
                        gpointer           vprivate )
{
    if( gtk_check_menu_item_get_active( menu_item ) )
    {
        PrivateData * p = vprivate;
        p->filter_text_mode =
            GPOINTER_TO_UINT( g_object_get_data( G_OBJECT( menu_item ),
                                                 FILTER_TEXT_MODE_KEY ) );
        refilter( p );
    }
}

static void
filter_toggled_cb( GtkToggleButton * toggle,
                   gpointer          vprivate )
{
    int mode = -1;
    PrivateData * p = vprivate;

    if( gtk_toggle_button_get_active( toggle ) )
    {
        int i;
        for( i=0; i<FILTER_MODE_QTY; ++i )
            if( p->filter_toggles[i] == toggle )
                mode = i;
            else
                gtk_toggle_button_set_active( p->filter_toggles[i], FALSE );
    }

    if( mode >= 0 ) {
        p->filter_mode = mode;
        refilter( p );
    }
}

static void
filter_entry_changed( GtkEditable * e,
                      gpointer      vprivate )
{
    char *        pch;
    PrivateData * p = vprivate;

    pch = gtk_editable_get_chars( e, 0, -1 );
    g_free( p->filter_text );
    p->filter_text = g_utf8_casefold( pch, -1 );
    refilter( p );
    g_free( pch );
}

static void
entry_icon_released( SexyIconEntry         * entry  UNUSED,
                     SexyIconEntryPosition          icon_pos,
                     int                     button UNUSED,
                     gpointer                       menu )
{
    if( icon_pos == SEXY_ICON_ENTRY_PRIMARY )
        gtk_menu_popup ( GTK_MENU(
                            menu ), NULL, NULL, NULL, NULL, 0,
                        gtk_get_current_event_time( ) );
}

#if GTK_CHECK_VERSION( 2, 12, 0 )

static void
findMaxAnnounceTime( GtkTreeModel *      model,
                     GtkTreePath  * path UNUSED,
                     GtkTreeIter *       iter,
                     gpointer            gmaxTime )
{
    tr_torrent *    tor;
    const tr_stat * torStat;
    time_t *        maxTime = gmaxTime;

    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    torStat = tr_torrentStatCached( tor );
    *maxTime = MAX( *maxTime, torStat->manualAnnounceTime );
}

static gboolean
onAskTrackerQueryTooltip( GtkWidget *            widget UNUSED,
                          gint                   x UNUSED,
                          gint                   y UNUSED,
                          gboolean               keyboard_tip UNUSED,
                          GtkTooltip *           tooltip,
                          gpointer               gdata )
{
    const time_t now = time( NULL );
    time_t       maxTime = 0;
    PrivateData * p = gdata;

    gtk_tree_selection_selected_foreach( p->selection,
                                         findMaxAnnounceTime,
                                         &maxTime );
    if( maxTime <= now )
    {
        return FALSE;
    }
    else
    {
        char      buf[128];
        char      timebuf[64];
        const int seconds = maxTime - now;

        tr_strltime( timebuf, seconds, sizeof( timebuf ) );
        g_snprintf( buf, sizeof( buf ),
                    _( "Tracker will allow requests in %s" ), timebuf );
        gtk_tooltip_set_text( tooltip, buf );
        return TRUE;
    }
}

#endif

/***
****  PUBLIC
***/

GtkWidget *
tr_window_new( GtkUIManager * ui_mgr, TrCore * core )
{
    int           i, n;
    const char *  pch;
    PrivateData * p;
    GtkWidget   *mainmenu, *toolbar, *filter, *list, *status;
    GtkWidget *   vbox, *w, *self, *h, *c, *s, *image, *menu;
    GtkWindow *   win;
    GSList *      l;

    const char *  filter_names[FILTER_MODE_QTY] = {
        /* show all torrents */
        N_( "A_ll" ),
        /* show only torrents that have connected peers */
        N_( "_Active" ),
        /* show only torrents that are trying to download */
        N_( "_Downloading" ),
        /* show only torrents that are trying to upload */
        N_( "_Seeding" ),
        /* show only torrents that are paused */
        N_( "_Paused" )
    };
    const char *  filter_text_names[FILTER_TEXT_MODE_QTY] = {
        N_( "Name" ), N_( "Files" ), N_( "Tracker" )
    };

    p = g_new0( PrivateData, 1 );
    p->filter_mode = FILTER_MODE_ALL;
    p->filter_text_mode = FILTER_TEXT_MODE_NAME;
    p->filter_text = NULL;

    /* make the window */
    self = gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    g_object_set_data_full( G_OBJECT(
                                self ), PRIVATE_DATA_KEY, p, privateFree );
    win = GTK_WINDOW( self );
    gtk_window_set_title( win, g_get_application_name( ) );
    gtk_window_set_role( win, "tr-main" );
    gtk_window_set_default_size( win,
                                 pref_int_get( PREF_KEY_MAIN_WINDOW_WIDTH ),
                                 pref_int_get( PREF_KEY_MAIN_WINDOW_HEIGHT ) );
    gtk_window_move( win, pref_int_get( PREF_KEY_MAIN_WINDOW_X ),
                     pref_int_get( PREF_KEY_MAIN_WINDOW_Y ) );
    gtk_window_add_accel_group( win, gtk_ui_manager_get_accel_group( ui_mgr ) );

    /* window's main container */
    vbox = gtk_vbox_new ( FALSE, 0 );
    gtk_container_add ( GTK_CONTAINER( self ), vbox );

    /* main menu */
    w = mainmenu = action_get_widget( "/main-window-menu" );
    w = action_get_widget( "/main-window-menu/torrent-menu/update-tracker" );
#if GTK_CHECK_VERSION( 2, 12, 0 )
    g_signal_connect( w, "query-tooltip",
                      G_CALLBACK( onAskTrackerQueryTooltip ), p );
#endif

    /* toolbar */
    w = toolbar = p->toolbar = action_get_widget( "/main-window-toolbar" );

    /* filter */
    h = filter = p->filter = gtk_hbox_new( FALSE, 0 );
    gtk_container_set_border_width( GTK_CONTAINER( h ), GUI_PAD_SMALL );
    for( i = 0; i < FILTER_MODE_QTY; ++i )
    {
        const char * mnemonic = _( filter_names[i] );
        w = gtk_toggle_button_new_with_mnemonic( mnemonic );
        gtk_button_set_relief( GTK_BUTTON( w ), GTK_RELIEF_NONE );
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), i == FILTER_MODE_ALL );
        p->filter_toggles[i] = GTK_TOGGLE_BUTTON( w );
        g_signal_connect( w, "toggled", G_CALLBACK( filter_toggled_cb ), p );
        gtk_box_pack_start( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    }
    s = sexy_icon_entry_new( );
    sexy_icon_entry_add_clear_button( SEXY_ICON_ENTRY( s ) );
    image = gtk_image_new_from_stock( GTK_STOCK_FIND, GTK_ICON_SIZE_MENU );
    sexy_icon_entry_set_icon( SEXY_ICON_ENTRY( s ), SEXY_ICON_ENTRY_PRIMARY, GTK_IMAGE( image ) );
    sexy_icon_entry_set_icon_highlight( SEXY_ICON_ENTRY( s ), SEXY_ICON_ENTRY_PRIMARY, TRUE );
    gtk_box_pack_end( GTK_BOX( h ), s, FALSE, FALSE, 0 );
    g_signal_connect( s, "changed", G_CALLBACK( filter_entry_changed ), p );

    /* status menu */
    menu = p->status_menu = gtk_menu_new( );
    l = NULL;
    pch = pref_string_get( PREF_KEY_STATUSBAR_STATS );
    for( i = 0, n = G_N_ELEMENTS( stats_modes ); i < n; ++i )
    {
        const char * val = stats_modes[i].val;
        w = gtk_radio_menu_item_new_with_label( l, _( stats_modes[i].i18n ) );
        l = gtk_radio_menu_item_get_group( GTK_RADIO_MENU_ITEM( w ) );
        gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM( w ),
                                       !strcmp( val, pch ) );
        g_object_set_data( G_OBJECT(
                               w ), STATS_MODE,
                           (gpointer)stats_modes[i].val );
        g_signal_connect( w, "toggled", G_CALLBACK(
                              status_menu_toggled_cb ), p );
        gtk_menu_shell_append( GTK_MENU_SHELL( menu ), w );
        gtk_widget_show( w );
    }

    /* status */
    h = status = p->status = gtk_hbox_new( FALSE, GUI_PAD );
    gtk_container_set_border_width( GTK_CONTAINER( h ), GUI_PAD );
    w = p->gutter_lb = gtk_label_new( "N Torrents" );
    gtk_box_pack_start( GTK_BOX( h ), w, 0, 0, 0 );
    w = p->ul_lb = gtk_label_new( NULL );
    gtk_box_pack_end( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    w = gtk_image_new_from_stock( GTK_STOCK_GO_UP, GTK_ICON_SIZE_MENU );
    gtk_box_pack_end( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
    gtk_widget_set_size_request( w, GUI_PAD, 0u );
    gtk_box_pack_end( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    w = p->dl_lb = gtk_label_new( NULL );
    gtk_box_pack_end( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    w = gtk_image_new_from_stock( GTK_STOCK_GO_DOWN, GTK_ICON_SIZE_MENU );
    gtk_box_pack_end( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
    gtk_widget_set_size_request( w, GUI_PAD, 0u );
    gtk_box_pack_end( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    w = p->stats_lb = gtk_label_new( NULL );
    gtk_box_pack_end( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    w = gtk_image_new_from_stock( GTK_STOCK_REFRESH, GTK_ICON_SIZE_MENU );
    c = gtk_event_box_new( );
    gtk_container_add( GTK_CONTAINER( c ), w );
    w = c;
    gtk_box_pack_end( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    g_signal_connect( w, "button-release-event",
                      G_CALLBACK( onYinYangReleased ), p );

    menu = gtk_menu_new( );
    l = NULL;
    for( i = 0; i < FILTER_TEXT_MODE_QTY; ++i )
    {
        const char * name = _( filter_text_names[i] );
        GtkWidget *  w = gtk_radio_menu_item_new_with_label ( l, name );
        l = gtk_radio_menu_item_get_group( GTK_RADIO_MENU_ITEM( w ) );
        g_object_set_data( G_OBJECT( w ), FILTER_TEXT_MODE_KEY,
                           GINT_TO_POINTER( i ) );
        g_signal_connect( w, "toggled",
                          G_CALLBACK( filter_text_toggled_cb ), p );
        gtk_menu_shell_append( GTK_MENU_SHELL( menu ), w );
        gtk_widget_show( w );
    }
    g_signal_connect( s, "icon-released",
                      G_CALLBACK( entry_icon_released ), menu );

    /* workarea */
    p->view = makeview( p, core );
    w = list = p->scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( w ),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW( w ),
                                         GTK_SHADOW_IN );
    gtk_container_add( GTK_CONTAINER( w ), p->view );

    /* layout the widgets */
    {
        const char * str = pref_string_get( PREF_KEY_MAIN_WINDOW_LAYOUT_ORDER );
        char ** tokens = g_strsplit( str, ",", -1 );
        for( i=0; tokens && tokens[i]; ++i )
        {
            const char * key = tokens[i];

            if( !strcmp( key, "menu" ) )
                gtk_box_pack_start( GTK_BOX( vbox ), mainmenu, FALSE, FALSE, 0 );
            else if( !strcmp( key, "toolbar" ) )
                gtk_box_pack_start( GTK_BOX( vbox ), toolbar, FALSE, FALSE, 0 );
            else if( !strcmp( key, "filter" ) )
                gtk_box_pack_start( GTK_BOX( vbox ), filter, FALSE, FALSE, 0 );
            else if( !strcmp( key, "list" ) )
                gtk_box_pack_start( GTK_BOX( vbox ), list, TRUE, TRUE, 0 );
            else if( !strcmp( key, "statusbar" ) )
                gtk_box_pack_start( GTK_BOX( vbox ), status, FALSE, FALSE, 0 );
        }
        g_strfreev( tokens );
    }

    /* show all but the window */
    gtk_widget_show_all( vbox );

    /* listen for prefs changes that affect the window */
    p->core = core;
    prefsChanged( core, PREF_KEY_MINIMAL_VIEW, self );
    prefsChanged( core, PREF_KEY_FILTERBAR, self );
    prefsChanged( core, PREF_KEY_STATUSBAR, self );
    prefsChanged( core, PREF_KEY_STATUSBAR_STATS, self );
    prefsChanged( core, PREF_KEY_TOOLBAR, self );
    p->pref_handler_id = g_signal_connect( core, "prefs-changed",
                                           G_CALLBACK( prefsChanged ), self );

    filter_entry_changed( GTK_EDITABLE( s ), p );
    return self;
}

static void
updateTorrentCount( PrivateData * p )
{
    if( p && p->core )
    {
        char      buf[128];
        const int torrentCount = gtk_tree_model_iter_n_children(
            tr_core_model( p->core ), NULL );
        const int visibleCount = gtk_tree_model_iter_n_children(
            p->filter_model, NULL );

        if( torrentCount != visibleCount )
            g_snprintf( buf, sizeof( buf ),
                        ngettext( "%1$'d of %2$'d Torrent",
                                  "%1$'d of %2$'d Torrents",
                                  torrentCount ),
                        visibleCount, torrentCount );
        else
            g_snprintf( buf, sizeof( buf ), ngettext( "%'d Torrent",
                                                      "%'d Torrents",
                                                      torrentCount ),
                        torrentCount );
        gtk_label_set_text( GTK_LABEL( p->gutter_lb ), buf );
    }
}

static void
updateStats( PrivateData * p )
{
    const char *            pch;
    char                    up[32], down[32], ratio[32], buf[128];
    struct tr_session_stats stats;
    tr_session *            session = tr_core_session( p->core );

    /* update the stats */
    pch = pref_string_get( PREF_KEY_STATUSBAR_STATS );
    if( !strcmp( pch, "session-ratio" ) )
    {
        tr_sessionGetStats( session, &stats );
        tr_strlratio( ratio, stats.ratio, sizeof( ratio ) );
        g_snprintf( buf, sizeof( buf ), _( "Ratio: %s" ), ratio );
    }
    else if( !strcmp( pch, "session-transfer" ) )
    {
        tr_sessionGetStats( session, &stats );
        tr_strlsize( up, stats.uploadedBytes, sizeof( up ) );
        tr_strlsize( down, stats.downloadedBytes, sizeof( down ) );
        /* Translators: "size|" is here for disambiguation.  Please remove it from your translation.
           %1$s is the size of the data we've downloaded
           %2$s is the size of the data we've uploaded */
        g_snprintf( buf, sizeof( buf ), Q_(
                        "size|Down: %1$s, Up: %2$s" ), down, up );
    }
    else if( !strcmp( pch, "total-transfer" ) )
    {
        tr_sessionGetCumulativeStats( session, &stats );
        tr_strlsize( up, stats.uploadedBytes, sizeof( up ) );
        tr_strlsize( down, stats.downloadedBytes, sizeof( down ) );
        /* Translators: "size|" is here for disambiguation.  Please remove it from your translation.
           %1$s is the size of the data we've downloaded
           %2$s is the size of the data we've uploaded */
        g_snprintf( buf, sizeof( buf ), Q_(
                        "size|Down: %1$s, Up: %2$s" ), down, up );
    }
    else     /* default is total-ratio */
    {
        tr_sessionGetCumulativeStats( session, &stats );
        tr_strlratio( ratio, stats.ratio, sizeof( ratio ) );
        g_snprintf( buf, sizeof( buf ), _( "Ratio: %s" ), ratio );
    }
    gtk_label_set_text( GTK_LABEL( p->stats_lb ), buf );
}

static void
updateSpeeds( PrivateData * p )
{
    tr_session * session = tr_core_session( p->core );

    if( session != NULL )
    {
        char buf[128];
        double d;

        d = tr_sessionGetPieceSpeed( session, TR_DOWN );
        tr_strlspeed( buf, d, sizeof( buf ) );
        gtk_label_set_text( GTK_LABEL( p->dl_lb ), buf );

        d = tr_sessionGetPieceSpeed( session, TR_UP );
        tr_strlspeed( buf, d, sizeof( buf ) );
        gtk_label_set_text( GTK_LABEL( p->ul_lb ), buf );
    }
}

void
tr_window_update( TrWindow * self )
{
    PrivateData * p = get_private_data( self );

    if( p && p->core && tr_core_session( p->core ) )
    {
        updateSpeeds( p );
        updateTorrentCount( p );
        updateStats( p );
        refilter( p );
    }
}

GtkTreeSelection*
tr_window_get_selection( TrWindow * w )
{
    return get_private_data( w )->selection;
}

