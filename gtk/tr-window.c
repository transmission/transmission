/******************************************************************************
 * $Id$
 *
 * Copyright (c) Transmission authors and contributors
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
#include <libtransmission/utils.h> /* tr_formatter_speed_KBps() */

#include "actions.h"
#include "conf.h"
#include "filter.h"
#include "hig.h"
#include "torrent-cell-renderer.h"
#include "tr-prefs.h"
#include "tr-window.h"
#include "util.h"

typedef struct
{
    GtkWidget *           speedlimit_on_item[2];
    GtkWidget *           speedlimit_off_item[2];
    GtkWidget *           ratio_on_item;
    GtkWidget *           ratio_off_item;
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
    GtkWidget *           alt_speed_image;
    GtkWidget *           alt_speed_button;
    GtkWidget *           options_menu;
    GtkTreeSelection *    selection;
    GtkCellRenderer *     renderer;
    GtkTreeViewColumn *   column;
    GtkTreeModel *        filter_model;
    TrCore *              core;
    gulong                pref_handler_id;
}
PrivateData;

static GQuark
get_private_data_key( void )
{
    static GQuark q = 0;
    if( !q ) q = g_quark_from_static_string( "private-data" );
    return q;
}

static PrivateData*
get_private_data( TrWindow * w )
{
    return g_object_get_qdata ( G_OBJECT( w ), get_private_data_key( ) );
}

/***
****
***/

static void
on_popup_menu( GtkWidget * self UNUSED,
               GdkEventButton * event )
{
    GtkWidget * menu = gtr_action_get_widget ( "/main-window-popup" );

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
    gtr_action_activate( "show-torrent-properties" );
}

static GtkWidget*
makeview( PrivateData * p )
{
    GtkWidget *         view;
    GtkTreeViewColumn * col;
    GtkTreeSelection *  sel;
    GtkCellRenderer *   r;

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
    gtk_tree_view_column_add_attribute( col, r, "torrent", MC_TORRENT );
    gtk_tree_view_column_add_attribute( col, r, "piece-upload-speed", MC_SPEED_UP );
    gtk_tree_view_column_add_attribute( col, r, "piece-download-speed", MC_SPEED_DOWN );

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


    gtk_tree_view_set_model( GTK_TREE_VIEW( view ), p->filter_model );
    g_object_unref( p->filter_model );

    return view;
}

static void syncAltSpeedButton( PrivateData * p );

static void
prefsChanged( TrCore * core UNUSED,
              const char *  key,
              gpointer      wind )
{
    PrivateData * p = get_private_data( GTK_WINDOW( wind ) );

    if( !strcmp( key, PREF_KEY_COMPACT_VIEW ) )
    {
        g_object_set( p->renderer, "compact", gtr_pref_flag_get( key ), NULL );
        /* since the cell size has changed, we need gtktreeview to revalidate
         * its fixed-height mode values. Unfortunately there's not an API call
         * for that, but it *does* revalidate when it thinks the style's been tweaked */
#if GTK_CHECK_VERSION( 3,0,0 )
        g_signal_emit_by_name( p->view, "style-updated", NULL, NULL );
#else
        g_signal_emit_by_name( p->view, "style-set", NULL, NULL );
#endif
    }
    else if( !strcmp( key, PREF_KEY_STATUSBAR ) )
    {
        const gboolean isEnabled = gtr_pref_flag_get( key );
        g_object_set( p->status, "visible", isEnabled, NULL );
    }
    else if( !strcmp( key, PREF_KEY_FILTERBAR ) )
    {
        const gboolean isEnabled = gtr_pref_flag_get( key );
        g_object_set( p->filter, "visible", isEnabled, NULL );
    }
    else if( !strcmp( key, PREF_KEY_TOOLBAR ) )
    {
        const gboolean isEnabled = gtr_pref_flag_get( key );
        g_object_set( p->toolbar, "visible", isEnabled, NULL );
    }
    else if( !strcmp( key, PREF_KEY_STATUSBAR_STATS ) )
    {
        gtr_window_refresh( (TrWindow*)wind );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_ENABLED ) ||
             !strcmp( key, TR_PREFS_KEY_ALT_SPEED_UP_KBps ) ||
             !strcmp( key, TR_PREFS_KEY_ALT_SPEED_DOWN_KBps ) )
    {
        syncAltSpeedButton( p );
    }
}

