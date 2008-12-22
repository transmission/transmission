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

#include <ctype.h> /* isspace */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h> /* free() */
#include <unistd.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>
#include <libtransmission/web.h>
#include "blocklist.h"
#include "conf.h"
#include "hig.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
void
tr_prefs_init_global( void )
{
    int          i;
    char         pw[32];
    const char * str;
    const char * pool = "abcdefghijklmnopqrstuvwxyz"
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                        "1234567890";
    GRand *      rand;

    cf_check_older_configs( );

#if HAVE_GIO
    str = NULL;
    if( !str ) str = g_get_user_special_dir( G_USER_DIRECTORY_DESKTOP );
    if( !str ) str = g_get_home_dir( );
    pref_string_set_default ( PREF_KEY_DIR_WATCH, str );
    pref_flag_set_default   ( PREF_KEY_DIR_WATCH_ENABLED, FALSE );
#endif

    pref_int_set_default    ( PREF_KEY_PEER_SOCKET_TOS,
                              TR_DEFAULT_PEER_SOCKET_TOS );
    pref_flag_set_default   ( PREF_KEY_INHIBIT_HIBERNATION, FALSE );
    pref_flag_set_default   ( PREF_KEY_BLOCKLIST_ENABLED, TRUE );
    pref_flag_set_default   ( PREF_KEY_BLOCKLIST_UPDATES_ENABLED, TRUE );

    pref_string_set_default ( PREF_KEY_OPEN_DIALOG_FOLDER, g_get_home_dir( ) );

    pref_int_set_default    ( PREF_KEY_MAX_PEERS_GLOBAL,
                              TR_DEFAULT_GLOBAL_PEER_LIMIT );
    pref_int_set_default    ( PREF_KEY_MAX_PEERS_PER_TORRENT, 50 );

    pref_flag_set_default   ( PREF_KEY_TOOLBAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_FILTERBAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_STATUSBAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_SHOW_TRAY_ICON, FALSE );
    pref_string_set_default ( PREF_KEY_STATUSBAR_STATS, "total-ratio" );

    pref_flag_set_default   ( PREF_KEY_DL_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_DL_LIMIT, 100 );
    pref_flag_set_default   ( PREF_KEY_UL_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_UL_LIMIT, 50 );
    pref_flag_set_default   ( PREF_KEY_SCHED_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_SCHED_BEGIN,    60 * 23 ); /* 11pm */
    pref_int_set_default    ( PREF_KEY_SCHED_END,      60 * 7 );  /* 7am */
    pref_int_set_default    ( PREF_KEY_SCHED_DL_LIMIT, 200 );   /* 2x the other
                                                                  limit */
    pref_int_set_default    ( PREF_KEY_SCHED_UL_LIMIT, 100 );   /* 2x the other
                                                                  limit */

    pref_flag_set_default   ( PREF_KEY_OPTIONS_PROMPT, TRUE );

    pref_int_set_default    ( PREF_KEY_MAIN_WINDOW_HEIGHT, 500 );
    pref_int_set_default    ( PREF_KEY_MAIN_WINDOW_WIDTH, 300 );
    pref_int_set_default    ( PREF_KEY_MAIN_WINDOW_X, 50 );
    pref_int_set_default    ( PREF_KEY_MAIN_WINDOW_Y, 50 );

    pref_string_set_default ( PREF_KEY_PROXY_SERVER, "" );
    pref_int_set_default    ( PREF_KEY_PROXY_PORT, TR_DEFAULT_PROXY_PORT );
    pref_int_set_default    ( PREF_KEY_PROXY_TYPE, TR_DEFAULT_PROXY_TYPE );
    pref_flag_set_default   ( PREF_KEY_PROXY_SERVER_ENABLED,
                              TR_DEFAULT_PROXY_ENABLED );
    pref_flag_set_default   ( PREF_KEY_PROXY_AUTH_ENABLED,
                              TR_DEFAULT_PROXY_AUTH_ENABLED );
    pref_string_set_default ( PREF_KEY_PROXY_USERNAME, "" );
    pref_string_set_default ( PREF_KEY_PROXY_PASSWORD, "" );

    str = NULL;
#if GLIB_CHECK_VERSION( 2, 14, 0 )
    if( !str ) str = g_get_user_special_dir( G_USER_DIRECTORY_DOWNLOAD );
#endif
    if( !str ) str = g_get_home_dir( );
    pref_string_set_default ( PREF_KEY_DOWNLOAD_DIR, str );

    pref_int_set_default    ( PREF_KEY_PORT, TR_DEFAULT_PORT );

    pref_flag_set_default   ( PREF_KEY_PORT_FORWARDING, TRUE );
    pref_flag_set_default   ( PREF_KEY_PEX, TR_DEFAULT_PEX_ENABLED );
    pref_flag_set_default   ( PREF_KEY_ASKQUIT, TRUE );
    pref_flag_set_default   ( PREF_KEY_ENCRYPTION, TR_DEFAULT_ENCRYPTION );
    pref_flag_set_default   ( PREF_KEY_LAZY_BITFIELD,
                              TR_DEFAULT_LAZY_BITFIELD_ENABLED );

    pref_int_set_default    ( PREF_KEY_MSGLEVEL, TR_MSG_INF );

    pref_string_set_default ( PREF_KEY_SORT_MODE, "sort-by-name" );
    pref_flag_set_default   ( PREF_KEY_SORT_REVERSED, FALSE );
    pref_flag_set_default   ( PREF_KEY_MINIMAL_VIEW, FALSE );

    pref_flag_set_default   ( PREF_KEY_START, TRUE );
    pref_flag_set_default   ( PREF_KEY_TRASH_ORIGINAL, FALSE );

    pref_flag_set_default   ( PREF_KEY_RPC_ENABLED, TR_DEFAULT_RPC_ENABLED );
    pref_int_set_default    ( PREF_KEY_RPC_PORT, TR_DEFAULT_RPC_PORT );
    pref_string_set_default ( PREF_KEY_RPC_WHITELIST, TR_DEFAULT_RPC_WHITELIST );
    pref_flag_set_default   ( PREF_KEY_RPC_WHITELIST_ENABLED,
                              TR_DEFAULT_RPC_WHITELIST_ENABLED  );

    rand = g_rand_new ( );
    for( i = 0; i < 16; ++i )
        pw[i] = pool[g_rand_int_range ( rand, 0, strlen( pool ) )];
    g_rand_free ( rand );

    pw[16] = '\0';
    pref_string_set_default( PREF_KEY_RPC_USERNAME, "transmission" );
    pref_string_set_default( PREF_KEY_RPC_PASSWORD, pw );
    pref_flag_set_default  ( PREF_KEY_RPC_AUTH_ENABLED, FALSE );

    pref_save( );
}

