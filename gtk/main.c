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

#include <locale.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h> /* exit() */
#include <sys/param.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>
#include <libtransmission/web.h>

#include "actions.h"
#include "conf.h"
#include "details.h"
#include "dialogs.h"
#include "hig.h"
#include "makemeta-ui.h"
#include "msgwin.h"
#include "open-dialog.h"
#include "relocate.h"
#include "stats.h"
#include "tr-core.h"
#include "tr-icon.h"
#include "tr-prefs.h"
#include "tr-window.h"
#include "util.h"
#include "ui.h"

#define MY_CONFIG_NAME "transmission"
#define MY_READABLE_NAME "transmission-gtk"

#define SHOW_LICENSE
static const char * LICENSE =
"The OS X client, CLI client, and parts of libtransmission are licensed under the terms of the MIT license.\n\n"
"The Transmission daemon, GTK+ client, Qt client, Web client, and most of libtransmission are licensed under the terms of the GNU GPL version 2, with two special exceptions:\n\n"
"1. The MIT-licensed portions of Transmission listed above are exempt from GPLv2 clause 2(b) and may retain their MIT license.\n\n"
"2. Permission is granted to link the code in this release with the OpenSSL project's 'OpenSSL' library and to distribute the linked executables. Works derived from Transmission may, at their authors' discretion, keep or delete this exception.";

struct cbdata
{
    char                      * config_dir;
    gboolean                    start_paused;
    gboolean                    is_iconified;

    guint                       timer;
    guint                       update_model_soon_tag;
    guint                       refresh_actions_tag;
    gpointer                    icon;
    GtkWindow                 * wind;
    TrCore                    * core;
    GtkWidget                 * msgwin;
    GtkWidget                 * prefs;
    GSList                    * error_list;
    GSList                    * duplicates_list;
    GSList                    * details;
    GtkTreeSelection          * sel;
    gpointer                    quit_dialog;
};

/***
****
****  DETAILS DIALOGS MANAGEMENT
****
***/

static void
gtr_window_present( GtkWindow * window )
{
    gtk_window_present_with_time( window, gtk_get_current_event_time( ) );
}

/***
****
****  DETAILS DIALOGS MANAGEMENT
****
***/

static int
compare_integers( const void * a, const void * b )
{
    return *(int*)a - *(int*)b;
}

static char*
get_details_dialog_key( GSList * id_list )
{
    int i;
    int n;
    int * ids;
    GSList * l;
    GString * gstr = g_string_new( NULL );

    n = g_slist_length( id_list );
    ids = g_new( int, n );
    i = 0;
    for( l=id_list; l!=NULL; l=l->next )
        ids[i++] = GPOINTER_TO_INT( l->data );
    g_assert( i == n );
    qsort( ids, n, sizeof(int), compare_integers );

    for( i=0; i<n; ++i )
        g_string_append_printf( gstr, "%d ", ids[i] );

    g_free( ids );
    return g_string_free( gstr, FALSE );
}

struct DetailsDialogHandle
{
    char * key;
    GtkWidget * dialog;
};

static GSList*
getSelectedTorrentIds( struct cbdata * data )
{
    GList * l;
    GtkTreeModel * model;
    GSList * ids = NULL;
    GList * paths = NULL;
    GtkTreeSelection * s = data->sel;

    /* build a list of the selected torrents' ids */
    for( paths=l=gtk_tree_selection_get_selected_rows(s,&model); l; l=l->next ) {
        GtkTreeIter iter;
        if( gtk_tree_model_get_iter( model, &iter, l->data ) ) {
            int id;
            gtk_tree_model_get( model, &iter, MC_TORRENT_ID, &id, -1 );
            ids = g_slist_append( ids, GINT_TO_POINTER( id ) );
        }
    }

    /* cleanup */
    g_list_foreach( paths, (GFunc)gtk_tree_path_free, NULL );
    g_list_free( paths );
    return ids;
}

static struct DetailsDialogHandle*
find_details_dialog_from_ids( struct cbdata * cbdata, GSList * ids )
{
    GSList * l;
    struct DetailsDialogHandle * ret = NULL;
    char * key = get_details_dialog_key( ids );

    for( l=cbdata->details; l!=NULL && ret==NULL; l=l->next ) {
        struct DetailsDialogHandle * h = l->data;
        if( !strcmp( h->key, key ) )
            ret = h;
    }

    g_free( key );
    return ret;
}

static struct DetailsDialogHandle*
find_details_dialog_from_widget( struct cbdata * cbdata, gpointer w )
{
    GSList * l;
    struct DetailsDialogHandle * ret = NULL;

    for( l=cbdata->details; l!=NULL && ret==NULL; l=l->next ) {
        struct DetailsDialogHandle * h = l->data;
        if( h->dialog == w )
            ret = h;
    }

    return ret;
}

static void
on_details_dialog_closed( gpointer gdata, GObject * dead )
{
    struct cbdata * data = gdata;
    struct DetailsDialogHandle * h = find_details_dialog_from_widget( data, dead );

    if( h != NULL )
    {
        data->details = g_slist_remove( data->details, h );
        g_free( h->key );
        g_free( h );
    }
}

static void
show_details_dialog_for_selected_torrents( struct cbdata * data )
{
    GtkWidget * w;
    GSList * ids = getSelectedTorrentIds( data );
    struct DetailsDialogHandle * h = find_details_dialog_from_ids( data, ids );

    if( h != NULL )
        w = h->dialog;
    else {
        h = g_new( struct DetailsDialogHandle, 1 );
        h->key = get_details_dialog_key( ids );
        h->dialog = w = gtr_torrent_details_dialog_new( data->wind, data->core );
        gtr_torrent_details_dialog_set_torrents( w, ids );
        data->details = g_slist_append( data->details, h );
        g_object_weak_ref( G_OBJECT( w ), on_details_dialog_closed, data );
        gtk_widget_show( w );
    }
    gtr_window_present( GTK_WINDOW( w ) );
    g_slist_free( ids );
}

/****
*****
*****  ON SELECTION CHANGED
*****
****/

struct counts_data
{
    int total_count;
    int queued_count;
    int stopped_count;
};

static void
get_selected_torrent_counts_foreach( GtkTreeModel * model, GtkTreePath * path UNUSED,
                                     GtkTreeIter * iter, gpointer user_data )
{
    int activity = 0;
    struct counts_data * counts = user_data;

    ++counts->total_count;

    gtk_tree_model_get( model, iter, MC_ACTIVITY, &activity, -1 );

    if( ( activity == TR_STATUS_DOWNLOAD_WAIT ) || ( activity == TR_STATUS_SEED_WAIT ) )
        ++counts->queued_count;

    if( activity == TR_STATUS_STOPPED )
        ++counts->stopped_count;
}

static void
get_selected_torrent_counts( struct cbdata * data, struct counts_data * counts )
{
    counts->total_count = 0;
    counts->queued_count = 0;
    counts->stopped_count = 0;

    gtk_tree_selection_selected_foreach( data->sel, get_selected_torrent_counts_foreach, counts );
}