static void
privateFree( gpointer vprivate )
{
    PrivateData * p = vprivate;
    g_signal_handler_disconnect( p->core, p->pref_handler_id );
    g_free( p );
}

static void
onYinYangReleased( GtkWidget * w UNUSED, gpointer vprivate )
{
    PrivateData * p = vprivate;

    gtk_menu_popup( GTK_MENU( p->status_menu ),
                    NULL, NULL, NULL, NULL, 0,
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
        gtr_core_set_pref( p->core, PREF_KEY_STATUSBAR_STATS, val );
    }
}

static void
syncAltSpeedButton( PrivateData * p )
{
    char u[32];
    char d[32];
    char * str;
    const char * fmt;
    const gboolean b = gtr_pref_flag_get( TR_PREFS_KEY_ALT_SPEED_ENABLED );
    const char * stock = b ? "alt-speed-on" : "alt-speed-off";
    GtkWidget * w = p->alt_speed_button;

    tr_formatter_speed_KBps( u, gtr_pref_int_get( TR_PREFS_KEY_ALT_SPEED_UP_KBps ), sizeof( u ) );
    tr_formatter_speed_KBps( d, gtr_pref_int_get( TR_PREFS_KEY_ALT_SPEED_DOWN_KBps ), sizeof( d ) );
    fmt = b ? _( "Click to disable Alternative Speed Limits\n(%1$s down, %2$s up)" )
            : _( "Click to enable Alternative Speed Limits\n(%1$s down, %2$s up)" );
    str = g_strdup_printf( fmt, d, u );

    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), b );
    gtk_image_set_from_stock( GTK_IMAGE( p->alt_speed_image ), stock, -1 );
    gtk_button_set_alignment( GTK_BUTTON( w ), 0.5, 0.5 );
    gtr_widget_set_tooltip_text( w, str );

    g_free( str );
}

static void
alt_speed_toggled_cb( GtkToggleButton * button, gpointer vprivate )
{
    PrivateData * p = vprivate;
    const gboolean b = gtk_toggle_button_get_active( button );
    gtr_core_set_pref_bool( p->core, TR_PREFS_KEY_ALT_SPEED_ENABLED,  b );
}

/***
****  FILTER
***/

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

    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
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
        char      buf[512];
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

static gboolean
onAltSpeedToggledIdle( gpointer vp )
{
    PrivateData * p = vp;
    gboolean b = tr_sessionUsesAltSpeed( gtr_core_session( p->core ) );
    gtr_core_set_pref_bool( p->core, TR_PREFS_KEY_ALT_SPEED_ENABLED, b );

    return FALSE;
}

static void
onAltSpeedToggled( tr_session * s UNUSED, bool isEnabled UNUSED, bool byUser UNUSED, void * p )
{
    gtr_idle_add( onAltSpeedToggledIdle, p );
}

/***
****  Speed limit menu
***/

#define DIRECTION_KEY "direction-key"
#define ENABLED_KEY "enabled-key"
#define SPEED_KEY "speed-key"

static void
onSpeedToggled( GtkCheckMenuItem * check, gpointer vp )
{
    PrivateData * p = vp;
    GObject * o = G_OBJECT( check );
    gboolean isEnabled = g_object_get_data( o, ENABLED_KEY ) != 0;
    tr_direction dir = GPOINTER_TO_INT( g_object_get_data( o, DIRECTION_KEY ) );
    const char * key = dir == TR_UP ? TR_PREFS_KEY_USPEED_ENABLED
                                    : TR_PREFS_KEY_DSPEED_ENABLED;

    if( gtk_check_menu_item_get_active( check ) )
        gtr_core_set_pref_bool( p->core, key, isEnabled );
}

static void
onSpeedSet( GtkCheckMenuItem * check, gpointer vp )
{
    const char * key;
    PrivateData * p = vp;
    GObject * o = G_OBJECT( check );
    const int KBps = GPOINTER_TO_INT( g_object_get_data( o, SPEED_KEY ) );
    tr_direction dir = GPOINTER_TO_INT( g_object_get_data( o, DIRECTION_KEY ) );

    key = dir==TR_UP ? TR_PREFS_KEY_USPEED_KBps : TR_PREFS_KEY_DSPEED_KBps;
    gtr_core_set_pref_int( p->core, key, KBps );

    key = dir==TR_UP ? TR_PREFS_KEY_USPEED_ENABLED : TR_PREFS_KEY_DSPEED_ENABLED;
    gtr_core_set_pref_bool( p->core, key, TRUE );
}