/**
***
**/

#define PREF_KEY "pref-key"

static void
response_cb( GtkDialog *     dialog,
             int             response,
             gpointer unused UNUSED )
{
    if( response == GTK_RESPONSE_HELP )
    {
        char * base = gtr_get_help_url( );
        char * url = g_strdup_printf( "%s/html/preferences.html", base );
        gtr_open_file( url );
        g_free( url );
        g_free( base );
    }

    if( response == GTK_RESPONSE_CLOSE )
        gtk_widget_destroy( GTK_WIDGET( dialog ) );
}

static void
toggled_cb( GtkToggleButton * w,
            gpointer          core )
{
    const char *   key = g_object_get_data( G_OBJECT( w ), PREF_KEY );
    const gboolean flag = gtk_toggle_button_get_active( w );

    tr_core_set_pref_bool( TR_CORE( core ), key, flag );
}

static GtkWidget*
new_check_button( const char * mnemonic,
                  const char * key,
                  gpointer     core )
{
    GtkWidget * w = gtk_check_button_new_with_mnemonic( mnemonic );

    g_object_set_data_full( G_OBJECT( w ), PREF_KEY, g_strdup(
                                key ), g_free );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ),
                                 pref_flag_get( key ) );
    g_signal_connect( w, "toggled", G_CALLBACK( toggled_cb ), core );
    return w;
}

#define IDLE_DATA "idle-data"

struct spin_idle_data
{
    gpointer    core;
    GTimer *    last_change;
};

static void
spin_idle_data_free( gpointer gdata )
{
    struct spin_idle_data * data = gdata;

    g_timer_destroy( data->last_change );
    g_free( data );
}

static gboolean
spun_cb_idle( gpointer spin )
{
    gboolean                keep_waiting = TRUE;
    GObject *               o = G_OBJECT( spin );
    struct spin_idle_data * data = g_object_get_data( o, IDLE_DATA );

    /* has the user stopped making changes? */
    if( g_timer_elapsed( data->last_change, NULL ) > 0.33f )
    {
        /* update the core */
        const char * key = g_object_get_data( o, PREF_KEY );
        const int    value = gtk_spin_button_get_value_as_int(
             GTK_SPIN_BUTTON( spin ) );
        tr_core_set_pref_int( TR_CORE( data->core ), key, value );

        /* cleanup */
        g_object_set_data( o, IDLE_DATA, NULL );
        keep_waiting = FALSE;
        g_object_unref( G_OBJECT( o ) );
    }

    return keep_waiting;
}

static void
spun_cb( GtkSpinButton * w,
         gpointer        core )
{
    /* user may be spinning through many values, so let's hold off
       for a moment to keep from flooding the core with changes */
    GObject *               o = G_OBJECT( w );
    struct spin_idle_data * data = g_object_get_data( o, IDLE_DATA );

    if( data == NULL )
    {
        data = g_new( struct spin_idle_data, 1 );
        data->core = core;
        data->last_change = g_timer_new( );
        g_object_set_data_full( o, IDLE_DATA, data, spin_idle_data_free );
        g_object_ref( G_OBJECT( o ) );
        g_timeout_add( 100, spun_cb_idle, w );
    }
    g_timer_start( data->last_change );
}

static GtkWidget*
new_spin_button( const char * key,
                 gpointer     core,
                 int          low,
                 int          high,
                 int          step )
{
    GtkWidget * w = gtk_spin_button_new_with_range( low, high, step );

    g_object_set_data_full( G_OBJECT( w ), PREF_KEY, g_strdup(
                                key ), g_free );
    gtk_spin_button_set_digits( GTK_SPIN_BUTTON( w ), 0 );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON( w ), pref_int_get( key ) );
    g_signal_connect( w, "value-changed", G_CALLBACK( spun_cb ), core );
    return w;
}

static void
entry_changed_cb( GtkEntry * w,
                  gpointer   core )
{
    const char * key = g_object_get_data( G_OBJECT( w ), PREF_KEY );
    const char * value = gtk_entry_get_text( w );

    tr_core_set_pref( TR_CORE( core ), key, value );
}

static GtkWidget*
new_entry( const char * key,
           gpointer     core )
{
    GtkWidget *  w = gtk_entry_new( );
    const char * value = pref_string_get( key );

    if( value )
        gtk_entry_set_text( GTK_ENTRY( w ), value );
    g_object_set_data_full( G_OBJECT( w ), PREF_KEY, g_strdup(
                                key ), g_free );
    g_signal_connect( w, "changed", G_CALLBACK( entry_changed_cb ), core );
    return w;
}

static void
chosen_cb( GtkFileChooser * w,
           gpointer         core )
{
    const char * key = g_object_get_data( G_OBJECT( w ), PREF_KEY );
    char *       value = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( w ) );

    tr_core_set_pref( TR_CORE( core ), key, value );
    g_free( value );
}

static GtkWidget*
new_path_chooser_button( const char * key,
                         gpointer     core )
{
    GtkWidget *  w = gtk_file_chooser_button_new(
        NULL,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER );
    const char * path = pref_string_get( key );

    g_object_set_data_full( G_OBJECT( w ), PREF_KEY, g_strdup(
                                key ), g_free );
    g_signal_connect( w, "selection-changed", G_CALLBACK( chosen_cb ), core );
    gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER( w ), path );
    return w;
}

static void
target_cb( GtkWidget * tb,
           gpointer    target )
{
    const gboolean b = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( tb ) );

    gtk_widget_set_sensitive( GTK_WIDGET( target ), b );
}

/****
*****  Torrent Tab
****/

static GtkWidget*
torrentPage( GObject * core )
{
    int          row = 0;
    const char * s;
    GtkWidget *  t;
    GtkWidget *  w;

#ifdef HAVE_GIO
    GtkWidget *  l;
#endif

    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Adding Torrents" ) );

#ifdef HAVE_GIO
    s = _( "Automatically _add torrents from:" );
    l = new_check_button( s, PREF_KEY_DIR_WATCH_ENABLED, core );
    w = new_path_chooser_button( PREF_KEY_DIR_WATCH, core );
    gtk_widget_set_sensitive( GTK_WIDGET( w ),
                             pref_flag_get( PREF_KEY_DIR_WATCH_ENABLED ) );
    g_signal_connect( l, "toggled", G_CALLBACK( target_cb ), w );
    hig_workarea_add_row_w( t, &row, l, w, NULL );
#endif

    s = _( "Display _options dialog" );
    w = new_check_button( s, PREF_KEY_OPTIONS_PROMPT, core );
    hig_workarea_add_wide_control( t, &row, w );

    s = _( "_Start when added" );
    w = new_check_button( s, PREF_KEY_START, core );
    hig_workarea_add_wide_control( t, &row, w );

    s = _( "Mo_ve source files to Trash" );
    w = new_check_button( s, PREF_KEY_TRASH_ORIGINAL, core );
    hig_workarea_add_wide_control( t, &row, w );

    w = new_path_chooser_button( PREF_KEY_DOWNLOAD_DIR, core );
    hig_workarea_add_row( t, &row, _( "_Destination folder:" ), w, NULL );

    hig_workarea_finish( t, &row );
    return t;
}