static void
count_updatable_foreach( GtkTreeModel * model, GtkTreePath * path UNUSED,
                         GtkTreeIter * iter, gpointer accumulated_status )
{
    tr_torrent * tor;
    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    *(int*)accumulated_status |= tr_torrentCanManualUpdate( tor );
}

static gboolean
refresh_actions( gpointer gdata )
{
    int canUpdate;
    struct counts_data sel_counts;
    struct cbdata * data = gdata;
    const size_t total = gtr_core_get_torrent_count( data->core );
    const size_t active = gtr_core_get_active_torrent_count( data->core );
    const int torrent_count = gtk_tree_model_iter_n_children( gtr_core_model( data->core ), NULL );
    bool has_selection;

    get_selected_torrent_counts( data, &sel_counts );
    has_selection = sel_counts.total_count > 0;

    gtr_action_set_sensitive( "select-all", torrent_count != 0 );
    gtr_action_set_sensitive( "deselect-all", torrent_count != 0 );
    gtr_action_set_sensitive( "pause-all-torrents", active != 0 );
    gtr_action_set_sensitive( "start-all-torrents", active != total );

    gtr_action_set_sensitive( "torrent-stop", ( sel_counts.stopped_count < sel_counts.total_count ) );
    gtr_action_set_sensitive( "torrent-start", ( sel_counts.stopped_count ) > 0 );
    gtr_action_set_sensitive( "torrent-start-now", ( sel_counts.stopped_count + sel_counts.queued_count ) > 0 );
    gtr_action_set_sensitive( "torrent-verify",          has_selection );
    gtr_action_set_sensitive( "remove-torrent",          has_selection );
    gtr_action_set_sensitive( "delete-torrent",          has_selection );
    gtr_action_set_sensitive( "relocate-torrent",        has_selection );
    gtr_action_set_sensitive( "queue-move-top",          has_selection );
    gtr_action_set_sensitive( "queue-move-up",           has_selection );
    gtr_action_set_sensitive( "queue-move-down",         has_selection );
    gtr_action_set_sensitive( "queue-move-bottom",       has_selection );
    gtr_action_set_sensitive( "show-torrent-properties", has_selection );
    gtr_action_set_sensitive( "open-torrent-folder", sel_counts.total_count == 1 );
    gtr_action_set_sensitive( "copy-magnet-link-to-clipboard", sel_counts.total_count == 1 );

    canUpdate = 0;
    gtk_tree_selection_selected_foreach( data->sel, count_updatable_foreach, &canUpdate );
    gtr_action_set_sensitive( "torrent-reannounce", canUpdate != 0 );

    data->refresh_actions_tag = 0;
    return FALSE;
}

static void
refresh_actions_soon( gpointer gdata )
{
    struct cbdata * data = gdata;

    if( data->refresh_actions_tag == 0 )
        data->refresh_actions_tag = gdk_threads_add_idle( refresh_actions, data );
}

static void
on_selection_changed( GtkTreeSelection * s UNUSED, gpointer gdata )
{
    refresh_actions_soon( gdata );
}

/***
****
****
***/

static void app_setup( TrWindow * wind, struct cbdata  * cbdata );

static void main_window_setup( struct cbdata * cbdata, TrWindow * wind );

static void on_app_exit( gpointer vdata );

static void on_core_error( TrCore *, guint, const char *, struct cbdata * );

static void on_add_torrent( TrCore *, tr_ctor *, gpointer );

static void on_prefs_changed( TrCore * core, const char * key, gpointer );

static gboolean update_model_loop( gpointer gdata );
static gboolean update_model_once( gpointer gdata );

/***
****
***/

static void
register_magnet_link_handler( void )
{
    GAppInfo * app_info = g_app_info_get_default_for_uri_scheme( "magnet" );
    if( app_info == NULL )
    {
        /* there's no default magnet handler, so register ourselves for the job... */
        GError * error = NULL;
        app_info = g_app_info_create_from_commandline( "transmission-gtk", "transmission-gtk", G_APP_INFO_CREATE_SUPPORTS_URIS, NULL );
        g_app_info_set_as_default_for_type( app_info, "x-scheme-handler/magnet", &error );
        if( error != NULL )
        {
            g_warning( _( "Error registering Transmission as x-scheme-handler/magnet handler: %s" ), error->message );
            g_clear_error( &error );
        }
    }
}

static void
on_main_window_size_allocated( GtkWidget      * gtk_window,
                               GtkAllocation  * alloc UNUSED,
                               gpointer         gdata UNUSED )
{
    GdkWindow * gdk_window = gtk_widget_get_window( gtk_window );
    const gboolean isMaximized = ( gdk_window != NULL )
                              && ( gdk_window_get_state( gdk_window ) & GDK_WINDOW_STATE_MAXIMIZED );

    gtr_pref_int_set( PREF_KEY_MAIN_WINDOW_IS_MAXIMIZED, isMaximized );

    if( !isMaximized )
    {
        int x, y, w, h;
        gtk_window_get_position( GTK_WINDOW( gtk_window ), &x, &y );
        gtk_window_get_size( GTK_WINDOW( gtk_window ), &w, &h );
        gtr_pref_int_set( PREF_KEY_MAIN_WINDOW_X, x );
        gtr_pref_int_set( PREF_KEY_MAIN_WINDOW_Y, y );
        gtr_pref_int_set( PREF_KEY_MAIN_WINDOW_WIDTH, w );
        gtr_pref_int_set( PREF_KEY_MAIN_WINDOW_HEIGHT, h );
    }
}

/***
**** listen to changes that come from RPC
***/

struct torrent_idle_data
{
    TrCore * core;
    int id;
    gboolean delete_files;
};

static gboolean
rpc_torrent_remove_idle( gpointer gdata )
{
    struct torrent_idle_data * data = gdata;

    gtr_core_remove_torrent( data->core, data->id, data->delete_files );

    g_free( data );
    return FALSE; /* tell g_idle not to call this func twice */
}

static gboolean
rpc_torrent_add_idle( gpointer gdata )
{
    tr_torrent * tor;
    struct torrent_idle_data * data = gdata;

    if(( tor = gtr_core_find_torrent( data->core, data->id )))
        gtr_core_add_torrent( data->core, tor, TRUE );

    g_free( data );
    return FALSE; /* tell g_idle not to call this func twice */
}