static GtkWidget*
createSpeedMenu( PrivateData * p, tr_direction dir )
{
    int i, n;
    GtkWidget *w, *m;
    const int speeds_KBps[] = { 5, 10, 20, 30, 40, 50, 75, 100, 150, 200, 250, 500, 750 };

    m = gtk_menu_new( );

    w = gtk_radio_menu_item_new_with_label( NULL, _( "Unlimited" ) );
    p->speedlimit_off_item[dir] = w;
    g_object_set_data( G_OBJECT( w ), DIRECTION_KEY, GINT_TO_POINTER( dir ) );
    g_object_set_data( G_OBJECT( w ), ENABLED_KEY, GINT_TO_POINTER( FALSE ) );
    g_signal_connect( w, "toggled", G_CALLBACK(onSpeedToggled), p );
    gtk_menu_shell_append( GTK_MENU_SHELL( m ), w );

    w = gtk_radio_menu_item_new_with_label_from_widget( GTK_RADIO_MENU_ITEM( w ), "" );
    p->speedlimit_on_item[dir] = w;
    g_object_set_data( G_OBJECT( w ), DIRECTION_KEY, GINT_TO_POINTER( dir ) );
    g_object_set_data( G_OBJECT( w ), ENABLED_KEY, GINT_TO_POINTER( TRUE ) );
    g_signal_connect( w, "toggled", G_CALLBACK(onSpeedToggled), p );
    gtk_menu_shell_append( GTK_MENU_SHELL( m ), w );

    w = gtk_separator_menu_item_new( );
    gtk_menu_shell_append( GTK_MENU_SHELL( m ), w );

    for( i=0, n=G_N_ELEMENTS(speeds_KBps); i<n; ++i )
    {
        char buf[128];
        tr_formatter_speed_KBps( buf, speeds_KBps[i], sizeof( buf ) );
        w = gtk_menu_item_new_with_label( buf );
        g_object_set_data( G_OBJECT( w ), DIRECTION_KEY, GINT_TO_POINTER( dir ) );
        g_object_set_data( G_OBJECT( w ), SPEED_KEY, GINT_TO_POINTER( speeds_KBps[i] ) );
        g_signal_connect( w, "activate", G_CALLBACK(onSpeedSet), p );
        gtk_menu_shell_append( GTK_MENU_SHELL( m ), w );
    }

    return m;
}

/***
****  Speed limit menu
***/

#define RATIO_KEY "stock-ratio-index"

static const double stockRatios[] = { 0.25, 0.5, 0.75, 1, 1.5, 2, 3 };

static void
onRatioToggled( GtkCheckMenuItem * check, gpointer vp )
{
    PrivateData * p = vp;
    if( gtk_check_menu_item_get_active( check ) )
    {
        gboolean f = g_object_get_data( G_OBJECT( check ), ENABLED_KEY ) != 0;
        gtr_core_set_pref_bool( p->core, TR_PREFS_KEY_RATIO_ENABLED, f );
    }
}
static void
onRatioSet( GtkCheckMenuItem * check, gpointer vp )
{
    PrivateData * p = vp;
    int i = GPOINTER_TO_INT( g_object_get_data( G_OBJECT( check ), RATIO_KEY ) );
    const double ratio = stockRatios[i];
    gtr_core_set_pref_double( p->core, TR_PREFS_KEY_RATIO, ratio );
    gtr_core_set_pref_bool  ( p->core, TR_PREFS_KEY_RATIO_ENABLED, TRUE );
}