/****
*****  Desktop Tab
****/

static GtkWidget*
desktopPage( GObject * core )
{
    int          row = 0;
    const char * s;
    GtkWidget *  t;
    GtkWidget *  w;

    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Options" ) );

    s = _( "Inhibit desktop _hibernation when torrents are active" );
    w = new_check_button( s, PREF_KEY_INHIBIT_HIBERNATION, core );
    hig_workarea_add_wide_control( t, &row, w );

    s = _( "Show _icon in the desktop Notification Area" );
    w = new_check_button( s, PREF_KEY_SHOW_TRAY_ICON, core );
    hig_workarea_add_wide_control( t, &row, w );

    hig_workarea_finish( t, &row );
    return t;
}

/****
*****  Peer Tab
****/

struct blocklist_data
{
    GtkWidget *  check;
    GtkWidget *  dialog;
    TrCore *     core;
    gulong       id;
    int          abortFlag;
    char         secondary[256];
};

static void
updateBlocklistText( GtkWidget * w,
                     TrCore *    core )
{
    const int n = tr_blocklistGetRuleCount( tr_core_session( core ) );
    char      buf[512];

    g_snprintf( buf, sizeof( buf ),
                ngettext( "Enable _blocklist (contains %'d rule)",
                          "Enable _blocklist (contains %'d rules)", n ), n );
    gtk_button_set_label( GTK_BUTTON( w ), buf );
}

static void
onBlocklistDialogResponse( GtkDialog *  d,
                           int response UNUSED,
                           gpointer     gdata )
{
    struct blocklist_data * data = gdata;

    g_signal_handler_disconnect( data->core, data->id );
    gtk_widget_destroy( GTK_WIDGET( d ) );
}

static void
onBlocklistStatus( TrCore * core UNUSED,
                   gboolean      isDone,
                   const char *  status,
                   gpointer      gdata )
{
    struct blocklist_data * data = gdata;

    gdk_threads_enter( );
    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( data-> dialog ),
                                              "%s", status );
    gtk_dialog_set_response_sensitive( GTK_DIALOG( data->dialog ),
                                       GTK_RESPONSE_CANCEL, !isDone );
    gtk_dialog_set_response_sensitive( GTK_DIALOG( data->dialog ),
                                       GTK_RESPONSE_CLOSE, isDone );
    if( isDone )
        updateBlocklistText( data->check, core );
    gdk_threads_leave( );
}

static void
onUpdateBlocklistCB( GtkButton * w,
                     gpointer    gdata )
{
    GtkWidget *             d;
    struct blocklist_data * data = gdata;

    d =
        gtk_message_dialog_new( GTK_WINDOW( gtk_widget_get_toplevel( 
                                                                    GTK_WIDGET(
                                                                        w ) ) ),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_INFO,
                               GTK_BUTTONS_NONE,
                               _( "Updating Blocklist" ) );

    data->dialog = d;
    data->id =
        g_signal_connect( data->core, "blocklist-status", G_CALLBACK(
                              onBlocklistStatus ), data );

    gtk_dialog_add_buttons( GTK_DIALOG( d ),
                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                            NULL );
    gtk_dialog_set_response_sensitive( GTK_DIALOG(
                                           d ), GTK_RESPONSE_CLOSE, FALSE );

    g_signal_connect( d, "response", G_CALLBACK(
                          onBlocklistDialogResponse ), data );
    gtk_widget_show( d );

    gtr_blocklist_update( data->core );
}

static void
onEncryptionToggled( GtkToggleButton * w,
                     gpointer          core )
{
    const int val = gtk_toggle_button_get_active( w )
                    ? TR_ENCRYPTION_REQUIRED
                    : TR_ENCRYPTION_PREFERRED;

    tr_core_set_pref_int( TR_CORE( core ), PREF_KEY_ENCRYPTION, val );
}

static GtkWidget*
peerPage( GObject * core )
{
    int                     row = 0;
    const char *            s;
    GtkWidget *             t;
    GtkWidget *             w;
    GtkWidget *             b;
    GtkWidget *             h;
    struct blocklist_data * data;

    data = g_new0( struct blocklist_data, 1 );
    data->core = TR_CORE( core );

    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Blocklist" ) );

    w = new_check_button( "", PREF_KEY_BLOCKLIST_ENABLED, core );
    updateBlocklistText( w, TR_CORE( core ) );
    h = gtk_hbox_new( FALSE, GUI_PAD_BIG );
    gtk_box_pack_start( GTK_BOX( h ), w, TRUE, TRUE, 0 );
    b = gtr_button_new_from_stock( GTK_STOCK_REFRESH, _( "_Update" ) );
    data->check = w;
    g_object_set_data( G_OBJECT( b ), "session",
                      tr_core_session( TR_CORE( core ) ) );
    g_signal_connect( b, "clicked", G_CALLBACK( onUpdateBlocklistCB ), data );
    gtk_box_pack_start( GTK_BOX( h ), b, FALSE, FALSE, 0 );
    g_signal_connect( w, "toggled", G_CALLBACK( target_cb ), b );
    target_cb( w, b );
    hig_workarea_add_wide_control( t, &row, h );

    s = _( "Enable _automatic updates" );
    w = new_check_button( s, PREF_KEY_BLOCKLIST_UPDATES_ENABLED, core );
    hig_workarea_add_wide_control( t, &row, w );
    g_signal_connect( data->check, "toggled", G_CALLBACK( target_cb ), w );
    target_cb( data->check, w );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Limits" ) );

    w = new_spin_button( PREF_KEY_MAX_PEERS_GLOBAL, core, 1, 3000, 5 );
    hig_workarea_add_row( t, &row, _( "Maximum peers _overall:" ), w, NULL );
    w = new_spin_button( PREF_KEY_MAX_PEERS_PER_TORRENT, core, 1, 300, 5 );
    hig_workarea_add_row( t, &row, _(
                              "Maximum peers per _torrent:" ), w, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title ( t, &row, _( "Options" ) );

    s = _( "_Ignore unencrypted peers" );
    w = gtk_check_button_new_with_mnemonic( s );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ),
                                  pref_int_get(
                                      PREF_KEY_ENCRYPTION ) ==
                                  TR_ENCRYPTION_REQUIRED );
    g_signal_connect( w, "toggled", G_CALLBACK( onEncryptionToggled ), core );
    hig_workarea_add_wide_control( t, &row, w );

    s = _( "Use peer e_xchange" );
    w = new_check_button( s, PREF_KEY_PEX, core );
    hig_workarea_add_wide_control( t, &row, w );

    hig_workarea_finish( t, &row );
    return t;
}