static tr_rpc_callback_status
on_rpc_changed( tr_session            * session,
                tr_rpc_callback_type    type,
                struct tr_torrent     * tor,
                void                  * gdata )
{
    tr_rpc_callback_status status = TR_RPC_OK;
    struct cbdata * cbdata = gdata;
    gdk_threads_enter( );

    switch( type )
    {
        case TR_RPC_SESSION_CLOSE:
            gtr_action_activate( "quit" );
            break;

        case TR_RPC_TORRENT_ADDED: {
            struct torrent_idle_data * data = g_new0( struct torrent_idle_data, 1 );
            data->id = tr_torrentId( tor );
            data->core = cbdata->core;
            gdk_threads_add_idle( rpc_torrent_add_idle, data );
            break;
        }

        case TR_RPC_TORRENT_REMOVING:
        case TR_RPC_TORRENT_TRASHING: {
            struct torrent_idle_data * data = g_new0( struct torrent_idle_data, 1 );
            data->id = tr_torrentId( tor );
            data->core = cbdata->core;
            data->delete_files = type == TR_RPC_TORRENT_TRASHING;
            gdk_threads_add_idle( rpc_torrent_remove_idle, data );
            status = TR_RPC_NOREMOVE;
            break;
        }

        case TR_RPC_SESSION_CHANGED: {
            int i;
            tr_benc tmp;
            tr_benc * newval;
            tr_benc * oldvals = gtr_pref_get_all( );
            const char * key;
            GSList * l;
            GSList * changed_keys = NULL;
            tr_bencInitDict( &tmp, 100 );
            tr_sessionGetSettings( session, &tmp );
            for( i=0; tr_bencDictChild( &tmp, i, &key, &newval ); ++i )
            {
                bool changed;
                tr_benc * oldval = tr_bencDictFind( oldvals, key );
                if( !oldval )
                    changed = true;
                else {
                    char * a = tr_bencToStr( oldval, TR_FMT_BENC, NULL );
                    char * b = tr_bencToStr( newval, TR_FMT_BENC, NULL );
                    changed = strcmp( a, b ) != 0;
                    tr_free( b );
                    tr_free( a );
                }

                if( changed )
                    changed_keys = g_slist_append( changed_keys, (gpointer)key );
            }
            tr_sessionGetSettings( session, oldvals );

            for( l=changed_keys; l!=NULL; l=l->next )
                gtr_core_pref_changed( cbdata->core, l->data );

            g_slist_free( changed_keys );
            tr_bencFree( &tmp );
            break;
        }

        case TR_RPC_TORRENT_CHANGED:
        case TR_RPC_TORRENT_MOVED:
        case TR_RPC_TORRENT_STARTED:
        case TR_RPC_TORRENT_STOPPED:
            /* nothing interesting to do here */
            break;
    }

    gdk_threads_leave( );
    return status;
}

/***
****  signal handling
***/

static sig_atomic_t global_sigcount = 0;
static struct cbdata * sighandler_cbdata = NULL;

static void
signal_handler( int sig )
{
    if( ++global_sigcount > 1 )
    {
        signal( sig, SIG_DFL );
        raise( sig );
    }
    else switch( sig )
    {
        case SIGINT:
        case SIGTERM:
            g_message( _( "Got signal %d; trying to shut down cleanly. Do it again if it gets stuck." ), sig );
            gtr_actions_handler( "quit", sighandler_cbdata );
            break;

        default:
            g_message( "unhandled signal" );
            break;
    }
}

/****
*****
*****
****/

static void
on_startup( GApplication * application, gpointer user_data )
{
    const char * str;
    GtkWindow * win;
    GtkUIManager * ui_manager;
    tr_session * session;
    struct cbdata * cbdata = user_data;

    signal( SIGINT, signal_handler );
    signal( SIGKILL, signal_handler );

    sighandler_cbdata = cbdata;

    /* ensure the directories are created */
    if(( str = gtr_pref_string_get( TR_PREFS_KEY_DOWNLOAD_DIR )))
	g_mkdir_with_parents( str, 0777 );
    if(( str = gtr_pref_string_get( TR_PREFS_KEY_INCOMPLETE_DIR )))
	g_mkdir_with_parents( str, 0777 );

    /* initialize the libtransmission session */
    session = tr_sessionInit( "gtk", cbdata->config_dir, TRUE, gtr_pref_get_all( ) );

    gtr_pref_flag_set( TR_PREFS_KEY_ALT_SPEED_ENABLED, tr_sessionUsesAltSpeed( session ) );
    gtr_pref_int_set( TR_PREFS_KEY_PEER_PORT, tr_sessionGetPeerPort( session ) );
    cbdata->core = gtr_core_new( session );

    /* init the ui manager */
    ui_manager = gtk_ui_manager_new ( );
    gtr_actions_init ( ui_manager, cbdata );
    gtk_ui_manager_add_ui_from_string ( ui_manager, fallback_ui_file, -1, NULL );
    gtk_ui_manager_ensure_update ( ui_manager );

    /* create main window now to be a parent to any error dialogs */
    win = GTK_WINDOW( gtr_window_new( ui_manager, cbdata->core ) );
    g_signal_connect( win, "size-allocate", G_CALLBACK( on_main_window_size_allocated ), cbdata );
    g_application_hold( application );
    g_object_weak_ref( G_OBJECT( win ), (GWeakNotify)g_application_release, application );
    app_setup( win, cbdata );
    tr_sessionSetRPCCallback( session, on_rpc_changed, cbdata );

    /* check & see if it's time to update the blocklist */
    if( gtr_pref_flag_get( TR_PREFS_KEY_BLOCKLIST_ENABLED ) ) {
	if( gtr_pref_flag_get( PREF_KEY_BLOCKLIST_UPDATES_ENABLED ) ) {
	    const int64_t last_time = gtr_pref_int_get( "blocklist-date" );
	    const int SECONDS_IN_A_WEEK = 7 * 24 * 60 * 60;
	    const time_t now = time( NULL );
	if( last_time + SECONDS_IN_A_WEEK < now )
	    gtr_core_blocklist_update( cbdata->core );
        }
    }

    /* if there's no magnet link handler registered, register us */
    register_magnet_link_handler( );
}

static void
on_activate( GApplication * app UNUSED, gpointer unused UNUSED )
{
    gtr_action_activate( "present-main-window" );
}

static void
open_files( GSList * files, gpointer gdata )
{
    struct cbdata * cbdata = gdata;
    const gboolean do_start = gtr_pref_flag_get( TR_PREFS_KEY_START ) && !cbdata->start_paused;
    const gboolean do_prompt = gtr_pref_flag_get( PREF_KEY_OPTIONS_PROMPT );
    const gboolean do_notify = TRUE;

    gtr_core_add_files( cbdata->core, files, do_start, do_prompt, do_notify );
}

static void
on_open (GApplication  * application UNUSED,
         GFile        ** f,
         gint            file_count,
         gchar         * hint UNUSED,
         gpointer        gdata )
{
    int i;
    GSList * files = NULL;

    for( i=0; i<file_count; ++i )
        files = g_slist_append( files, f[i] );

    open_files( files, gdata );

    g_slist_free( files );
}

/***
****
***/