static GtkWidget*
createRatioMenu( PrivateData * p )
{
    int i, n;
    GtkWidget *m, *w;

    m = gtk_menu_new( );

    w = gtk_radio_menu_item_new_with_label( NULL, _( "Seed Forever" ) );
    p->ratio_off_item = w;
    g_object_set_data( G_OBJECT( w ), ENABLED_KEY, GINT_TO_POINTER( FALSE ) );
    g_signal_connect( w, "toggled", G_CALLBACK(onRatioToggled), p );
    gtk_menu_shell_append( GTK_MENU_SHELL( m ), w );

    w = gtk_radio_menu_item_new_with_label_from_widget( GTK_RADIO_MENU_ITEM( w ), "" );
    p->ratio_on_item = w;
    g_object_set_data( G_OBJECT( w ), ENABLED_KEY, GINT_TO_POINTER( TRUE ) );
    g_signal_connect( w, "toggled", G_CALLBACK(onRatioToggled), p );
    gtk_menu_shell_append( GTK_MENU_SHELL( m ), w );

    w = gtk_separator_menu_item_new( );
    gtk_menu_shell_append( GTK_MENU_SHELL( m ), w );

    for( i=0, n=G_N_ELEMENTS(stockRatios); i<n; ++i )
    {
        char buf[128];
        tr_strlratio( buf, stockRatios[i], sizeof( buf ) );
        w = gtk_menu_item_new_with_label( buf );
        g_object_set_data( G_OBJECT( w ), RATIO_KEY, GINT_TO_POINTER( i ) );
        g_signal_connect( w, "activate", G_CALLBACK(onRatioSet), p );
        gtk_menu_shell_append( GTK_MENU_SHELL( m ), w );
    }

    return m;
}

/***
****  Option menu
***/

static GtkWidget*
createOptionsMenu( PrivateData * p )
{
    GtkWidget * m;
    GtkWidget * top = gtk_menu_new( );

    m = gtk_menu_item_new_with_label( _( "Limit Download Speed" ) );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM( m ), createSpeedMenu( p, TR_DOWN ) );
    gtk_menu_shell_append( GTK_MENU_SHELL( top ), m );

    m = gtk_menu_item_new_with_label( _( "Limit Upload Speed" ) );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM( m ), createSpeedMenu( p, TR_UP ) );
    gtk_menu_shell_append( GTK_MENU_SHELL( top ), m );

    m = gtk_separator_menu_item_new( );
    gtk_menu_shell_append( GTK_MENU_SHELL( top ), m );

    m = gtk_menu_item_new_with_label( _( "Stop Seeding at Ratio" ) );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM( m ), createRatioMenu( p ) );
    gtk_menu_shell_append( GTK_MENU_SHELL( top ), m );

    gtk_widget_show_all( top );
    return top;
}

static void
onOptionsClicked( GtkButton * button UNUSED, gpointer vp )
{
    char buf1[512];
    char buf2[512];
    gboolean b;
    GtkWidget * w;
    PrivateData * p = vp;

    w = p->speedlimit_on_item[TR_DOWN];
    tr_formatter_speed_KBps( buf1, gtr_pref_int_get( TR_PREFS_KEY_DSPEED_KBps ), sizeof( buf1 ) );
    gtr_label_set_text( GTK_LABEL( gtk_bin_get_child( GTK_BIN( w ) ) ), buf1 );

    b = gtr_pref_flag_get( TR_PREFS_KEY_DSPEED_ENABLED );
    w = b ? p->speedlimit_on_item[TR_DOWN] : p->speedlimit_off_item[TR_DOWN];
    gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM( w ), TRUE );

    w = p->speedlimit_on_item[TR_UP];
    tr_formatter_speed_KBps( buf1, gtr_pref_int_get( TR_PREFS_KEY_USPEED_KBps ), sizeof( buf1 ) );
    gtr_label_set_text( GTK_LABEL( gtk_bin_get_child( GTK_BIN( w ) ) ), buf1 );

    b = gtr_pref_flag_get( TR_PREFS_KEY_USPEED_ENABLED );
    w = b ? p->speedlimit_on_item[TR_UP] : p->speedlimit_off_item[TR_UP];
    gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM( w ), TRUE );

    tr_strlratio( buf1, gtr_pref_double_get( TR_PREFS_KEY_RATIO ), sizeof( buf1 ) );
    g_snprintf( buf2, sizeof( buf2 ), _( "Stop at Ratio (%s)" ), buf1 );
    gtr_label_set_text( GTK_LABEL( gtk_bin_get_child( GTK_BIN( p->ratio_on_item ) ) ), buf2 );

    b = gtr_pref_flag_get( TR_PREFS_KEY_RATIO_ENABLED );
    gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM( b ? p->ratio_on_item : p->ratio_off_item ), TRUE );

    gtk_menu_popup ( GTK_MENU( p->options_menu ), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time( ) );
}