/****
*****  Web Tab
****/

enum
{
    COL_ADDRESS,
    N_COLS
};

static GtkTreeModel*
whitelist_tree_model_new( const char * whitelist )
{
    int            i;
    char **        rules;
    GtkListStore * store = gtk_list_store_new( N_COLS,
                                               G_TYPE_STRING,
                                               G_TYPE_STRING );

    rules = g_strsplit( whitelist, ",", 0 );

    for( i = 0; rules && rules[i]; ++i )
    {
        GtkTreeIter iter;
        const char * s = rules[i];
        while( isspace( *s ) ) ++s;
        gtk_list_store_append( store, &iter );
        gtk_list_store_set( store, &iter, COL_ADDRESS, s, -1 );
    }

    g_strfreev( rules );
    return GTK_TREE_MODEL( store );
}

struct remote_page
{
    TrCore *           core;
    GtkTreeView *      view;
    GtkListStore *     store;
    GtkWidget *        remove_button;
    GSList *           widgets;
    GSList *           auth_widgets;
    GSList *           whitelist_widgets;
    GtkToggleButton *  rpc_tb;
    GtkToggleButton *  auth_tb;
    GtkToggleButton *  whitelist_tb;
};

static void
refreshWhitelist( struct remote_page * page )
{
    GtkTreeIter    iter;
    GtkTreeModel * model = GTK_TREE_MODEL( page->store );
    GString *      gstr = g_string_new( NULL );

    if( gtk_tree_model_get_iter_first( model, &iter ) ) do
        {
            char * address;
            gtk_tree_model_get( model, &iter,
                                COL_ADDRESS, &address,
                                -1 );
            g_string_append( gstr, address );
            g_string_append( gstr, "," );
            g_free( address );
        }
        while( gtk_tree_model_iter_next( model, &iter ) );

    g_string_truncate( gstr, gstr->len - 1 ); /* remove the trailing comma */

    tr_core_set_pref( page->core, PREF_KEY_RPC_WHITELIST, gstr->str );

    g_string_free( gstr, TRUE );
}

static void
onAddressEdited( GtkCellRendererText  * r UNUSED,
                 gchar *                  path_string,
                 gchar *                  address,
                 gpointer                 gpage )
{
    GtkTreeIter          iter;
    struct remote_page * page = gpage;
    GtkTreeModel *       model = GTK_TREE_MODEL( page->store );
    GtkTreePath *        path = gtk_tree_path_new_from_string( path_string );

    if( gtk_tree_model_get_iter( model, &iter, path ) )
        gtk_list_store_set( page->store, &iter, COL_ADDRESS, address, -1 );

    gtk_tree_path_free( path );
    refreshWhitelist( page );
}

static void
onAddWhitelistClicked( GtkButton * b UNUSED,
                 gpointer      gpage )
{
    GtkTreeIter          iter;
    GtkTreePath *        path;
    struct remote_page * page = gpage;

    gtk_list_store_append( page->store, &iter );
    gtk_list_store_set( page->store, &iter,
                        COL_ADDRESS,  "0.0.0.0",
                        -1 );

    path = gtk_tree_model_get_path( GTK_TREE_MODEL( page->store ), &iter );
    gtk_tree_view_set_cursor(
        page->view, path,
        gtk_tree_view_get_column( page->view, COL_ADDRESS ),
        TRUE );
    gtk_tree_path_free( path );
}

static void
onRemoveWhitelistClicked( GtkButton * b UNUSED,
                    gpointer      gpage )
{
    struct remote_page * page = gpage;
    GtkTreeSelection *   sel = gtk_tree_view_get_selection( page->view );
    GtkTreeIter          iter;

    if( gtk_tree_selection_get_selected( sel, NULL, &iter ) )
    {
        gtk_list_store_remove( page->store, &iter );
        refreshWhitelist( page );
    }
}

static void
refreshRPCSensitivity( struct remote_page * page )
{
    GSList *           l;
    const int          rpc_active = gtk_toggle_button_get_active(
        page->rpc_tb );
    const int          auth_active = gtk_toggle_button_get_active(
        page->auth_tb );
    const int          whitelist_active = gtk_toggle_button_get_active(
        page->whitelist_tb );
    GtkTreeSelection * sel = gtk_tree_view_get_selection( page->view );
    const int          have_addr =
        gtk_tree_selection_get_selected( sel, NULL,
                                         NULL );
    const int          n_rules = gtk_tree_model_iter_n_children(
        GTK_TREE_MODEL( page->store ), NULL );

    for( l = page->widgets; l != NULL; l = l->next )
        gtk_widget_set_sensitive( GTK_WIDGET( l->data ), rpc_active );

    for( l = page->auth_widgets; l != NULL; l = l->next )
        gtk_widget_set_sensitive( GTK_WIDGET(
                                      l->data ), rpc_active && auth_active );

    for( l = page->whitelist_widgets; l != NULL; l = l->next )
        gtk_widget_set_sensitive( GTK_WIDGET( l->data ),
                                  rpc_active && whitelist_active );

    gtk_widget_set_sensitive( page->remove_button,
                              rpc_active && have_addr && n_rules > 1 );
}

static void
onRPCToggled( GtkToggleButton * tb UNUSED,
              gpointer             page )
{
    refreshRPCSensitivity( page );
}

static void
onWhitelistSelectionChanged( GtkTreeSelection * sel UNUSED,
                       gpointer               page )
{
    refreshRPCSensitivity( page );
}

static void
onLaunchClutchCB( GtkButton * w UNUSED,
                  gpointer data UNUSED )
{
    int    port = pref_int_get( PREF_KEY_RPC_PORT );
    char * url = g_strdup_printf( "http://localhost:%d/transmission/web",
                                  port );

    gtr_open_file( url );
    g_free( url );
}