int
main( int argc, char ** argv )
{
    int ret;
    struct stat sb;
    char * application_id;
    GApplication * app;
    GOptionContext * option_context;
    bool show_version = false;
    GError * error = NULL;
    struct cbdata cbdata;

    GOptionEntry option_entries[] = {
        { "config-dir", 'g', 0, G_OPTION_ARG_FILENAME, &cbdata.config_dir, _( "Where to look for configuration files" ), NULL },
        { "paused",     'p', 0, G_OPTION_ARG_NONE, &cbdata.start_paused, _( "Start with all torrents paused" ), NULL },
        { "minimized",  'm', 0, G_OPTION_ARG_NONE, &cbdata.is_iconified, _( "Start minimized in notification area" ), NULL },
        { "version",    'v', 0, G_OPTION_ARG_NONE, &show_version, _( "Show version number and exit" ), NULL },
        { NULL, 0,   0, 0, NULL, NULL, NULL }
    };

    /* default settings */
    memset( &cbdata, 0, sizeof( struct cbdata ) );
    cbdata.config_dir = (char*) tr_getDefaultConfigDir( MY_CONFIG_NAME );

    /* init i18n */
    setlocale( LC_ALL, "" );
    bindtextdomain( MY_READABLE_NAME, TRANSMISSIONLOCALEDIR );
    bind_textdomain_codeset( MY_READABLE_NAME, "UTF-8" );
    textdomain( MY_READABLE_NAME );

    /* init glib/gtk */
    g_thread_init (NULL);
    g_type_init ();
    gtk_init (&argc, &argv);
    g_set_application_name (_( "Transmission" ));
    gtk_window_set_default_icon_name (MY_CONFIG_NAME);


    /* parse the command line */
    option_context = g_option_context_new( _( "[torrent files or urls]" ) );
    g_option_context_add_main_entries( option_context, option_entries, GETTEXT_PACKAGE );
    g_option_context_set_translation_domain( option_context, GETTEXT_PACKAGE );
    if( !g_option_context_parse( option_context, &argc, &argv, &error ) ) {
        g_print (_("%s\nRun '%s --help' to see a full list of available command line options.\n"), error->message, argv[0]);
        g_error_free (error);
        g_option_context_free (option_context);
        return 1;
    }
    g_option_context_free (option_context);

    /* handle the trivial "version" option */
    if( show_version ) {
        fprintf( stderr, "%s %s\n", MY_READABLE_NAME, LONG_VERSION_STRING );
        return 0;
    }

    /* init the unit formatters */
    tr_formatter_mem_init( mem_K, _(mem_K_str), _(mem_M_str), _(mem_G_str), _(mem_T_str) );
    tr_formatter_size_init( disk_K, _(disk_K_str), _(disk_M_str), _(disk_G_str), _(disk_T_str) );
    tr_formatter_speed_init( speed_K, _(speed_K_str), _(speed_M_str), _(speed_G_str), _(speed_T_str) );

    /* set up the config dir */
    gtr_pref_init( cbdata.config_dir );
    g_mkdir_with_parents( cbdata.config_dir, 0755 );

    /* init the application for the specified config dir */
    stat( cbdata.config_dir, &sb );
    application_id = g_strdup_printf( "com.transmissionbt.transmission_%lu_%lu", (unsigned long)sb.st_dev, (unsigned long)sb.st_ino );
    app = g_application_new( application_id, G_APPLICATION_HANDLES_OPEN );
    g_signal_connect( app, "open", G_CALLBACK(on_open), &cbdata );
    g_signal_connect( app, "startup", G_CALLBACK(on_startup), &cbdata );
    g_signal_connect( app, "activate", G_CALLBACK(on_activate), &cbdata );
    ret = g_application_run (app, argc, argv);
    g_object_unref( app );
    return ret;
}

static void
on_core_busy( TrCore * core UNUSED, gboolean busy, struct cbdata * c )
{
    gtr_window_set_busy( c->wind, busy );
}

static void
app_setup( TrWindow * wind, struct cbdata * cbdata )
{
    if( cbdata->is_iconified )
        gtr_pref_flag_set( PREF_KEY_SHOW_TRAY_ICON, TRUE );

    gtr_actions_set_core( cbdata->core );

    /* set up core handlers */
    g_signal_connect( cbdata->core, "busy", G_CALLBACK( on_core_busy ), cbdata );
    g_signal_connect( cbdata->core, "add-error", G_CALLBACK( on_core_error ), cbdata );
    g_signal_connect( cbdata->core, "add-prompt", G_CALLBACK( on_add_torrent ), cbdata );
    g_signal_connect( cbdata->core, "prefs-changed", G_CALLBACK( on_prefs_changed ), cbdata );

    /* add torrents from command-line and saved state */
    gtr_core_load( cbdata->core, cbdata->start_paused );
    gtr_core_torrents_added( cbdata->core );

    /* set up main window */
    main_window_setup( cbdata, wind );

    /* set up the icon */
    on_prefs_changed( cbdata->core, PREF_KEY_SHOW_TRAY_ICON, cbdata );

    /* start model update timer */
    cbdata->timer = gdk_threads_add_timeout_seconds( MAIN_WINDOW_REFRESH_INTERVAL_SECONDS, update_model_loop, cbdata );
    update_model_once( cbdata );

    /* either show the window or iconify it */
    if( !cbdata->is_iconified )
        gtk_widget_show( GTK_WIDGET( wind ) );
    else
    {
        gtk_window_set_skip_taskbar_hint( cbdata->wind,
                                          cbdata->icon != NULL );
        cbdata->is_iconified = FALSE; // ensure that the next toggle iconifies
        gtr_action_set_toggled( "toggle-main-window", FALSE );
    }

    if( !gtr_pref_flag_get( PREF_KEY_USER_HAS_GIVEN_INFORMED_CONSENT ) )
    {
        GtkWidget * w = gtk_message_dialog_new( GTK_WINDOW( wind ),
                                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_INFO,
                                                GTK_BUTTONS_NONE,
                                                "%s",
             _( "Transmission is a file-sharing program. When you run a torrent, its data will be made available to others by means of upload. You and you alone are fully responsible for exercising proper judgement and abiding by your local laws." ) );
        gtk_dialog_add_button( GTK_DIALOG( w ), GTK_STOCK_QUIT, GTK_RESPONSE_REJECT );
        gtk_dialog_add_button( GTK_DIALOG( w ), _( "I _Accept" ), GTK_RESPONSE_ACCEPT );
        gtk_dialog_set_default_response( GTK_DIALOG( w ), GTK_RESPONSE_ACCEPT );
        switch( gtk_dialog_run( GTK_DIALOG( w ) ) ) {
            case GTK_RESPONSE_ACCEPT:
                /* only show it once */
                gtr_pref_flag_set( PREF_KEY_USER_HAS_GIVEN_INFORMED_CONSENT, TRUE );
                gtk_widget_destroy( w );
                break;
            default:
                exit( 0 );
        }
    }
}