/***
****  PUBLIC
***/

GtkWidget *
gtr_window_new( GtkUIManager * ui_mgr, TrCore * core )
{
    int           i, n;
    const char  * pch;
    PrivateData * p;
    GtkWidget   * mainmenu, *toolbar, *filter, *list, *status;
    GtkWidget   * vbox, *w, *self, *h, *hbox, *menu;
    GtkWindow   * win;
    GSList      * l;

    p = g_new0( PrivateData, 1 );

    /* make the window */
    self = gtk_window_new ( GTK_WINDOW_TOPLEVEL );
    g_object_set_qdata_full( G_OBJECT(self), get_private_data_key( ), p, privateFree );
    win = GTK_WINDOW( self );
    gtk_window_set_title( win, g_get_application_name( ) );
    gtk_window_set_role( win, "tr-main" );
    gtk_window_set_default_size( win,
                                 gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_WIDTH ),
                                 gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_HEIGHT ) );
    gtk_window_move( win, gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_X ),
                          gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_Y ) );
    if( gtr_pref_flag_get( PREF_KEY_MAIN_WINDOW_IS_MAXIMIZED ) )
        gtk_window_maximize( win );
    gtk_window_add_accel_group( win, gtk_ui_manager_get_accel_group( ui_mgr ) );

    /* window's main container */
    vbox = gtk_vbox_new ( FALSE, 0 );
    gtk_container_add ( GTK_CONTAINER( self ), vbox );

    /* main menu */
    mainmenu = gtr_action_get_widget( "/main-window-menu" );
    w = gtr_action_get_widget( "/main-window-menu/torrent-menu/update-tracker" );
#if GTK_CHECK_VERSION( 2, 12, 0 )
    g_signal_connect( w, "query-tooltip",
                      G_CALLBACK( onAskTrackerQueryTooltip ), p );
