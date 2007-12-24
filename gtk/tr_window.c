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
#include "conf.h"
#include "hig.h"
#include "sexy-icon-entry.h"
#include "torrent-cell-renderer.h"
#include "tr_prefs.h"
#include "tr_torrent.h"
#include "tr_window.h"
#include "util.h"

#if !GTK_CHECK_VERSION(2,8,0)
static void
gtk_tree_view_column_queue_resize( GtkTreeViewColumn * column ) /* yuck */
{
   const int spacing = gtk_tree_view_column_get_spacing( column );
   gtk_tree_view_column_set_spacing( column, spacing+1 );
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
    GtkWidget * scroll;
    GtkWidget * view;
    GtkWidget * toolbar;
    GtkWidget * filter;
    GtkWidget * status;
    GtkWidget * status_menu;
    GtkWidget * ul_lb;
    GtkWidget * dl_lb;
    GtkWidget * stats_lb;
    GtkWidget * gutter_lb;
    GtkTreeSelection  * selection;
    GtkCellRenderer   * renderer;
    GtkTreeViewColumn * column;
    GtkTreeModel * filter_model;
    TrCore * core;
    gulong pref_handler_id;
    filter_mode_t filter_mode;
    filter_text_mode_t filter_text_mode;
    char * filter_text;
}
PrivateData;

#define PRIVATE_DATA_KEY "private-data"

#define FILTER_MODE_KEY "tr-filter-mode"
#define FILTER_TEXT_MODE_KEY "tr-filter-text-mode"
#define FILTER_TOGGLES_KEY "tr-filter-toggles"

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

static gboolean is_row_visible( GtkTreeModel *, GtkTreeIter  *, gpointer );

static GtkWidget*
makeview( PrivateData * p, TrCore * core )
{
    GtkWidget         * view;
    GtkTreeViewColumn * col;
    GtkTreeSelection  * sel;
    GtkCellRenderer   * r;
    GtkTreeModel      * filter_model;

    view = gtk_tree_view_new();
    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(view), FALSE );

    p->selection = gtk_tree_view_get_selection( GTK_TREE_VIEW(view) );

    p->renderer = r = torrent_cell_renderer_new( );
    p->column = col = gtk_tree_view_column_new_with_attributes(
        _("Torrent"), r, "torrent", MC_TORRENT_RAW, NULL );
    g_object_set( G_OBJECT(col), "resizable", TRUE,
                                 "sizing", GTK_TREE_VIEW_COLUMN_FIXED,
                                 NULL );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );
    g_object_set( r, "xpad", GUI_PAD_SMALL, "ypad", GUI_PAD_SMALL, NULL );

    gtk_tree_view_set_rules_hint( GTK_TREE_VIEW( view ), TRUE );
    sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );
    gtk_tree_selection_set_mode( GTK_TREE_SELECTION( sel ),
                                 GTK_SELECTION_MULTIPLE );

    g_signal_connect( view, "popup-menu",
                      G_CALLBACK(on_popup_menu), NULL );
    g_signal_connect( view, "button-press-event",
                      G_CALLBACK(on_tree_view_button_pressed),
                      (void *) on_popup_menu);
    g_signal_connect( view, "row-activated",
                      G_CALLBACK(view_row_activated), NULL);


    filter_model = p->filter_model = gtk_tree_model_filter_new( tr_core_model( core ), NULL );

    gtk_tree_model_filter_set_visible_func( GTK_TREE_MODEL_FILTER( filter_model ),
                                            is_row_visible,
                                            p, NULL );

    gtk_tree_view_set_model( GTK_TREE_VIEW( view ), filter_model );

    return view;
}

static void
realized_cb ( GtkWidget * wind, gpointer unused UNUSED )
{
    PrivateData * p = get_private_data( GTK_WINDOW( wind ) );
    sizingmagic( GTK_WINDOW(wind),
                 GTK_SCROLLED_WINDOW( p->scroll ),
                 GTK_POLICY_NEVER,
                 GTK_POLICY_ALWAYS );
}