static void
presentMainWindow( struct cbdata * cbdata )
{
    GtkWindow * window = cbdata->wind;

    if( cbdata->is_iconified )
    {
        cbdata->is_iconified = false;

        gtk_window_set_skip_taskbar_hint( window, FALSE );
    }

    if( !gtk_widget_get_visible( GTK_WIDGET( window ) ) )
    {
        gtk_window_resize( window, gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_WIDTH ),
                                   gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_HEIGHT ) );
        gtk_window_move( window, gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_X ),
                                 gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_Y ) );
        gtr_widget_set_visible( GTK_WIDGET( window ), TRUE );
    }
    gtr_window_present( window );
}

static void
hideMainWindow( struct cbdata * cbdata )
{
    GtkWindow * window = cbdata->wind;
    gtk_window_set_skip_taskbar_hint( window, TRUE );
    gtr_widget_set_visible( GTK_WIDGET( window ), FALSE );
    cbdata->is_iconified = true;
}

static void
toggleMainWindow( struct cbdata * cbdata )
{
    if( cbdata->is_iconified )
        presentMainWindow( cbdata );
    else
        hideMainWindow( cbdata );
}

static gboolean
winclose( GtkWidget * w    UNUSED,
          GdkEvent * event UNUSED,
          gpointer         gdata )
{
    struct cbdata * cbdata = gdata;

    if( cbdata->icon != NULL )
        gtr_action_activate ( "toggle-main-window" );
    else
        on_app_exit( cbdata );

    return TRUE; /* don't propagate event further */
}

static void
rowChangedCB( GtkTreeModel  * model UNUSED,
              GtkTreePath   * path,
              GtkTreeIter   * iter  UNUSED,
              gpointer        gdata )
{
    struct cbdata * data = gdata;

    if( gtk_tree_selection_path_is_selected ( data->sel, path ) )
        refresh_actions_soon( data );
}

static void
on_drag_data_received( GtkWidget         * widget          UNUSED,
                       GdkDragContext    * drag_context,
                       gint                x               UNUSED,
                       gint                y               UNUSED,
                       GtkSelectionData  * selection_data,
                       guint               info            UNUSED,
                       guint               time_,
                       gpointer            gdata )
{
    guint i;
    char ** uris = gtk_selection_data_get_uris( selection_data );
    const guint file_count = g_strv_length( uris );
    GSList * files = NULL;

    for( i=0; i<file_count; ++i )
        files = g_slist_append( files, g_file_new_for_uri( uris[i] ) );

    open_files( files, gdata );

    /* cleanup */
    g_slist_foreach( files, (GFunc)g_object_unref, NULL );
    g_slist_free( files );
    g_strfreev( uris );

    gtk_drag_finish( drag_context, true, FALSE, time_ );
}

static void
main_window_setup( struct cbdata * cbdata, TrWindow * wind )
{
    GtkWidget * w;
    GtkTreeModel * model;
    GtkTreeSelection * sel;

    g_assert( NULL == cbdata->wind );
    cbdata->wind = GTK_WINDOW( wind );
    cbdata->sel = sel = GTK_TREE_SELECTION( gtr_window_get_selection( cbdata->wind ) );

    g_signal_connect( sel, "changed", G_CALLBACK( on_selection_changed ), cbdata );
    on_selection_changed( sel, cbdata );
    model = gtr_core_model( cbdata->core );
    g_signal_connect( model, "row-changed", G_CALLBACK( rowChangedCB ), cbdata );
    g_signal_connect( wind, "delete-event", G_CALLBACK( winclose ), cbdata );
    refresh_actions( cbdata );

    /* register to handle URIs that get dragged onto our main window */
    w = GTK_WIDGET( wind );
    gtk_drag_dest_set( w, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY );
    gtk_drag_dest_add_uri_targets( w );
    g_signal_connect( w, "drag-data-received", G_CALLBACK(on_drag_data_received), cbdata );
}

static gboolean
on_session_closed( gpointer gdata )
{
    struct cbdata * cbdata = gdata;

    /* shutdown the gui */
    while( cbdata->details != NULL ) {
        struct DetailsDialogHandle * h = cbdata->details->data;
        gtk_widget_destroy( h->dialog );
    }

    if( cbdata->prefs )
        gtk_widget_destroy( GTK_WIDGET( cbdata->prefs ) );
    if( cbdata->wind )
        gtk_widget_destroy( GTK_WIDGET( cbdata->wind ) );
    g_object_unref( cbdata->core );
    if( cbdata->icon )
        g_object_unref( cbdata->icon );
    g_slist_foreach( cbdata->error_list, (GFunc)g_free, NULL );
    g_slist_free( cbdata->error_list );
    g_slist_foreach( cbdata->duplicates_list, (GFunc)g_free, NULL );
    g_slist_free( cbdata->duplicates_list );

    return FALSE;
}

static gpointer
session_close_threadfunc( gpointer gdata )
{
    /* since tr_sessionClose() is a blocking function,
     * call it from another thread... when it's done,
     * punt the GUI teardown back to the GTK+ thread */
    struct cbdata * cbdata = gdata;
    gdk_threads_enter( );
    gtr_core_close( cbdata->core );
    gdk_threads_add_idle( on_session_closed, gdata );
    gdk_threads_leave( );
    return NULL;
}

static void
exit_now_cb( GtkWidget *w UNUSED, gpointer data UNUSED )
{
    exit( 0 );
}

static void
on_app_exit( gpointer vdata )
{
    GtkWidget *r, *p, *b, *w, *c;
    struct cbdata *cbdata = vdata;

    /* stop the update timer */
    if( cbdata->timer )
    {
        g_source_remove( cbdata->timer );
        cbdata->timer = 0;
    }

    c = GTK_WIDGET( cbdata->wind );
    gtk_container_remove( GTK_CONTAINER( c ), gtk_bin_get_child( GTK_BIN( c ) ) );

    r = gtk_alignment_new( 0.5, 0.5, 0.01, 0.01 );
    gtk_container_add( GTK_CONTAINER( c ), r );

    p = gtk_table_new( 3, 2, FALSE );
    gtk_table_set_col_spacings( GTK_TABLE( p ), GUI_PAD_BIG );
    gtk_container_add( GTK_CONTAINER( r ), p );

    w = gtk_image_new_from_stock( GTK_STOCK_NETWORK, GTK_ICON_SIZE_DIALOG );
    gtk_table_attach_defaults( GTK_TABLE( p ), w, 0, 1, 0, 2 );

    w = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL( w ), _( "<b>Closing Connections</b>" ) );
    gtk_misc_set_alignment( GTK_MISC( w ), 0.0, 0.5 );
    gtk_table_attach_defaults( GTK_TABLE( p ), w, 1, 2, 0, 1 );

    w = gtk_label_new( _( "Sending upload/download totals to tracker..." ) );
    gtk_misc_set_alignment( GTK_MISC( w ), 0.0, 0.5 );
    gtk_table_attach_defaults( GTK_TABLE( p ), w, 1, 2, 1, 2 );

    b = gtk_alignment_new( 0.0, 1.0, 0.01, 0.01 );
    w = gtk_button_new_with_mnemonic( _( "_Quit Now" ) );
    g_signal_connect( w, "clicked", G_CALLBACK( exit_now_cb ), NULL );
    gtk_container_add( GTK_CONTAINER( b ), w );
    gtk_table_attach( GTK_TABLE( p ), b, 1, 2, 2, 3, GTK_FILL, GTK_FILL, 0, 10 );

    gtk_widget_show_all( r );
    gtk_widget_grab_focus( w );

    /* clear the UI */
    gtr_core_clear( cbdata->core );

    /* ensure the window is in its previous position & size.
     * this seems to be necessary because changing the main window's
     * child seems to unset the size */
    gtk_window_resize( cbdata->wind, gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_WIDTH ),
                                     gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_HEIGHT ) );
    gtk_window_move( cbdata->wind, gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_X ),
                                   gtr_pref_int_get( PREF_KEY_MAIN_WINDOW_Y ) );

    /* shut down libT */
    g_thread_create( session_close_threadfunc, vdata, TRUE, NULL );
}

