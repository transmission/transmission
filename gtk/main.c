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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

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
    gboolean                    is_iconified;
    guint                       timer;
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
    int active_count;
    int inactive_count;
};

static void
get_selected_torrent_counts_foreach( GtkTreeModel * model, GtkTreePath * path UNUSED,
                                     GtkTreeIter * iter, gpointer user_data )
{
    int activity = 0;
    struct counts_data * counts = user_data;

    ++counts->total_count;

    gtk_tree_model_get( model, iter, MC_ACTIVITY, &activity, -1 );

    if( activity == TR_STATUS_STOPPED )
        ++counts->inactive_count;
    else
        ++counts->active_count;
}

static void
get_selected_torrent_counts( struct cbdata * data, struct counts_data * counts )
{
    counts->active_count = 0;
    counts->inactive_count = 0;
    counts->total_count = 0;

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

    gtr_action_set_sensitive( "select-all", torrent_count != 0 );
    gtr_action_set_sensitive( "deselect-all", torrent_count != 0 );
    gtr_action_set_sensitive( "pause-all-torrents", active != 0 );
    gtr_action_set_sensitive( "start-all-torrents", active != total );

    get_selected_torrent_counts( data, &sel_counts );
    gtr_action_set_sensitive( "pause-torrent", sel_counts.active_count != 0 );
    gtr_action_set_sensitive( "start-torrent", sel_counts.inactive_count != 0 );
    gtr_action_set_sensitive( "remove-torrent", sel_counts.total_count != 0 );
    gtr_action_set_sensitive( "delete-torrent", sel_counts.total_count != 0 );
    gtr_action_set_sensitive( "verify-torrent", sel_counts.total_count != 0 );
    gtr_action_set_sensitive( "relocate-torrent", sel_counts.total_count != 0 );
    gtr_action_set_sensitive( "show-torrent-properties", sel_counts.total_count != 0 );
    gtr_action_set_sensitive( "open-torrent-folder", sel_counts.total_count == 1 );
    gtr_action_set_sensitive( "copy-magnet-link-to-clipboard", sel_counts.total_count == 1 );

    canUpdate = 0;
    gtk_tree_selection_selected_foreach( data->sel, count_updatable_foreach, &canUpdate );
    gtr_action_set_sensitive( "update-tracker", canUpdate != 0 );

    data->refresh_actions_tag = 0;
    return FALSE;
}

static void
on_selection_changed( GtkTreeSelection * s UNUSED, gpointer gdata )
{
    struct cbdata * data = gdata;

    if( data->refresh_actions_tag == 0 )
        data->refresh_actions_tag = gtr_idle_add( refresh_actions, data );
}

/***
****
****
***/

static void app_setup( TrWindow       * wind,
                       GSList         * torrent_files,
                       struct cbdata  * cbdata,
                       gboolean         paused,
                       gboolean         minimized );

static void main_window_setup( struct cbdata * cbdata, TrWindow * wind );

static void on_app_exit( gpointer vdata );

static void on_core_error( TrCore *, guint, const char *, struct cbdata * );

static void on_add_torrent( TrCore *, tr_ctor *, gpointer );

static void on_prefs_changed( TrCore * core, const char * key, gpointer );

static gboolean update_model( gpointer gdata );

/***
****
***/

static void
register_magnet_link_handler( void )
{
#ifdef HAVE_GIO
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
#endif
}