static void
prefsChanged( TrCore * core UNUSED, const char * key, gpointer wind )
{
    PrivateData * p = get_private_data( GTK_WINDOW( wind ) );

    if( !strcmp( key, PREF_KEY_MINIMAL_VIEW ) )
    {
       g_object_set( p->renderer, "minimal", pref_flag_get( key ), NULL );
       gtk_tree_view_column_queue_resize( p->column );
    }
    else if( !strcmp( key, PREF_KEY_STATUS_BAR ) )
    {
        const gboolean isEnabled = pref_flag_get( key );
        g_object_set( p->status, "visible", isEnabled, NULL );
    }
    else if( !strcmp( key, PREF_KEY_FILTER_BAR ) )
    {
        const gboolean isEnabled = pref_flag_get( key );
        g_object_set( p->filter, "visible", isEnabled, NULL );
    }
    else if( !strcmp( key, PREF_KEY_TOOLBAR ) )
    {
        const gboolean isEnabled = pref_flag_get( key );
        g_object_set( p->toolbar, "visible", isEnabled, NULL );
    }
    else if( !strcmp( key, PREF_KEY_STATUS_BAR_STATS ) )
    {
        tr_window_update( (TrWindow*)wind );
    }
}

static void
privateFree( gpointer vprivate )
{
    PrivateData * p = ( PrivateData * ) vprivate;
    g_signal_handler_disconnect( p->core, p->pref_handler_id );
    g_free( p );
}

static void
onYinYangReleased( GtkWidget * w UNUSED, GdkEventButton * button UNUSED, gpointer vprivate )
{
    PrivateData * p = ( PrivateData * ) vprivate;
    gtk_menu_popup( GTK_MENU( p->status_menu ), 0, 0, 0, 0, 0, gtk_get_current_event_time( ) );
}

#define STATS_MODE "stats-mode"

static struct {
    const char *val, *i18n;
} stats_modes[] = {
    { "total-ratio",      N_("Total Ratio") },
    { "session-ratio",    N_("Session Ratio") },
    { "total-transfer",   N_("Total Transfer") },
    { "session-transfer", N_("Session Transfer") }
};