static void
show_torrent_errors( GtkWindow * window, const char * primary, GSList ** files )
{
    GSList * l;
    GtkWidget * w;
    GString * s = g_string_new( NULL );
    const char * leader = g_slist_length( *files ) > 1
                        ? gtr_get_unicode_string( GTR_UNICODE_BULLET )
                        : "";

    for( l=*files; l!=NULL; l=l->next )
        g_string_append_printf( s, "%s %s\n", leader, (const char*)l->data );

    w = gtk_message_dialog_new( window,
                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_CLOSE,
                                "%s", primary );
    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( w ),
                                              "%s", s->str );
    g_signal_connect_swapped( w, "response",
                              G_CALLBACK( gtk_widget_destroy ), w );
    gtk_widget_show( w );
    g_string_free( s, TRUE );

    g_slist_foreach( *files, (GFunc)g_free, NULL );
    g_slist_free( *files );
    *files = NULL;
}

static void
flush_torrent_errors( struct cbdata * cbdata )
{
    if( cbdata->error_list )
        show_torrent_errors( cbdata->wind,
                              ngettext( "Couldn't add corrupt torrent",
                                        "Couldn't add corrupt torrents",
                                        g_slist_length( cbdata->error_list ) ),
                              &cbdata->error_list );

    if( cbdata->duplicates_list )
        show_torrent_errors( cbdata->wind,
                              ngettext( "Couldn't add duplicate torrent",
                                        "Couldn't add duplicate torrents",
                                        g_slist_length( cbdata->duplicates_list ) ),
                              &cbdata->duplicates_list );
}

static void
on_core_error( TrCore * core UNUSED, guint code, const char * msg, struct cbdata * c )
{
    switch( code )
    {
        case TR_PARSE_ERR:
            c->error_list =
                g_slist_append( c->error_list, g_path_get_basename( msg ) );
            break;

        case TR_PARSE_DUPLICATE:
            c->duplicates_list = g_slist_append( c->duplicates_list, g_strdup( msg ) );
            break;

        case TR_CORE_ERR_NO_MORE_TORRENTS:
            flush_torrent_errors( c );
            break;

        default:
            g_assert_not_reached( );
            break;
    }
}

static gboolean
on_main_window_focus_in( GtkWidget      * widget UNUSED,
                         GdkEventFocus  * event  UNUSED,
                         gpointer                gdata )
{
    struct cbdata * cbdata = gdata;

    if( cbdata->wind )
        gtk_window_set_urgency_hint( cbdata->wind, FALSE );
    return FALSE;
}

static void
on_add_torrent( TrCore * core, tr_ctor * ctor, gpointer gdata )
{
    struct cbdata * cbdata = gdata;
    GtkWidget * w = gtr_torrent_options_dialog_new( cbdata->wind, core, ctor );

    g_signal_connect( w, "focus-in-event",
                      G_CALLBACK( on_main_window_focus_in ),  cbdata );
    if( cbdata->wind )
        gtk_window_set_urgency_hint( cbdata->wind, TRUE );

    gtk_widget_show( w );
}

static void
on_prefs_changed( TrCore * core UNUSED, const char * key, gpointer data )
{
    struct cbdata * cbdata = data;
    tr_session * tr = gtr_core_session( cbdata->core );

    if( !strcmp( key, TR_PREFS_KEY_ENCRYPTION ) )
    {
        tr_sessionSetEncryption( tr, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_DOWNLOAD_DIR ) )
    {
        tr_sessionSetDownloadDir( tr, gtr_pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_MSGLEVEL ) )
    {
        tr_setMessageLevel( gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_PORT ) )
    {
        tr_sessionSetPeerPort( tr, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_BLOCKLIST_ENABLED ) )
    {
        tr_blocklistSetEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_BLOCKLIST_URL ) )
    {
        tr_blocklistSetURL( tr, gtr_pref_string_get( key ) );
    }
    else if( !strcmp( key, PREF_KEY_SHOW_TRAY_ICON ) )
    {
        const int show = gtr_pref_flag_get( key );
        if( show && !cbdata->icon )
            cbdata->icon = gtr_icon_new( cbdata->core );
        else if( !show && cbdata->icon ) {
            g_object_unref( cbdata->icon );
            cbdata->icon = NULL;
        }
    }
    else if( !strcmp( key, TR_PREFS_KEY_DSPEED_ENABLED ) )
    {
        tr_sessionLimitSpeed( tr, TR_DOWN, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_DSPEED_KBps ) )
    {
        tr_sessionSetSpeedLimit_KBps( tr, TR_DOWN, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_USPEED_ENABLED ) )
    {
        tr_sessionLimitSpeed( tr, TR_UP, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_USPEED_KBps ) )
    {
        tr_sessionSetSpeedLimit_KBps( tr, TR_UP, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RATIO_ENABLED ) )
    {
        tr_sessionSetRatioLimited( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RATIO ) )
    {
        tr_sessionSetRatioLimit( tr, gtr_pref_double_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_IDLE_LIMIT ) )
    {
        tr_sessionSetIdleLimit( tr, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_IDLE_LIMIT_ENABLED ) )
    {
        tr_sessionSetIdleLimited( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PORT_FORWARDING ) )
    {
        tr_sessionSetPortForwardingEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEX_ENABLED ) )
    {
        tr_sessionSetPexEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RENAME_PARTIAL_FILES ) )
    {
        tr_sessionSetIncompleteFileNamingEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_DOWNLOAD_QUEUE_SIZE ) )
    {
        tr_sessionSetQueueSize( tr, TR_DOWN, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_QUEUE_STALLED_MINUTES ) )
    {
        tr_sessionSetQueueStalledMinutes( tr, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_DHT_ENABLED ) )
    {
        tr_sessionSetDHTEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_UTP_ENABLED ) )
    {
        tr_sessionSetUTPEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_LPD_ENABLED ) )
    {
        tr_sessionSetLPDEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_PORT ) )
    {
        tr_sessionSetRPCPort( tr, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_ENABLED ) )
    {
        tr_sessionSetRPCEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_WHITELIST ) )
    {
        tr_sessionSetRPCWhitelist( tr, gtr_pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_WHITELIST_ENABLED ) )
    {
        tr_sessionSetRPCWhitelistEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_USERNAME ) )
    {
        tr_sessionSetRPCUsername( tr, gtr_pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_PASSWORD ) )
    {
        tr_sessionSetRPCPassword( tr, gtr_pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_AUTH_REQUIRED ) )
    {
        tr_sessionSetRPCPasswordEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_UP_KBps ) )
    {
        tr_sessionSetAltSpeed_KBps( tr, TR_UP, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_DOWN_KBps ) )
    {
        tr_sessionSetAltSpeed_KBps( tr, TR_DOWN, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_ENABLED ) )
    {
        const gboolean b = gtr_pref_flag_get( key );
        tr_sessionUseAltSpeed( tr, b );
        gtr_action_set_toggled( key, b );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN ) )
    {
        tr_sessionSetAltSpeedBegin( tr, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_TIME_END ) )
    {
        tr_sessionSetAltSpeedEnd( tr, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED ) )
    {
        tr_sessionUseAltSpeedTime( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_TIME_DAY ) )
    {
        tr_sessionSetAltSpeedDay( tr, gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START ) )
    {
        tr_sessionSetPeerPortRandomOnStart( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_INCOMPLETE_DIR ) )
    {
        tr_sessionSetIncompleteDir( tr, gtr_pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED ) )
    {
        tr_sessionSetIncompleteDirEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_ENABLED ) )
    {
        tr_sessionSetTorrentDoneScriptEnabled( tr, gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_FILENAME ) )
    {
        tr_sessionSetTorrentDoneScript( tr, gtr_pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_START) )
    {
        tr_sessionSetPaused( tr, !gtr_pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_TRASH_ORIGINAL ) )
    {
        tr_sessionSetDeleteSource( tr, gtr_pref_flag_get( key ) );
    }
}