static GtkWidget*
webPage( GObject * core )
{
    const char *         s;
    int                  row = 0;
    GtkWidget *          t;
    GtkWidget *          w;
    GtkWidget *          h;
    struct remote_page * page = g_new0( struct remote_page, 1 );

    page->core = TR_CORE( core );

    t = hig_workarea_create( );
    g_object_set_data_full( G_OBJECT( t ), "page", page, g_free );

    hig_workarea_add_section_title( t, &row, _( "Web Interface" ) );

    /* "enabled" checkbutton */
    s = _( "_Enable web interface" );
    w = new_check_button( s, PREF_KEY_RPC_ENABLED, core );
    page->rpc_tb = GTK_TOGGLE_BUTTON( w );
    g_signal_connect( w, "clicked", G_CALLBACK( onRPCToggled ), page );
    h = gtk_hbox_new( FALSE, GUI_PAD_BIG );
    gtk_box_pack_start( GTK_BOX( h ), w, TRUE, TRUE, 0 );
    w = gtk_button_new_from_stock( GTK_STOCK_OPEN );
    page->widgets = g_slist_append( page->widgets, w );
    g_signal_connect( w, "clicked", G_CALLBACK( onLaunchClutchCB ), NULL );
    gtk_box_pack_start( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    hig_workarea_add_wide_control( t, &row, h );

    /* port */
    w = new_spin_button( PREF_KEY_RPC_PORT, core, 0, 65535, 1 );
    page->widgets = g_slist_append( page->widgets, w );
    w = hig_workarea_add_row( t, &row, _( "Listening _port:" ), w, NULL );
    page->widgets = g_slist_append( page->widgets, w );

    /* require authentication */
    s = _( "_Require username" );
    w = new_check_button( s, PREF_KEY_RPC_AUTH_ENABLED, core );
    hig_workarea_add_wide_control( t, &row, w );
    page->auth_tb = GTK_TOGGLE_BUTTON( w );
    page->widgets = g_slist_append( page->widgets, w );
    g_signal_connect( w, "clicked", G_CALLBACK( onRPCToggled ), page );

    /* username */
    s = _( "_Username:" );
    w = new_entry( PREF_KEY_RPC_USERNAME, core );
    page->auth_widgets = g_slist_append( page->auth_widgets, w );
    w = hig_workarea_add_row( t, &row, s, w, NULL );
    page->auth_widgets = g_slist_append( page->auth_widgets, w );

    /* password */
    s = _( "Pass_word:" );
    w = new_entry( PREF_KEY_RPC_PASSWORD, core );
    gtk_entry_set_visibility( GTK_ENTRY( w ), FALSE );
    page->auth_widgets = g_slist_append( page->auth_widgets, w );
    w = hig_workarea_add_row( t, &row, s, w, NULL );
    page->auth_widgets = g_slist_append( page->auth_widgets, w );

    /* require authentication */
    s = _( "Only allow the following IP _addresses to connect:" );
    w = new_check_button( s, PREF_KEY_RPC_WHITELIST_ENABLED, core );
    hig_workarea_add_wide_control( t, &row, w );
    page->whitelist_tb = GTK_TOGGLE_BUTTON( w );
    page->widgets = g_slist_append( page->widgets, w );
    g_signal_connect( w, "clicked", G_CALLBACK( onRPCToggled ), page );

    /* access control list */
    {
        const char *        val = pref_string_get( PREF_KEY_RPC_WHITELIST );
        GtkTreeModel *      m = whitelist_tree_model_new( val );
        GtkTreeViewColumn * c;
        GtkCellRenderer *   r;
        GtkTreeSelection *  sel;
        GtkTreeView *       v;
        GtkWidget *         w;
        GtkWidget *         h;

        page->store = GTK_LIST_STORE( m );
        w = gtk_tree_view_new_with_model( m );
        g_signal_connect( w, "button-release-event",
                          G_CALLBACK( on_tree_view_button_released ), NULL );

        page->whitelist_widgets = g_slist_append( page->whitelist_widgets, w );
        v = page->view = GTK_TREE_VIEW( w );
#if GTK_CHECK_VERSION( 2,12,0 )
        gtk_widget_set_tooltip_text( w,
                  _( "IP addresses may use wildcards, such as 192.168.*.*" ) );
#endif
        sel = gtk_tree_view_get_selection( v );
        g_signal_connect( sel, "changed",
                          G_CALLBACK( onWhitelistSelectionChanged ), page );
        g_object_unref( G_OBJECT( m ) );
        gtk_tree_view_set_headers_visible( v, TRUE );
        w = gtk_frame_new( NULL );
        gtk_frame_set_shadow_type( GTK_FRAME( w ), GTK_SHADOW_IN );
        gtk_container_add( GTK_CONTAINER( w ), GTK_WIDGET( v ) );

        /* ip address column */
        r = gtk_cell_renderer_text_new( );
        g_signal_connect( r, "edited",
                          G_CALLBACK( onAddressEdited ), page );
        g_object_set( G_OBJECT( r ), "editable", TRUE, NULL );
        c = gtk_tree_view_column_new_with_attributes( NULL, r,
                                                      "text", COL_ADDRESS,
                                                      NULL );
        gtk_tree_view_column_set_expand( c, TRUE );
        gtk_tree_view_append_column( v, c );
        gtk_tree_view_set_headers_visible( v, FALSE );

        s = _( "Addresses:" );
        w = hig_workarea_add_row( t, &row, s, w, NULL );
        gtk_misc_set_alignment( GTK_MISC( w ), 0.0f, 0.0f );
        gtk_misc_set_padding( GTK_MISC( w ), 0, GUI_PAD );
        page->whitelist_widgets = g_slist_append( page->whitelist_widgets, w );

        h = gtk_hbox_new( TRUE, GUI_PAD );
        w = gtk_button_new_from_stock( GTK_STOCK_REMOVE );
        g_signal_connect( w, "clicked", G_CALLBACK(
                              onRemoveWhitelistClicked ), page );
        page->remove_button = w;
        onWhitelistSelectionChanged( sel, page );
        gtk_box_pack_start( GTK_BOX( h ), w, TRUE, TRUE, 0 );
        w = gtk_button_new_from_stock( GTK_STOCK_ADD );
        page->whitelist_widgets = g_slist_append( page->whitelist_widgets, w );
        g_signal_connect( w, "clicked", G_CALLBACK( onAddWhitelistClicked ), page );
        gtk_box_pack_start( GTK_BOX( h ), w, TRUE, TRUE, 0 );
        w = gtk_hbox_new( FALSE, 0 );
        gtk_box_pack_start( GTK_BOX( w ), gtk_alignment_new( 0, 0, 0, 0 ),
                            TRUE, TRUE, 0 );
        gtk_box_pack_start( GTK_BOX( w ), h, FALSE, FALSE, 0 );
        hig_workarea_add_wide_control( t, &row, w );
    }

    refreshRPCSensitivity( page );
    hig_workarea_finish( t, &row );
    return t;
}

/****
*****  Proxy Tab
****/

struct ProxyPage
{
    TrCore *  core;
    GSList *  proxy_widgets;
    GSList *  proxy_auth_widgets;
};

static void
refreshProxySensitivity( struct ProxyPage * p )
{
    GSList *       l;
    const gboolean proxy_enabled = pref_flag_get(
        PREF_KEY_PROXY_SERVER_ENABLED );
    const gboolean proxy_auth_enabled = pref_flag_get(
        PREF_KEY_PROXY_AUTH_ENABLED );

    for( l = p->proxy_widgets; l != NULL; l = l->next )
        gtk_widget_set_sensitive( GTK_WIDGET( l->data ), proxy_enabled );

    for( l = p->proxy_auth_widgets; l != NULL; l = l->next )
        gtk_widget_set_sensitive( GTK_WIDGET( l->data ),
                                  proxy_enabled && proxy_auth_enabled );
}

static void
onProxyToggled( GtkToggleButton * tb UNUSED,
                gpointer             user_data )
{
    refreshProxySensitivity( user_data );
}

static void
proxyPageFree( gpointer gpage )
{
    struct ProxyPage * page = gpage;

    g_slist_free( page->proxy_widgets );
    g_slist_free( page->proxy_auth_widgets );
    g_free( page );
}

static GtkTreeModel*
proxyTypeModelNew( void )
{
    GtkTreeIter    iter;
    GtkListStore * store = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );

    gtk_list_store_append( store, &iter );
    gtk_list_store_set( store, &iter, 0, "HTTP", 1, TR_PROXY_HTTP, -1 );
    gtk_list_store_append( store, &iter );
    gtk_list_store_set( store, &iter, 0, "SOCKS4", 1, TR_PROXY_SOCKS4, -1 );
    gtk_list_store_append( store, &iter );
    gtk_list_store_set( store, &iter, 0, "SOCKS5", 1, TR_PROXY_SOCKS5, -1 );
    return GTK_TREE_MODEL( store );
}