static void
status_menu_toggled_cb( GtkCheckMenuItem  * menu_item,
                        gpointer            vprivate )
{
    if( gtk_check_menu_item_get_active( menu_item ) )
    {
        PrivateData * p = (PrivateData*) vprivate;
        const char * val = g_object_get_data( G_OBJECT( menu_item ), STATS_MODE );
        tr_core_set_pref( p->core, PREF_KEY_STATUS_BAR_STATS, val );
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
    int i;
    int ret = 0;
    char * pch;

    switch( filter_text_mode )
    {
        case FILTER_TEXT_MODE_FILES:
            for( i=0; i<torInfo->fileCount && !ret; ++i ) {
                pch = g_ascii_strdown( torInfo->files[i].name, -1 );
                ret = !text || strstr( pch, text ) != NULL;
                g_free( pch );
            }
            break;

        case FILTER_TEXT_MODE_TRACKER:
            pch = g_ascii_strdown( torInfo->primaryAddress, -1 );
            ret = !text || ( strstr( pch, text ) != NULL );
            g_free( pch );
            break;

        default: /* NAME */
            pch = g_ascii_strdown( torInfo->name, -1 );
            ret = !text || ( strstr( pch, text ) != NULL );
            g_free( pch );
            break;
    }

    return ret;
}

static int
checkFilterMode( filter_mode_t       filter_mode,
                 const tr_stat     * torStat )
{
    int ret = 0;
    const int haveDown = ((int)( torStat->rateDownload * 1024 )) > 1.0;
    const int haveUp = ((int)( torStat->rateUpload * 1024 )) > 1.0;

    switch( filter_mode )
    {
        case FILTER_MODE_ACTIVE:
            ret = haveUp || haveDown;
            break;
        case FILTER_MODE_DOWNLOADING:
            ret = torStat->status == TR_STATUS_DOWNLOAD;
            break;
        case FILTER_MODE_SEEDING:
            ret =    ( torStat->status == TR_STATUS_DONE )
                  || ( torStat->status == TR_STATUS_SEED );
            break;
        case FILTER_MODE_PAUSED:
            ret = torStat->status == TR_STATUS_STOPPED;
            break;
        default: /* all */
            ret = 1;
    }

    return ret;
}

static gboolean
is_row_visible( GtkTreeModel   * model,
                GtkTreeIter    * iter,
                gpointer         vprivate )
{
    PrivateData * p = vprivate;
    tr_torrent * tor;
    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );

    return checkFilterMode( p->filter_mode, tr_torrentStatCached( tor ) )
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
filter_text_toggled_cb( GtkCheckMenuItem  * menu_item,
                        gpointer            vprivate )
{
    if( gtk_check_menu_item_get_active( menu_item ) )
    {
        PrivateData * p = (PrivateData*) vprivate;
        p->filter_text_mode = GPOINTER_TO_UINT( g_object_get_data( G_OBJECT( menu_item ),
                                                                   FILTER_TEXT_MODE_KEY ) );
        refilter( p );
    }
}

static void
filter_toggled_cb( GtkToggleButton * toggle, gpointer vprivate )
{
    PrivateData * p = (PrivateData*) vprivate;
    GSList * l;
    GSList * toggles = g_object_get_data( G_OBJECT( toggle ), FILTER_TOGGLES_KEY );
    const gboolean isActive = gtk_toggle_button_get_active( toggle );
    const filter_mode_t mode = GPOINTER_TO_UINT( g_object_get_data( G_OBJECT( toggle ), FILTER_MODE_KEY ) );

    /* update the filter */
    if( isActive ) {
        p->filter_mode = mode;
        refilter( p );
    }

    /* deactivate the other toggles */
    for( l=toggles; l!=NULL; l=l->next ) {
        GtkToggleButton * walk = GTK_TOGGLE_BUTTON( l->data );
        if( isActive && ( toggle != walk ) )
            gtk_toggle_button_set_active( walk, FALSE );
    }

    /* at least one button must always be set */
    if( !isActive && ( p->filter_mode == mode ) )
        gtk_toggle_button_set_active( toggle, TRUE );
}

static void
filter_entry_changed( GtkEditable * e, gpointer vprivate )
{
    char * pch;
    PrivateData * p = (PrivateData*) vprivate;
    g_free( p->filter_text );
    pch = gtk_editable_get_chars( e, 0, -1 );
    p->filter_text = g_ascii_strdown( pch, -1 );
    refilter( p );
    g_free( pch );
}

static void
entry_icon_released( SexyIconEntry         * entry UNUSED,
                     SexyIconEntryPosition   icon_pos,
                     int                     button UNUSED,
                     gpointer                menu )
{
    if ( icon_pos == SEXY_ICON_ENTRY_PRIMARY )
        gtk_menu_popup ( GTK_MENU( menu ), 0, 0, 0, 0, 0, gtk_get_current_event_time( ) );
}

static void
entry_icon_released_2( SexyIconEntry         * entry,
                       SexyIconEntryPosition   icon_pos,
                       int                     button UNUSED,
                       gpointer                vprivate UNUSED )
{
    if ( icon_pos == SEXY_ICON_ENTRY_SECONDARY )
        gtk_editable_delete_text( GTK_EDITABLE( entry ), 0, -1 );
}


/***
****  PUBLIC
***/

GtkWidget *
tr_window_new( GtkUIManager * ui_manager, TrCore * core )
{
    int i, n;
    int status_stats_mode;
    char * pch;
    PrivateData * p = g_new( PrivateData, 1 );
    GtkWidget *vbox, *w, *self, *h, *c, *s, *image, *menu;
    GSList * l;
    GSList * toggles;
    const char * filter_names[FILTER_MODE_QTY] = {
        N_( "A_ll"), N_("_Active"),
        N_("_Downloading"), N_("_Seeding"), N_("_Paused")
    };
    const char * filter_text_names[FILTER_TEXT_MODE_QTY] = {
        N_("Name"), N_("Files"), N_("Tracker")
    };

    p->filter_mode = FILTER_MODE_ALL;
    p->filter_text_mode = FILTER_TEXT_MODE_NAME;
    p->filter_text = NULL;

    /* make the window */
    self = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    g_object_set_data_full(G_OBJECT(self), PRIVATE_DATA_KEY, p, privateFree );
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
    w = p->toolbar = action_get_widget( "/main-window-toolbar" );
    gtk_box_pack_start( GTK_BOX(vbox), w, FALSE, FALSE, 0 ); 

    /* status menu */
    menu = p->status_menu = gtk_menu_new( );
    status_stats_mode = 0;
    l = NULL;
    pch = pref_string_get( PREF_KEY_STATUS_BAR_STATS );
    for( i=0, n=G_N_ELEMENTS(stats_modes); i<n; ++i )
    {
        const char * val = stats_modes[i].val;
        w = gtk_radio_menu_item_new_with_label( l, _( stats_modes[i].i18n ) );
        l = gtk_radio_menu_item_get_group( GTK_RADIO_MENU_ITEM(w) );
        gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM(w), !strcmp( val, pch ) );
        g_object_set_data( G_OBJECT(w), STATS_MODE, (gpointer)stats_modes[i].val );
        g_signal_connect( w, "toggled", G_CALLBACK(status_menu_toggled_cb), p );
        gtk_menu_shell_append( GTK_MENU_SHELL(menu), w );
        gtk_widget_show( w );
    }
    g_free( pch );

    /* statusbar */
    h = p->status = gtk_hbox_new( FALSE, GUI_PAD );
    gtk_container_set_border_width( GTK_CONTAINER(h), GUI_PAD );
     
    w = p->ul_lb = gtk_label_new( NULL );
    gtk_box_pack_end( GTK_BOX(h), w, FALSE, FALSE, 0 );
    w = gtk_image_new_from_stock( "tr-arrow-up", (GtkIconSize)-1 );
    gtk_box_pack_end( GTK_BOX(h), w, FALSE, FALSE, 0 );
    w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
    gtk_widget_set_usize( w, GUI_PAD, 0u );
    gtk_box_pack_end( GTK_BOX(h), w, FALSE, FALSE, 0 );
    w = p->dl_lb = gtk_label_new( NULL );
    gtk_box_pack_end( GTK_BOX(h), w, FALSE, FALSE, 0 );
    w = gtk_image_new_from_stock( "tr-arrow-down", (GtkIconSize)-1 );
    gtk_box_pack_end( GTK_BOX(h), w, FALSE, FALSE, 0 );

    w = gtk_image_new_from_stock( "tr-yin-yang", (GtkIconSize)-1 );
    c = gtk_event_box_new( );
    gtk_container_add( GTK_CONTAINER(c), w );
    w = c;
    g_signal_connect( w, "button-release-event", G_CALLBACK(onYinYangReleased), p );
    gtk_box_pack_start( GTK_BOX(h), w, FALSE, FALSE, 0 );
    w = p->stats_lb = gtk_label_new( NULL );
    gtk_box_pack_start( GTK_BOX(h), w, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox), h, FALSE, FALSE, 0 );

    w = gtk_hseparator_new( );
    gtk_box_pack_start( GTK_BOX(vbox), w, FALSE, FALSE, 0 ); 

    /* filter */
    toggles = NULL;
    h = p->filter = gtk_hbox_new( FALSE, 0 );
    gtk_container_set_border_width( GTK_CONTAINER( h ), GUI_PAD_SMALL );
    for( i=0; i<FILTER_MODE_QTY; ++i ) {
        const char * mnemonic = _( filter_names[i] );
        w = gtk_toggle_button_new_with_mnemonic( mnemonic );
        g_object_set_data( G_OBJECT( w ), FILTER_MODE_KEY, GINT_TO_POINTER( i ) );
        gtk_button_set_relief( GTK_BUTTON( w ), GTK_RELIEF_NONE );
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), i==FILTER_MODE_ALL );
        toggles = g_slist_prepend( toggles, w );
        g_signal_connect( w, "toggled", G_CALLBACK(filter_toggled_cb), p );
        gtk_box_pack_start( GTK_BOX( h ), w, FALSE, FALSE, 0 ); 
    }
    for( l=toggles; l!=NULL; l=l->next )
        g_object_set_data( G_OBJECT( l->data ), FILTER_TOGGLES_KEY, toggles );
    s = sexy_icon_entry_new( );
    image = gtk_image_new_from_stock(GTK_STOCK_CLEAR, GTK_ICON_SIZE_MENU );
    sexy_icon_entry_set_icon( SEXY_ICON_ENTRY(s), SEXY_ICON_ENTRY_SECONDARY, GTK_IMAGE(image) );
    sexy_icon_entry_set_icon_highlight( SEXY_ICON_ENTRY(s), SEXY_ICON_ENTRY_SECONDARY, TRUE );
    image = gtk_image_new_from_stock( "tr-search-pulldown", GTK_ICON_SIZE_MENU );
    sexy_icon_entry_set_icon( SEXY_ICON_ENTRY(s), SEXY_ICON_ENTRY_PRIMARY, GTK_IMAGE(image) );
    sexy_icon_entry_set_icon_highlight( SEXY_ICON_ENTRY(s), SEXY_ICON_ENTRY_PRIMARY, TRUE );
    gtk_box_pack_end( GTK_BOX( h ), s, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox), h, FALSE, FALSE, 0 ); 
    g_signal_connect( s, "changed", G_CALLBACK( filter_entry_changed ), p );

    menu = gtk_menu_new( );
    l = NULL;
    for( i=0; i<FILTER_TEXT_MODE_QTY; ++i )
    {
        const char * name = _( filter_text_names[i] );
        GtkWidget * w = gtk_radio_menu_item_new_with_label ( l, name );
        l = gtk_radio_menu_item_get_group( GTK_RADIO_MENU_ITEM( w ) );
        g_object_set_data( G_OBJECT( w ), FILTER_TEXT_MODE_KEY, GINT_TO_POINTER( i ) );
        g_signal_connect( w, "toggled", G_CALLBACK( filter_text_toggled_cb ), p );
        gtk_menu_shell_append( GTK_MENU_SHELL( menu ), w );
        gtk_widget_show( w );
    }
    g_signal_connect( s, "icon-released", G_CALLBACK(entry_icon_released), menu );
    g_signal_connect( s, "icon-released", G_CALLBACK( entry_icon_released_2 ), p );


    /* workarea */
    p->view = makeview( p, core );
    w = p->scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(w), p->view );
    gtk_box_pack_start_defaults( GTK_BOX(vbox), w );
    gtk_container_set_focus_child( GTK_CONTAINER( vbox ), w );

    /* spacer */
    w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
    gtk_widget_set_usize (w, 0u, 6u);
    gtk_box_pack_start( GTK_BOX(vbox), w, FALSE, FALSE, 0 ); 

    /* status */
    h = gtk_hbox_new( FALSE, GUI_PAD );
    w = p->gutter_lb = gtk_label_new( "N transfers" );
    gtk_box_pack_start_defaults( GTK_BOX(h), w );
    gtk_box_pack_start( GTK_BOX(vbox), h, FALSE, FALSE, 0 ); 

    /* show all but the window */
    gtk_widget_show_all( vbox );

    /* listen for prefs changes that affect the window */
    p->core = core;
    prefsChanged( core, PREF_KEY_MINIMAL_VIEW, self );
    prefsChanged( core, PREF_KEY_FILTER_BAR, self );
    prefsChanged( core, PREF_KEY_STATUS_BAR, self );
    prefsChanged( core, PREF_KEY_STATUS_BAR_STATS, self );
    prefsChanged( core, PREF_KEY_TOOLBAR, self );
    p->pref_handler_id = g_signal_connect( core, "prefs-changed",
                                           G_CALLBACK(prefsChanged), self );

    return self;
}