static gboolean
update_model_once( gpointer gdata )
{
    struct cbdata *data = gdata;

    /* update the torrent data in the model */
    gtr_core_update( data->core );

    /* refresh the main window's statusbar and toolbar buttons */
    if( data->wind != NULL )
        gtr_window_refresh( data->wind );

    /* update the actions */
        refresh_actions( data );

    /* update the status tray icon */
    if( data->icon != NULL )
        gtr_icon_refresh( data->icon );

    data->update_model_soon_tag = 0;
    return FALSE;
}

static void
update_model_soon( gpointer gdata )
{
    struct cbdata *data = gdata;

    if( data->update_model_soon_tag == 0 )
        data->update_model_soon_tag = gdk_threads_add_idle( update_model_once, data );
}

static gboolean
update_model_loop( gpointer gdata )
{
    const gboolean done = global_sigcount;

    if( !done )
        update_model_once( gdata );

    return !done;
}

static void
show_about_dialog( GtkWindow * parent )
{
    GtkWidget * d;
    const char * website_uri = "http://www.transmissionbt.com/";
    const char * authors[] = {
        "Jordan Lee (Backend; GTK+)",
        "Mitchell Livingston (Backend; OS X)",
        NULL
    };

    d = g_object_new( GTK_TYPE_ABOUT_DIALOG,
                      "authors", authors,
                      "comments", _( "A fast and easy BitTorrent client" ),
                      "copyright", _( "Copyright (c) The Transmission Project" ),
                      "logo-icon-name", MY_CONFIG_NAME,
                      "name", g_get_application_name( ),
                      /* Translators: translate "translator-credits" as your name
                         to have it appear in the credits in the "About"
                         dialog */
                      "translator-credits", _( "translator-credits" ),
                      "version", LONG_VERSION_STRING,
                      "website", website_uri,
                      "website-label", website_uri,
#ifdef SHOW_LICENSE
                      "license", LICENSE,
                      "wrap-license", TRUE,
#endif
                      NULL );
    gtk_window_set_transient_for( GTK_WINDOW( d ), parent );
    g_signal_connect_swapped( d, "response", G_CALLBACK (gtk_widget_destroy), d );
    gtk_widget_show( d );
}

static void
append_id_to_benc_list( GtkTreeModel * m, GtkTreePath * path UNUSED,
                        GtkTreeIter * iter, gpointer list )
{
    tr_torrent * tor = NULL;
    gtk_tree_model_get( m, iter, MC_TORRENT, &tor, -1 );
    tr_bencListAddInt( list, tr_torrentId( tor ) );
}

static gboolean
call_rpc_for_selected_torrents( struct cbdata * data, const char * method )
{
    tr_benc top, *args, *ids;
    gboolean invoked = FALSE;
    GtkTreeSelection * s = data->sel;
    tr_session * session = gtr_core_session( data->core );

    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", method );
    args = tr_bencDictAddDict( &top, "arguments", 1 );
    ids = tr_bencDictAddList( args, "ids", 0 );
    gtk_tree_selection_selected_foreach( s, append_id_to_benc_list, ids );

    if( tr_bencListSize( ids ) != 0 )
    {
        int json_len;
        char * json = tr_bencToStr( &top, TR_FMT_JSON_LEAN, &json_len );
        tr_rpc_request_exec_json( session, json, json_len, NULL, NULL );
        g_free( json );
        invoked = TRUE;
    }

    tr_bencFree( &top );
    return invoked;
}

static void
open_folder_foreach( GtkTreeModel * model, GtkTreePath * path UNUSED,
                     GtkTreeIter * iter, gpointer core )
{
    int id;
    gtk_tree_model_get( model, iter, MC_TORRENT_ID, &id, -1 );
    gtr_core_open_folder( core, id );
}

static gboolean
on_message_window_closed( void )
{
    gtr_action_set_toggled( "toggle-message-log", FALSE );
    return FALSE;
}

static void
accumulate_selected_torrents( GtkTreeModel  * model, GtkTreePath   * path UNUSED,
                              GtkTreeIter   * iter, gpointer        gdata )
{
    int id;
    GSList ** data = gdata;

    gtk_tree_model_get( model, iter, MC_TORRENT_ID, &id, -1 );
    *data = g_slist_append( *data, GINT_TO_POINTER( id ) );
}

static void
remove_selected( struct cbdata * data, gboolean delete_files )
{
    GSList * l = NULL;

    gtk_tree_selection_selected_foreach( data->sel, accumulate_selected_torrents, &l );

    if( l != NULL )
        gtr_confirm_remove( data->wind, data->core, l, delete_files );
}

static void
start_all_torrents( struct cbdata * data )
{
    tr_session * session = gtr_core_session( data->core );
    const char * cmd = "{ \"method\": \"torrent-start\" }";
    tr_rpc_request_exec_json( session, cmd, strlen( cmd ), NULL, NULL );
}