static void
onProxyTypeChanged( GtkComboBox * w,
                    gpointer      gpage )
{
    GtkTreeIter iter;

    if( gtk_combo_box_get_active_iter( w, &iter ) )
    {
        struct ProxyPage * page = gpage;
        int                type = TR_PROXY_HTTP;
        gtk_tree_model_get( gtk_combo_box_get_model(
                                w ), &iter, 1, &type, -1 );
        tr_core_set_pref_int( TR_CORE(
                                  page->core ), PREF_KEY_PROXY_TYPE, type );
    }
}

static GtkWidget*
trackerPage( GObject * core )
{
    int                row = 0;
    const char *       s;
    GtkWidget *        t;
    GtkWidget *        w;
    GtkTreeModel *     m;
    GtkCellRenderer *  r;
    struct ProxyPage * page = tr_new0( struct ProxyPage, 1 );

    page->core = TR_CORE( core );

    t = hig_workarea_create( );
    hig_workarea_add_section_title ( t, &row, _( "Tracker Proxy" ) );

    s = _( "Connect to tracker via a pro_xy" );
    w = new_check_button( s, PREF_KEY_PROXY_SERVER_ENABLED, core );
    g_signal_connect( w, "toggled", G_CALLBACK( onProxyToggled ), page );
    hig_workarea_add_wide_control( t, &row, w );

    s = _( "Proxy _server:" );
    w = new_entry( PREF_KEY_PROXY_SERVER, core );
    page->proxy_widgets = g_slist_append( page->proxy_widgets, w );
    w = hig_workarea_add_row( t, &row, s, w, NULL );
    page->proxy_widgets = g_slist_append( page->proxy_widgets, w );

    w = new_spin_button( PREF_KEY_PROXY_PORT, core, 0, 65535, 1 );
    page->proxy_widgets = g_slist_append( page->proxy_widgets, w );
    w = hig_workarea_add_row( t, &row, _( "Proxy _port:" ), w, NULL );
    page->proxy_widgets = g_slist_append( page->proxy_widgets, w );

    s = _( "Proxy _type:" );
    m = proxyTypeModelNew( );
    w = gtk_combo_box_new_with_model( m );
    r = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT( w ), r, TRUE );
    gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT(
                                        w ), r, "text", 0, NULL );
    gtk_combo_box_set_active( GTK_COMBO_BOX( w ),
                             pref_int_get( PREF_KEY_PROXY_TYPE ) );
    g_signal_connect( w, "changed", G_CALLBACK( onProxyTypeChanged ), page );
    g_object_unref( G_OBJECT( m ) );
    page->proxy_widgets = g_slist_append( page->proxy_widgets, w );
    w = hig_workarea_add_row( t, &row, s, w, NULL );
    page->proxy_widgets = g_slist_append( page->proxy_widgets, w );

    s = _( "_Authentication is required" );
    w = new_check_button( s, PREF_KEY_PROXY_AUTH_ENABLED, core );
    g_signal_connect( w, "toggled", G_CALLBACK( onProxyToggled ), page );
    hig_workarea_add_wide_control( t, &row, w );
    page->proxy_widgets = g_slist_append( page->proxy_widgets, w );

    s = _( "_Username:" );
    w = new_entry( PREF_KEY_PROXY_USERNAME, core );
    page->proxy_auth_widgets = g_slist_append( page->proxy_auth_widgets, w );
    w = hig_workarea_add_row( t, &row, s, w, NULL );
    page->proxy_auth_widgets = g_slist_append( page->proxy_auth_widgets, w );

    s = _( "Pass_word:" );
    w = new_entry( PREF_KEY_PROXY_PASSWORD, core );
    gtk_entry_set_visibility( GTK_ENTRY( w ), FALSE );
    page->proxy_auth_widgets = g_slist_append( page->proxy_auth_widgets, w );
    w = hig_workarea_add_row( t, &row, s, w, NULL );
    page->proxy_auth_widgets = g_slist_append( page->proxy_auth_widgets, w );

    hig_workarea_finish( t, &row );
    g_object_set_data_full( G_OBJECT( t ), "page", page, proxyPageFree );

    refreshProxySensitivity( page );
    return t;
}