#endif

    /* toolbar */
    toolbar = p->toolbar = gtr_action_get_widget( "/main-window-toolbar" );
    gtr_action_set_important( "open-torrent-toolbar", TRUE );
    gtr_action_set_important( "show-torrent-properties", TRUE );

    /* filter */
    h = filter = p->filter = gtr_filter_bar_new( gtr_core_session( core ),
                                                 gtr_core_model( core ),
                                                 &p->filter_model );
    gtk_container_set_border_width( GTK_CONTAINER( h ), GUI_PAD_SMALL );

    /* status menu */
    menu = p->status_menu = gtk_menu_new( );
    l = NULL;
    pch = gtr_pref_string_get( PREF_KEY_STATUSBAR_STATS );
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
    h = status = p->status = gtk_hbox_new( FALSE, GUI_PAD_BIG );
    gtk_container_set_border_width( GTK_CONTAINER( h ), GUI_PAD_SMALL );

        w = gtk_button_new( );
        gtk_container_add( GTK_CONTAINER( w ), gtk_image_new_from_stock( "utilities", -1 ) );
        gtr_widget_set_tooltip_text( w, _( "Options" ) );
        gtk_box_pack_start( GTK_BOX( h ), w, 0, 0, 0 );
        gtk_button_set_relief( GTK_BUTTON( w ), GTK_RELIEF_NONE );
        p->options_menu = createOptionsMenu( p );
        g_signal_connect( w, "clicked", G_CALLBACK(onOptionsClicked), p );

        p->alt_speed_image = gtk_image_new( );
        w = p->alt_speed_button = gtk_toggle_button_new( );
        gtk_button_set_image( GTK_BUTTON( w ), p->alt_speed_image );
        gtk_button_set_relief( GTK_BUTTON( w ), GTK_RELIEF_NONE );
        g_signal_connect( w, "toggled", G_CALLBACK(alt_speed_toggled_cb ), p );
        gtk_box_pack_start( GTK_BOX( h ), w, 0, 0, 0 );

        w = p->gutter_lb = gtk_label_new( "N Torrents" );
        gtk_label_set_single_line_mode( GTK_LABEL( w ), TRUE );
        gtk_box_pack_start( GTK_BOX( h ), w, 1, 1, GUI_PAD );

        hbox = gtk_hbox_new( FALSE, GUI_PAD );
            w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
            gtk_widget_set_size_request( w, GUI_PAD, 0u );
            gtk_box_pack_start( GTK_BOX( hbox ), w, FALSE, FALSE, 0 );
            w = p->ul_lb = gtk_label_new( NULL );
            gtk_label_set_single_line_mode( GTK_LABEL( w ), TRUE );
            gtk_box_pack_start( GTK_BOX( hbox ), w, FALSE, FALSE, 0 );
            w = gtk_image_new_from_stock( GTK_STOCK_GO_UP, GTK_ICON_SIZE_MENU );
            gtk_box_pack_start( GTK_BOX( hbox ), w, FALSE, FALSE, 0 );
        gtk_box_pack_end( GTK_BOX( h ), hbox, FALSE, FALSE, 0 );

        hbox = gtk_hbox_new( FALSE, GUI_PAD );
            w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
            gtk_widget_set_size_request( w, GUI_PAD, 0u );
            gtk_box_pack_start( GTK_BOX( hbox ), w, FALSE, FALSE, 0 );
            w = p->dl_lb = gtk_label_new( NULL );
            gtk_label_set_single_line_mode( GTK_LABEL( w ), TRUE );
            gtk_box_pack_start( GTK_BOX( hbox ), w, FALSE, FALSE, 0 );
            w = gtk_image_new_from_stock( GTK_STOCK_GO_DOWN, GTK_ICON_SIZE_MENU );
            gtk_box_pack_start( GTK_BOX( hbox ), w, FALSE, FALSE, 0 );
        gtk_box_pack_end( GTK_BOX( h ), hbox, FALSE, FALSE, 0 );

        hbox = gtk_hbox_new( FALSE, GUI_PAD );
            w = gtk_button_new( );
            gtr_widget_set_tooltip_text( w, _( "Statistics" ) );
            gtk_container_add( GTK_CONTAINER( w ), gtk_image_new_from_stock( "ratio", -1 ) );
            gtk_button_set_relief( GTK_BUTTON( w ), GTK_RELIEF_NONE );
            g_signal_connect( w, "clicked", G_CALLBACK( onYinYangReleased ), p );
            gtk_box_pack_start( GTK_BOX( hbox ), w, FALSE, FALSE, 0 );
            w = p->stats_lb = gtk_label_new( NULL );
            gtk_label_set_single_line_mode( GTK_LABEL( w ), TRUE );
            gtk_box_pack_end( GTK_BOX( hbox ), w, FALSE, FALSE, 0 );
        gtk_box_pack_end( GTK_BOX( h ), hbox, FALSE, FALSE, 0 );


    /* workarea */
    p->view = makeview( p );
    w = list = p->scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( w ),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW( w ),
                                         GTK_SHADOW_IN );
    gtk_container_add( GTK_CONTAINER( w ), p->view );

    /* lay out the widgets */
    gtk_box_pack_start( GTK_BOX( vbox ), mainmenu, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( vbox ), toolbar, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( vbox ), filter, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( vbox ), list, TRUE, TRUE, 0 );
    gtk_box_pack_start( GTK_BOX( vbox ), status, FALSE, FALSE, 0 );

    {
        /* this is to determine the maximum width/height for the label */
        int w=0, h=0;
        PangoLayout * pango_layout;
        pango_layout = gtk_widget_create_pango_layout( p->ul_lb, "999.99 KiB/s" );
        pango_layout_get_pixel_size( pango_layout, &w, &h );
        gtk_widget_set_size_request( p->ul_lb, w, h );
        gtk_widget_set_size_request( p->dl_lb, w, h );
        gtk_misc_set_alignment( GTK_MISC( p->ul_lb ), 1.0, 0.5 );
        gtk_misc_set_alignment( GTK_MISC( p->dl_lb ), 1.0, 0.5 );
        g_object_unref( G_OBJECT( pango_layout ) );
    }

    /* show all but the window */
    gtk_widget_show_all( vbox );

    /* listen for prefs changes that affect the window */
    p->core = core;
    prefsChanged( core, PREF_KEY_COMPACT_VIEW, self );
    prefsChanged( core, PREF_KEY_FILTERBAR, self );
    prefsChanged( core, PREF_KEY_STATUSBAR, self );
    prefsChanged( core, PREF_KEY_STATUSBAR_STATS, self );
    prefsChanged( core, PREF_KEY_TOOLBAR, self );
    prefsChanged( core, TR_PREFS_KEY_ALT_SPEED_ENABLED, self );
    p->pref_handler_id = g_signal_connect( core, "prefs-changed",
                                           G_CALLBACK( prefsChanged ), self );

    tr_sessionSetAltSpeedFunc( gtr_core_session( core ), onAltSpeedToggled, p );

    return self;
}