static void
pause_all_torrents( struct cbdata * data )
{
    tr_session * session = gtr_core_session( data->core );
    const char * cmd = "{ \"method\": \"torrent-stop\" }";
    tr_rpc_request_exec_json( session, cmd, strlen( cmd ), NULL, NULL );
}

static tr_torrent*
get_first_selected_torrent( struct cbdata * data )
{
    tr_torrent * tor = NULL;
    GtkTreeModel * m;
    GList * l = gtk_tree_selection_get_selected_rows( data->sel, &m );
    if( l != NULL ) {
        GtkTreePath * p = l->data;
        GtkTreeIter i;
        if( gtk_tree_model_get_iter( m, &i, p ) )
            gtk_tree_model_get( m, &i, MC_TORRENT, &tor, -1 );
    }
    g_list_foreach( l, (GFunc)gtk_tree_path_free, NULL );
    g_list_free( l );
    return tor;
}

static void
copy_magnet_link_to_clipboard( GtkWidget * w, tr_torrent * tor )
{
    char * magnet = tr_torrentGetMagnetLink( tor );
    GdkDisplay * display = gtk_widget_get_display( w );
    GdkAtom selection;
    GtkClipboard * clipboard;

    /* this is The Right Thing for copy/paste... */
    selection = GDK_SELECTION_CLIPBOARD;
    clipboard = gtk_clipboard_get_for_display( display, selection );
    gtk_clipboard_set_text( clipboard, magnet, -1 );

    /* ...but people using plain ol' X need this instead */
    selection = GDK_SELECTION_PRIMARY;
    clipboard = gtk_clipboard_get_for_display( display, selection );
    gtk_clipboard_set_text( clipboard, magnet, -1 );

    /* cleanup */
    tr_free( magnet );
}

void
gtr_actions_handler( const char * action_name, gpointer user_data )
{
    struct cbdata * data = user_data;
    gboolean        changed = FALSE;

    if( !strcmp( action_name, "open-torrent-from-url" ) )
    {
        GtkWidget * w = gtr_torrent_open_from_url_dialog_new( data->wind, data->core );
        gtk_widget_show( w );
    }
    else if(  !strcmp( action_name, "open-torrent-menu" )
      || !strcmp( action_name, "open-torrent-toolbar" ) )
    {
        GtkWidget * w = gtr_torrent_open_from_file_dialog_new( data->wind, data->core );
        gtk_widget_show( w );
    }
    else if( !strcmp( action_name, "show-stats" ) )
    {
        GtkWidget * dialog = gtr_stats_dialog_new( data->wind, data->core );
        gtk_widget_show( dialog );
    }
    else if( !strcmp( action_name, "donate" ) )
    {
        gtr_open_uri( "http://www.transmissionbt.com/donate.php" );
    }
    else if( !strcmp( action_name, "pause-all-torrents" ) )
    {
        pause_all_torrents( data );
    }
    else if( !strcmp( action_name, "start-all-torrents" ) )
    {
        start_all_torrents( data );
    }
    else if( !strcmp( action_name, "copy-magnet-link-to-clipboard" ) )
    {
        tr_torrent * tor = get_first_selected_torrent( data );
        if( tor != NULL )
        {
            copy_magnet_link_to_clipboard( GTK_WIDGET( data->wind ), tor );
        }
    }
    else if( !strcmp( action_name, "relocate-torrent" ) )
    {
        GSList * ids = getSelectedTorrentIds( data );
        if( ids != NULL )
        {
            GtkWindow * parent = data->wind;
            GtkWidget * w = gtr_relocate_dialog_new( parent, data->core, ids );
            gtk_widget_show( w );
        }
    }
    else if( !strcmp( action_name, "torrent-start" )
          || !strcmp( action_name, "torrent-start-now" )
          || !strcmp( action_name, "torrent-stop" )
          || !strcmp( action_name, "torrent-reannounce" )
          || !strcmp( action_name, "torrent-verify" )
          || !strcmp( action_name, "queue-move-top" )
          || !strcmp( action_name, "queue-move-up" )
          || !strcmp( action_name, "queue-move-down" )
          || !strcmp( action_name, "queue-move-bottom" ) )
    {
        changed |= call_rpc_for_selected_torrents( data, action_name );
    }
    else if( !strcmp( action_name, "open-torrent-folder" ) )
    {
        gtk_tree_selection_selected_foreach( data->sel, open_folder_foreach, data->core );
    }
    else if( !strcmp( action_name, "show-torrent-properties" ) )
    {
        show_details_dialog_for_selected_torrents( data );
    }
    else if( !strcmp( action_name, "new-torrent" ) )
    {
        GtkWidget * w = gtr_torrent_creation_dialog_new( data->wind, data->core );
        gtk_widget_show( w );
    }
    else if( !strcmp( action_name, "remove-torrent" ) )
    {
        remove_selected( data, FALSE );
    }
    else if( !strcmp( action_name, "delete-torrent" ) )
    {
        remove_selected( data, TRUE );
    }
    else if( !strcmp( action_name, "quit" ) )
    {
        on_app_exit( data );
    }
    else if( !strcmp( action_name, "select-all" ) )
    {
        gtk_tree_selection_select_all( data->sel );
    }
    else if( !strcmp( action_name, "deselect-all" ) )
    {
        gtk_tree_selection_unselect_all( data->sel );
    }
    else if( !strcmp( action_name, "edit-preferences" ) )
    {
        if( NULL == data->prefs )
        {
            data->prefs = gtr_prefs_dialog_new( data->wind, G_OBJECT( data->core ) );
            g_signal_connect( data->prefs, "destroy",
                              G_CALLBACK( gtk_widget_destroyed ), &data->prefs );
        }
        gtr_window_present( GTK_WINDOW( data->prefs ) );
    }
    else if( !strcmp( action_name, "toggle-message-log" ) )
    {
        if( !data->msgwin )
        {
            GtkWidget * win = gtr_message_log_window_new( data->wind, data->core );
            g_signal_connect( win, "destroy", G_CALLBACK( on_message_window_closed ), NULL );
            data->msgwin = win;
        }
        else
        {
            gtr_action_set_toggled( "toggle-message-log", FALSE );
            gtk_widget_destroy( data->msgwin );
            data->msgwin = NULL;
        }
    }
    else if( !strcmp( action_name, "show-about-dialog" ) )
    {
        show_about_dialog( data->wind );
    }
    else if( !strcmp ( action_name, "help" ) )
    {
        gtr_open_uri( gtr_get_help_uri( ) );
    }
    else if( !strcmp( action_name, "toggle-main-window" ) )
    {
        toggleMainWindow( data );
    }
    else if( !strcmp( action_name, "present-main-window" ) )
    {
        presentMainWindow( data );
    }
    else g_error ( "Unhandled action: %s", action_name );

    if( changed )
        update_model_soon( data );
}