/****
*****  Bandwidth Tab
****/

struct BandwidthPage
{
    TrCore *  core;
    GSList *  sched_widgets;
};

static void
refreshSchedSensitivity( struct BandwidthPage * p )
{
    GSList *       l;
    const gboolean sched_enabled = pref_flag_get(
        PREF_KEY_SCHED_LIMIT_ENABLED );

    for( l = p->sched_widgets; l != NULL; l = l->next )
        gtk_widget_set_sensitive( GTK_WIDGET( l->data ), sched_enabled );
}

static void
onSchedToggled( GtkToggleButton * tb UNUSED,
                gpointer             user_data )
{
    refreshSchedSensitivity( user_data );
}

static void
onTimeComboChanged( GtkComboBox * w,
                    gpointer      core )
{
    GtkTreeIter iter;

    if( gtk_combo_box_get_active_iter( w, &iter ) )
    {
        const char * key = g_object_get_data( G_OBJECT( w ), PREF_KEY );
        int          val = 0;
        gtk_tree_model_get( gtk_combo_box_get_model(
                                w ), &iter, 0, &val, -1 );
        tr_core_set_pref_int( TR_CORE( core ), key, val );
    }
}

static GtkWidget*
new_time_combo( GObject *    core,
                const char * key )
{
    int               val;
    int               i;
    GtkWidget *       w;
    GtkCellRenderer * r;
    GtkListStore *    store;

    /* build a store at 15 minute intervals */
    store = gtk_list_store_new( 2, G_TYPE_INT, G_TYPE_STRING );
    for( i = 0; i < 60 * 24; i += 15 )
    {
        char        buf[128];
        GtkTreeIter iter;
        struct tm   tm;
        tm.tm_hour = i / 60;
        tm.tm_min = i % 60;
        tm.tm_sec = 0;
        strftime( buf, sizeof( buf ), "%X", &tm );
        gtk_list_store_append( store, &iter );
        gtk_list_store_set( store, &iter, 0, i, 1, buf, -1 );
    }

    /* build the widget */
    w = gtk_combo_box_new_with_model( GTK_TREE_MODEL( store ) );
    gtk_combo_box_set_wrap_width( GTK_COMBO_BOX( w ), 4 );
    r = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT( w ), r, TRUE );
    gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT(
                                        w ), r, "text", 1, NULL );
    g_object_set_data_full( G_OBJECT( w ), PREF_KEY, tr_strdup(
                                key ), g_free );
    val = pref_int_get( key );
    gtk_combo_box_set_active( GTK_COMBO_BOX( w ), val / ( 15 ) );
    g_signal_connect( w, "changed", G_CALLBACK( onTimeComboChanged ), core );

    /* cleanup */
    g_object_unref( G_OBJECT( store ) );
    return w;
}

static void
bandwidthPageFree( gpointer gpage )
{
    struct BandwidthPage * page = gpage;

    g_slist_free( page->sched_widgets );
    g_free( page );
}