static void
on_main_window_size_allocated( GtkWidget      * gtk_window,
                               GtkAllocation  * alloc UNUSED,
                               gpointer         gdata UNUSED )
{
    GdkWindow * gdk_window = gtr_widget_get_window( gtk_window );
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
            gtr_idle_add( rpc_torrent_add_idle, data );
            break;
        }

        case TR_RPC_TORRENT_REMOVING:
        case TR_RPC_TORRENT_TRASHING: {
            struct torrent_idle_data * data = g_new0( struct torrent_idle_data, 1 );
            data->id = tr_torrentId( tor );
            data->core = cbdata->core;
            data->delete_files = type == TR_RPC_TORRENT_TRASHING;
            gtr_idle_add( rpc_torrent_remove_idle, data );
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

static void
setupsighandlers( void )
{
    signal( SIGINT, signal_handler );
    signal( SIGKILL, signal_handler );
}

/***
****
***/

static GSList *
checkfilenames( int argc, char **argv )
{
    int i;
    GSList * ret = NULL;
    char * pwd = g_get_current_dir( );

    for( i=0; i<argc; ++i )
    {
        const char * arg = argv[i];

        if( gtr_is_supported_url( arg ) || gtr_is_magnet_link( arg ) )
        {
            ret = g_slist_prepend( ret, g_strdup( arg ) );
        }
        else /* local file */
        {
            char * filename;

            if( g_path_is_absolute( arg ) )
                filename = g_strdup( arg );
            else {
                filename = g_filename_from_uri( arg, NULL, NULL );

                if( filename == NULL )
                    filename = g_build_filename( pwd, arg, NULL );
            }

            if( g_file_test( filename, G_FILE_TEST_EXISTS ) )
                ret = g_slist_prepend( ret, filename );
            else {
                if( gtr_is_hex_hashcode( argv[i] ) )
                    ret = g_slist_prepend( ret, g_strdup_printf( "magnet:?xt=urn:btih:%s", argv[i] ) );
                g_free( filename );
            }
        }
    }

    g_free( pwd );
    return g_slist_reverse( ret );
}

/****
*****
*****
****/

int
main( int argc, char ** argv )
{
    char * err = NULL;
    GSList * argfiles;
    GError * gerr;
    gboolean didinit = FALSE;
    gboolean didlock = FALSE;
    gboolean showversion = FALSE;
    gboolean startpaused = FALSE;
    gboolean startminimized = FALSE;
    const char * domain = MY_READABLE_NAME;
    char * configDir = NULL;
    gtr_lockfile_state_t tr_state;

    GOptionEntry entries[] = {
        { "paused",     'p', 0, G_OPTION_ARG_NONE,
          &startpaused, _( "Start with all torrents paused" ), NULL },
        { "version",    '\0', 0, G_OPTION_ARG_NONE,
          &showversion, _( "Show version number and exit" ), NULL },
#ifdef STATUS_ICON_SUPPORTED
        { "minimized",  'm', 0, G_OPTION_ARG_NONE,
          &startminimized,
          _( "Start minimized in notification area" ), NULL },
#endif
        { "config-dir", 'g', 0, G_OPTION_ARG_FILENAME, &configDir,
          _( "Where to look for configuration files" ), NULL },
        { NULL, 0,   0, 0, NULL, NULL, NULL }
    };

    /* bind the gettext domain */
    setlocale( LC_ALL, "" );
    bindtextdomain( domain, TRANSMISSIONLOCALEDIR );
    bind_textdomain_codeset( domain, "UTF-8" );
    textdomain( domain );
    g_set_application_name( _( "Transmission" ) );
    tr_formatter_mem_init( mem_K, _(mem_K_str), _(mem_M_str), _(mem_G_str), _(mem_T_str) );
    tr_formatter_size_init( disk_K, _(disk_K_str), _(disk_M_str), _(disk_G_str), _(disk_T_str) );
    tr_formatter_speed_init( speed_K, _(speed_K_str), _(speed_M_str), _(speed_G_str), _(speed_T_str) );

    /* initialize gtk */
    if( !g_thread_supported( ) )
        g_thread_init( NULL );

    gerr = NULL;
    if( !gtk_init_with_args( &argc, &argv, (char*)_( "[torrent files or urls]" ), entries,
                             (char*)domain, &gerr ) )
    {
        fprintf( stderr, "%s\n", gerr->message );
        g_clear_error( &gerr );
        return 0;
    }

    if( showversion )
    {
        fprintf( stderr, "%s %s\n", MY_READABLE_NAME, LONG_VERSION_STRING );
        return 0;
    }

    if( configDir == NULL )
        configDir = (char*) tr_getDefaultConfigDir( MY_CONFIG_NAME );

    didinit = cf_init( configDir, NULL ); /* must come before actions_init */

    setupsighandlers( ); /* set up handlers for fatal signals */

    didlock = cf_lock( &tr_state, &err );
    argfiles = checkfilenames( argc - 1, argv + 1 );

    if( !didlock && argfiles )
    {
        /* We have torrents to add but there's another copy of Transmsision
         * running... chances are we've been invoked from a browser, etc.
         * So send the files over to the "real" copy of Transmission, and
         * if that goes well, then our work is done. */
        GSList * l;
        gboolean delegated = FALSE;
        const gboolean trash_originals = gtr_pref_flag_get( TR_PREFS_KEY_TRASH_ORIGINAL );

        for( l=argfiles; l!=NULL; l=l->next )
        {
            const char * filename = l->data;
            const gboolean added = gtr_dbus_add_torrent( filename );

            if( added && trash_originals )
                gtr_file_trash_or_remove( filename );

            delegated |= added;
        }

        if( delegated ) {
            g_slist_foreach( argfiles, (GFunc)g_free, NULL );
            g_slist_free( argfiles );
            argfiles = NULL;

            if( err ) {
                g_free( err );
                err = NULL;
            }
        }
    }
    else if( ( !didlock ) && ( tr_state == GTR_LOCKFILE_ELOCK ) )
    {
        /* There's already another copy of Transmission running,
         * so tell it to present its window to the user */
        err = NULL;
        if( !gtr_dbus_present_window( ) )
            err = g_strdup( _( "Transmission is already running, but is not responding. To start a new session, you must first close the existing Transmission process." ) );
    }

    if( didlock && ( didinit || cf_init( configDir, &err ) ) )
    {
        /* No other copy of Transmission running...
         * so we're going to be the primary. */

        const char * str;
        GtkWindow * win;
        GtkUIManager * myUIManager;
        tr_session * session;
        struct cbdata * cbdata = g_new0( struct cbdata, 1 );

        sighandler_cbdata = cbdata;

        /* ensure the directories are created */
        if(( str = gtr_pref_string_get( TR_PREFS_KEY_DOWNLOAD_DIR )))
            gtr_mkdir_with_parents( str, 0777 );
        if(( str = gtr_pref_string_get( TR_PREFS_KEY_INCOMPLETE_DIR )))
            gtr_mkdir_with_parents( str, 0777 );

        /* initialize the libtransmission session */
        session = tr_sessionInit( "gtk", configDir, TRUE, gtr_pref_get_all( ) );

        gtr_pref_flag_set( TR_PREFS_KEY_ALT_SPEED_ENABLED, tr_sessionUsesAltSpeed( session ) );
        gtr_pref_int_set( TR_PREFS_KEY_PEER_PORT, tr_sessionGetPeerPort( session ) );
        cbdata->core = gtr_core_new( session );

        /* init the ui manager */
        myUIManager = gtk_ui_manager_new ( );
        gtr_actions_init ( myUIManager, cbdata );
        gtk_ui_manager_add_ui_from_string ( myUIManager, fallback_ui_file, -1, NULL );
        gtk_ui_manager_ensure_update ( myUIManager );
        gtk_window_set_default_icon_name ( MY_CONFIG_NAME );

        /* create main window now to be a parent to any error dialogs */
        win = GTK_WINDOW( gtr_window_new( myUIManager, cbdata->core ) );
        g_signal_connect( win, "size-allocate", G_CALLBACK( on_main_window_size_allocated ), cbdata );

        app_setup( win, argfiles, cbdata, startpaused, startminimized );
        tr_sessionSetRPCCallback( session, on_rpc_changed, cbdata );

        /* on startup, check & see if it's time to update the blocklist */
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

        gtk_main( );
    }
    else if( err )
    {
        const char * primary_text = _( "Transmission cannot be started." );
        GtkWidget * w = gtk_message_dialog_new( NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, primary_text, NULL );
        gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( w ), "%s", err );
        g_signal_connect( w, "response", G_CALLBACK(gtk_main_quit), NULL );
        gtk_widget_show( w );
        g_free( err );
        gtk_main( );
    }

    return 0;
}