static void
updateTorrentCount( PrivateData * p )
{
    if( p && p->core )
    {
        char      buf[512];
        const int torrentCount = gtk_tree_model_iter_n_children( gtr_core_model( p->core ), NULL );
        const int visibleCount = gtk_tree_model_iter_n_children( p->filter_model, NULL );

        if( !torrentCount )
            *buf = '\0';
        else if( torrentCount != visibleCount )
            g_snprintf( buf, sizeof( buf ),
                        ngettext( "%1$'d of %2$'d Torrent",
                                  "%1$'d of %2$'d Torrents",
                                  torrentCount ),
                        visibleCount, torrentCount );
        else
            g_snprintf( buf, sizeof( buf ),
                        ngettext( "%'d Torrent", "%'d Torrents", torrentCount ),
                        torrentCount );
        gtr_label_set_text( GTK_LABEL( p->gutter_lb ), buf );
    }
}

static void
updateStats( PrivateData * p )
{
    const char *            pch;
    char                    up[32], down[32], ratio[32], buf[512];
    struct tr_session_stats stats;
    tr_session *            session = gtr_core_session( p->core );

    /* update the stats */
    pch = gtr_pref_string_get( PREF_KEY_STATUSBAR_STATS );
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
        /* Translators: "size|" is here for disambiguation. Please remove it from your translation.
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
        /* Translators: "size|" is here for disambiguation. Please remove it from your translation.
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
    gtr_label_set_text( GTK_LABEL( p->stats_lb ), buf );
}

static void
updateSpeeds( PrivateData * p )
{
    tr_session * session = gtr_core_session( p->core );

    if( session != NULL )
    {
        char buf[128];
        double up=0, down=0;
        GtkTreeIter iter;
        GtkTreeModel * model = gtr_core_model( p->core );

        if( gtk_tree_model_iter_nth_child( model, &iter, NULL, 0 ) ) do
        {
            double u, d;
            gtk_tree_model_get( model, &iter, MC_SPEED_UP, &u,
                                              MC_SPEED_DOWN, &d,
                                              -1 );
            up += u;
            down += d;
        }
        while( gtk_tree_model_iter_next( model, &iter ) );

        tr_formatter_speed_KBps( buf, down, sizeof( buf ) );
        gtr_label_set_text( GTK_LABEL( p->dl_lb ), buf );

        tr_formatter_speed_KBps( buf, up, sizeof( buf ) );
        gtr_label_set_text( GTK_LABEL( p->ul_lb ), buf );
    }
}

void
gtr_window_refresh( TrWindow * self )
{
    PrivateData * p = get_private_data( self );

    if( p && p->core && gtr_core_session( p->core ) )
    {
        updateSpeeds( p );
        updateTorrentCount( p );
        updateStats( p );
    }
}

GtkTreeSelection*
gtr_window_get_selection( TrWindow * w )
{
    return get_private_data( w )->selection;
}

void
gtr_window_set_busy( TrWindow * w, gboolean isBusy )
{
    if( w && gtr_widget_get_realized( GTK_WIDGET( w ) ) )
    {
        GdkDisplay * display = gtk_widget_get_display( GTK_WIDGET( w ) );
        GdkCursor * cursor = isBusy ? gdk_cursor_new_for_display( display, GDK_WATCH ) : NULL;

        gdk_window_set_cursor( gtr_widget_get_window( GTK_WIDGET( w ) ), cursor );
        gdk_display_flush( display );

        if( cursor ) {
#if GTK_CHECK_VERSION(3,0,0)
            g_object_unref( cursor );
#else
            gdk_cursor_unref( cursor );
#endif
        }
    }
}