static void
updateTorrentCount( PrivateData * p )
{
    if( p && p->core )
    {
        char buf[128];
        const int torrentCount = gtk_tree_model_iter_n_children( tr_core_model( p->core ), NULL );
        const int visibleCount = gtk_tree_model_iter_n_children( p->filter_model, NULL );

        if( torrentCount != visibleCount )
            g_snprintf( buf, sizeof( buf ), ngettext( _( "%d of %d Transfer" ),
                                                      _( "%d of %d Transfers" ),
                                                      torrentCount ),
                                            visibleCount, torrentCount );
        else
            g_snprintf( buf, sizeof( buf ), ngettext( _( "%d Transfer" ),
                                                      _( "%d Transfers" ),
                                                      torrentCount ),
                                            torrentCount );
        gtk_label_set_text( GTK_LABEL( p->gutter_lb ), buf );
    }
}


void
tr_window_update( TrWindow * self )
{
    PrivateData * p = get_private_data( self );
    tr_handle * handle = NULL;

    if( p && p->core )
        handle = tr_core_handle( p->core );

    if( handle )
    {
        char * pch;
        float u, d;
        char up[32], down[32], buf[128];
        struct tr_session_stats stats;

        /* update the speeds */
        tr_torrentRates( handle, &d, &u );
        tr_strlspeed( buf, d, sizeof( buf ) );
        gtk_label_set_text( GTK_LABEL( p->dl_lb ), buf );
        tr_strlspeed( buf, u, sizeof( buf ) );
        gtk_label_set_text( GTK_LABEL( p->ul_lb ), buf );

        updateTorrentCount( p );

        /* update the stats */
        pch = pref_string_get( PREF_KEY_STATUS_BAR_STATS );
        if( !strcmp( pch, "session-ratio" ) ) {
            tr_getSessionStats( handle, &stats );
            g_snprintf( buf, sizeof(buf), _("Ratio: %.1f"), stats.ratio );
        } else if( !strcmp( pch, "session-transfer" ) ) {
            tr_getSessionStats( handle, &stats );
            tr_strlsize( up, stats.uploadedBytes, sizeof( up ) );
            tr_strlsize( down, stats.downloadedBytes, sizeof( down ) );
            g_snprintf( buf, sizeof( buf ), _( "Down: %s  Up: %s" ), down, up );
        } else if( !strcmp( pch, "total-transfer" ) ) { 
            tr_getCumulativeSessionStats( handle, &stats );
            tr_strlsize( up, stats.uploadedBytes, sizeof( up ) );
            tr_strlsize( down, stats.downloadedBytes, sizeof( down ) );
            g_snprintf( buf, sizeof( buf ), _( "Down: %s  Up: %s" ), down, up );
        } else { /* default is total-ratio */
            tr_getCumulativeSessionStats( handle, &stats );
            g_snprintf( buf, sizeof(buf), _("Ratio: %.1f"), stats.ratio );
        }
        g_free( pch );
        gtk_label_set_text( GTK_LABEL( p->stats_lb ), buf );
    }
}

GtkTreeSelection*
tr_window_get_selection ( TrWindow * w )
{
    return get_private_data(w)->selection;
}