static void
on_core_busy( TrCore * core UNUSED, gboolean busy, struct cbdata * c )
{
    gtr_window_set_busy( c->wind, busy );
}

static void
app_setup( TrWindow      * wind,
           GSList        * files,
           struct cbdata * cbdata,
           gboolean        pause_all,
           gboolean        is_iconified )
{
    const gboolean do_start = gtr_pref_flag_get( TR_PREFS_KEY_START ) && !pause_all;
    const gboolean do_prompt = gtr_pref_flag_get( PREF_KEY_OPTIONS_PROMPT );
    const gboolean do_notify = TRUE;

    cbdata->is_iconified = is_iconified;

    if( is_iconified )
        gtr_pref_flag_set( PREF_KEY_SHOW_TRAY_ICON, TRUE );

    gtr_actions_set_core( cbdata->core );

    /* set up core handlers */
    g_signal_connect( cbdata->core, "busy", G_CALLBACK( on_core_busy ), cbdata );
    g_signal_connect( cbdata->core, "add-error", G_CALLBACK( on_core_error ), cbdata );
    g_signal_connect( cbdata->core, "add-prompt", G_CALLBACK( on_add_torrent ), cbdata );
    g_signal_connect( cbdata->core, "prefs-changed", G_CALLBACK( on_prefs_changed ), cbdata );

    /* add torrents from command-line and saved state */
    gtr_core_load( cbdata->core, pause_all );
    gtr_core_add_list( cbdata->core, files, do_start, do_prompt, do_notify );
    files = NULL;
    gtr_core_torrents_added( cbdata->core );

    /* set up main window */
    main_window_setup( cbdata, wind );

    /* set up the icon */
    on_prefs_changed( cbdata->core, PREF_KEY_SHOW_TRAY_ICON, cbdata );

    /* start model update timer */
    cbdata->timer = gtr_timeout_add_seconds( MAIN_WINDOW_REFRESH_INTERVAL_SECONDS, update_model, cbdata );
    update_model( cbdata );

    /* either show the window or iconify it */
    if( !is_iconified )
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
toggleMainWindow( struct cbdata * cbdata )
{
    GtkWindow * window = cbdata->wind;
    const int   doShow = cbdata->is_iconified;
    static int  x = 0;
    static int  y = 0;

    if( doShow )
    {
        cbdata->is_iconified = 0;
        gtk_window_set_skip_taskbar_hint( window, FALSE );
        gtk_window_move( window, x, y );
        gtr_widget_set_visible( GTK_WIDGET( window ), TRUE );
        gtr_window_present( window );
    }
    else
    {
        gtk_window_get_position( window, &x, &y );
        gtk_window_set_skip_taskbar_hint( window, TRUE );
        gtr_widget_set_visible( GTK_WIDGET( window ), FALSE );
        cbdata->is_iconified = 1;
    }
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
        refresh_actions( gdata );
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
    int i;
    gboolean success = FALSE;
    GSList * files = NULL;
    struct cbdata * data = gdata;
    char ** uris = gtk_selection_data_get_uris( selection_data );

    /* try to add the filename URIs... */
    for( i=0; uris && uris[i]; ++i )
    {
        const char * uri = uris[i];
        char * filename = g_filename_from_uri( uri, NULL, NULL );

        if( filename && g_file_test( filename, G_FILE_TEST_EXISTS ) )
        {
            files = g_slist_append( files, g_strdup( filename ) );
            success = TRUE;
        }
        else if( tr_urlIsValid( uri, -1 ) || gtr_is_magnet_link( uri ) )
        {
            gtr_core_add_from_url( data->core, uri );
            success = TRUE;
        }

        g_free( filename );
    }

    if( files )
        gtr_core_add_list_defaults( data->core, g_slist_reverse( files ), TRUE );

    gtr_core_torrents_added( data->core );
    gtk_drag_finish( drag_context, success, FALSE, time_ );

    /* cleanup */
    g_strfreev( uris );
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
    g_free( cbdata );

    gtk_main_quit( );

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
    gtr_idle_add( on_session_closed, gdata );
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
update_model( gpointer gdata )
{
    struct cbdata *data = gdata;
    const gboolean done = global_sigcount;

    if( !done )
    {
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
    }

    return !done;
}

/* GTK+ versions before 2.18.0 don't have a default URI hook... */
#if !GTK_CHECK_VERSION(2,18,0)
 #define NEED_URL_HOOK
#endif

#ifdef NEED_URL_HOOK
static void
on_uri_clicked( GtkAboutDialog * u UNUSED, const gchar * uri, gpointer u2 UNUSED )
{
    gtr_open_uri( uri );
}
#endif

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

#ifdef NEED_URL_HOOK
    gtk_about_dialog_set_url_hook( on_uri_clicked, NULL, NULL );
#endif

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
    else if( !strcmp( action_name, "start-torrent" ) )
    {
        changed |= call_rpc_for_selected_torrents( data, "torrent-start" );
    }
    else if( !strcmp( action_name, "pause-torrent" ) )
    {
        changed |= call_rpc_for_selected_torrents( data, "torrent-stop" );
    }
    else if( !strcmp( action_name, "verify-torrent" ) )
    {
        changed |= call_rpc_for_selected_torrents( data, "torrent-verify" );
    }
    else if( !strcmp( action_name, "update-tracker" ) )
    {
        changed |= call_rpc_for_selected_torrents( data, "torrent-reannounce" );
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
    else g_error ( "Unhandled action: %s", action_name );

    if( changed )
        update_model( data );
}