static GtkWidget*
bandwidthPage( GObject * core )
{
    int                    row = 0;
    const char *           s;
    GtkWidget *            t;
    GtkWidget *            w, * w2, * h, * l;
    struct BandwidthPage * page = tr_new0( struct BandwidthPage, 1 );

    page->core = TR_CORE( core );

    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Limits" ) );

    s = _( "Limit _download speed (KB/s):" );
    w = new_check_button( s, PREF_KEY_DL_LIMIT_ENABLED, core );
    w2 = new_spin_button( PREF_KEY_DL_LIMIT, core, 0, INT_MAX, 5 );
    gtk_widget_set_sensitive( GTK_WIDGET( w2 ),
                             pref_flag_get( PREF_KEY_DL_LIMIT_ENABLED ) );
    g_signal_connect( w, "toggled", G_CALLBACK( target_cb ), w2 );
    hig_workarea_add_row_w( t, &row, w, w2, NULL );

    s = _( "Limit _upload speed (KB/s):" );
    w = new_check_button( s, PREF_KEY_UL_LIMIT_ENABLED, core );
    w2 = new_spin_button( PREF_KEY_UL_LIMIT, core, 0, INT_MAX, 5 );
    gtk_widget_set_sensitive( GTK_WIDGET( w2 ),
                             pref_flag_get( PREF_KEY_UL_LIMIT_ENABLED ) );
    g_signal_connect( w, "toggled", G_CALLBACK( target_cb ), w2 );
    hig_workarea_add_row_w( t, &row, w, w2, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Scheduled Limits" ) );

    h = gtk_hbox_new( FALSE, 0 );
    w2 = new_time_combo( core, PREF_KEY_SCHED_BEGIN );
    page->sched_widgets = g_slist_append( page->sched_widgets, w2 );
    gtk_box_pack_start( GTK_BOX( h ), w2, FALSE, FALSE, 0 );
    w2 = gtk_label_new ( _( " and " ) );
    page->sched_widgets = g_slist_append( page->sched_widgets, w2 );
    gtk_box_pack_start( GTK_BOX( h ), w2, FALSE, FALSE, 0 );
    w2 = new_time_combo( core, PREF_KEY_SCHED_END );
    page->sched_widgets = g_slist_append( page->sched_widgets, w2 );
    gtk_box_pack_start( GTK_BOX( h ), w2, FALSE, FALSE, 0 );

    s = _( "_Limit bandwidth between" );
    w = new_check_button( s, PREF_KEY_SCHED_LIMIT_ENABLED, core );
    g_signal_connect( w, "toggled", G_CALLBACK( onSchedToggled ), page );
    hig_workarea_add_row_w( t, &row, w, h, NULL );

    w = new_spin_button( PREF_KEY_SCHED_DL_LIMIT, core, 0, INT_MAX, 5 );
    page->sched_widgets = g_slist_append( page->sched_widgets, w );
    l = hig_workarea_add_row( t, &row, _(
                                  "Limit d_ownload speed (KB/s):" ), w,
                              NULL );
    page->sched_widgets = g_slist_append( page->sched_widgets, l );

    w = new_spin_button( PREF_KEY_SCHED_UL_LIMIT, core, 0, INT_MAX, 5 );
    page->sched_widgets = g_slist_append( page->sched_widgets, w );
    l = hig_workarea_add_row( t, &row, _(
                                  "Limit u_pload speed (KB/s):" ), w, NULL );
    page->sched_widgets = g_slist_append( page->sched_widgets, l );

    hig_workarea_finish( t, &row );
    g_object_set_data_full( G_OBJECT( t ), "page", page, bandwidthPageFree );

    refreshSchedSensitivity( page );
    return t;
}

/****
*****  Network Tab
****/

struct test_port_data
{
    GtkWidget *  label;
    gboolean *   alive;
    char text[128];
};

/* this is invoked in the gtk main loop's thread */
static gboolean
testing_port_done_idle( gpointer gdata )
{
    struct test_port_data * data = gdata;

    if( *data->alive )
    {
        gdk_threads_enter( );
        gtk_label_set_markup( GTK_LABEL( data->label ), data->text );
        gdk_threads_leave( );
    }

    return FALSE;
}

/* this is invoked in the libtransmission thread */
static void
testing_port_done( tr_session * session        UNUSED,
                   long          response_code UNUSED,
                   const void *                response,
                   size_t                      response_len,
                   void *                      gdata )
{
    struct test_port_data * data = gdata;

    if( *data->alive )
    {
        const int isOpen = response_len && *(char*)response == '1';
        g_snprintf( data->text, sizeof( data->text ), isOpen 
                    ? _( "Port is <b>open</b>" )
                    : _( "Port is <b>closed</b>" ) );
        g_idle_add( testing_port_done_idle, data );
    }
}

static gboolean
testing_port_begin( gpointer gdata )
{
    struct test_port_data * data = gdata;

    if( *data->alive )
    {
        char            url[256];
        GObject       * o       = G_OBJECT( data->label );
        GtkSpinButton * spin    = g_object_get_data( o, "tr-port-spin" );
        tr_session    * session = g_object_get_data( o, "session" );
        const int       port    = gtk_spin_button_get_value_as_int( spin );
        g_snprintf( url, sizeof( url ),
                    "http://portcheck.transmissionbt.com/%d",
                    port );
        tr_webRun( session, url, NULL, testing_port_done, data );
    }
    return FALSE;
}

struct network_page_data
{
    gboolean *   alive;
    GtkWidget *  label;
    guint        id;
    TrCore *     core;
};

static void
onCorePrefsChanged( TrCore * core UNUSED,
                    const char *  key,
                    gpointer      gdata )
{
    if( !strcmp( key, PREF_KEY_PORT ) )
    {
        struct network_page_data * ndata = gdata;
        struct test_port_data *    data;

        gtk_label_set_markup( GTK_LABEL( ndata->label ),
                             _( "<i>Testing port...</i>" ) );

        /* wait three seconds to give the port forwarding time to kick in */
        data = g_new0( struct test_port_data, 1 );
        data->label = ndata->label;
        data->alive = ndata->alive;
        gtr_timeout_add_seconds( 3, testing_port_begin, data );
    }
}

static void
networkPageDestroyed( gpointer       gdata,
                      GObject * dead UNUSED )
{
    struct network_page_data * data = gdata;

    *data->alive = FALSE;
    g_signal_handler_disconnect( data->core, data->id );
    g_free( data );
}

static GtkWidget*
networkPage( GObject * core )
{
    int                        row = 0;
    const char *               s;
    GtkWidget *                t;
    GtkWidget *                w;
    GtkWidget *                w2;
    GtkWidget *                h;
    GtkWidget *                l;
    struct network_page_data * data;

    /* register to stop listening to core prefs changes when the page is
      destroyed */
    data = g_new0( struct network_page_data, 1 );
    data->core = TR_CORE( core );

    /* we leak this gboolean* s.t. we know it will still be alive when the port
       check is done, whether the dialog was destroyed or not.  kind of
       clumsy... */
    data->alive = g_new( gboolean, 1 );
    *data->alive = TRUE;

    /* build the page */
    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Incoming Peers" ) );

    h = gtk_hbox_new( FALSE, GUI_PAD_BIG );
    w2 = new_spin_button( PREF_KEY_PORT, core, 1, 65535, 1 );
    gtk_box_pack_start( GTK_BOX( h ), w2, FALSE, FALSE, 0 );
    data->label = l = gtk_label_new( NULL );
    gtk_misc_set_alignment( GTK_MISC( l ), 0.0f, 0.5f );
    gtk_box_pack_start( GTK_BOX( h ), l, FALSE, FALSE, 0 );
    hig_workarea_add_row( t, &row, _( "Listening _port:" ), h, w2 );

    g_object_set_data( G_OBJECT( l ), "tr-port-spin", w2 );
    g_object_set_data( G_OBJECT( l ), "session",
                      tr_core_session( TR_CORE( core ) ) );
    data->id = g_signal_connect( TR_CORE(
                                     core ), "prefs-changed",
                                 G_CALLBACK( onCorePrefsChanged ), data );
    onCorePrefsChanged( NULL, PREF_KEY_PORT, data );

    s = _( "Use UPnP or NAT-PMP port _forwarding from my router" );
    w = new_check_button( s, PREF_KEY_PORT_FORWARDING, core );
    hig_workarea_add_wide_control( t, &row, w );

    hig_workarea_finish( t, &row );
    g_object_weak_ref( G_OBJECT( t ), networkPageDestroyed, data );

    return t;
}

/****
*****
****/

GtkWidget *
tr_prefs_dialog_new( GObject *   core,
                     GtkWindow * parent )
{
    GtkWidget * d;
    GtkWidget * n;

    d = gtk_dialog_new_with_buttons( _(
                                         "Transmission Preferences" ),
                                     parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     NULL );
    gtk_window_set_role( GTK_WINDOW( d ), "transmission-preferences-dialog" );
    gtk_dialog_set_has_separator( GTK_DIALOG( d ), FALSE );
    gtk_container_set_border_width( GTK_CONTAINER( d ), GUI_PAD );

    n = gtk_notebook_new( );
    gtk_container_set_border_width ( GTK_CONTAINER ( n ), GUI_PAD );

    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              torrentPage( core ),
                              gtk_label_new ( _( "Torrents" ) ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              peerPage( core ),
                              gtk_label_new ( _( "Peers" ) ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              networkPage( core ),
                              gtk_label_new ( _( "Network" ) ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              desktopPage( core ),
                              gtk_label_new ( _( "Desktop" ) ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              bandwidthPage( core ),
                              gtk_label_new ( _( "Bandwidth" ) ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              webPage( core ),
                              gtk_label_new ( _( "Web" ) ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              trackerPage( core ),
                              gtk_label_new ( _( "Trackers" ) ) );

    g_signal_connect( d, "response", G_CALLBACK( response_cb ), core );
    gtk_box_pack_start( GTK_BOX( GTK_DIALOG( d )->vbox ), n, TRUE, TRUE, 0 );
    gtk_widget_show_all( GTK_DIALOG( d )->vbox );
    return d;
}

