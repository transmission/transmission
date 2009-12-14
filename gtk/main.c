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

#include <locale.h>
#include <sys/param.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <libtransmission/transmission.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "actions.h"
#include "add-dialog.h"
#include "conf.h"
#include "details.h"
#include "dialogs.h"
#include "hig.h"
#include "makemeta-ui.h"
#include "msgwin.h"
#include "notify.h"
#include "relocate.h"
#include "stats.h"
#include "tr-core.h"
#include "tr-icon.h"
#include "tr-prefs.h"
#include "tr-torrent.h"
#include "tr-window.h"
#include "util.h"
#include "ui.h"

#define MY_NAME "transmission"

#define REFRESH_INTERVAL_SECONDS 2

#if GTK_CHECK_VERSION( 2, 8, 0 )
 #define SHOW_LICENSE
static const char * LICENSE =
"The OS X client, CLI client, and parts of libtransmission are licensed under the terms of the MIT license.\n\n"
"The Transmission daemon, GTK+ client, Qt client, Web client, and most of libtransmission are licensed under the terms of the GNU GPL version 2, with two special exceptions:\n\n"
"1. The MIT-licensed portions of Transmission listed above are exempt from GPLv2 clause 2(b) and may retain their MIT license.\n\n"
"2. Permission is granted to link the code in this release with the OpenSSL project's 'OpenSSL' library and to distribute the linked executables.  Works derived from Transmission may, at their authors' discretion, keep or delete this exception.";
#endif

struct cbdata
{
    gboolean            isIconified;
    gboolean            isClosing;
    guint               timer;
    gpointer            icon;
    GtkWindow         * wind;
    TrCore            * core;
    GtkWidget         * msgwin;
    GtkWidget         * prefs;
    GSList            * errqueue;
    GSList            * dupqueue;
    GSList            * details;
    GtkTreeSelection  * sel;
};

/**
***
**/

static int
compareInts( const void * a, const void * b )
{
    return *(int*)a - *(int*)b;
}

static char*
getDetailsDialogKey( GSList * id_list )
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
    qsort( ids, n, sizeof(int), compareInts );

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
    GtkTreeSelection * s;
    GtkTreeModel * model;
    GSList * ids = NULL;
    GList * paths = NULL;
    GList * l;

    /* build a list of the selected torrents' ids */
    s = tr_window_get_selection( data->wind );
    for( paths=l=gtk_tree_selection_get_selected_rows(s,&model); l; l=l->next ) {
        GtkTreeIter iter;
        if( gtk_tree_model_get_iter( model, &iter, l->data ) ) {
            tr_torrent * tor;
            gtk_tree_model_get( model, &iter, MC_TORRENT_RAW, &tor, -1 );
            ids = g_slist_append( ids, GINT_TO_POINTER( tr_torrentId( tor ) ) );
        }
    }

    /* cleanup */
    g_list_foreach( paths, (GFunc)gtk_tree_path_free, NULL );
    g_list_free( paths );
    return ids;
}

static struct DetailsDialogHandle*
findDetailsDialogFromIds( struct cbdata * cbdata, GSList * ids )
{
    GSList * l;
    struct DetailsDialogHandle * ret = NULL;
    char * key = getDetailsDialogKey( ids );

    for( l=cbdata->details; l!=NULL && ret==NULL; l=l->next ) {
        struct DetailsDialogHandle * h = l->data;
        if( !strcmp( h->key, key ) )
            ret = h;
    }

    g_free( key );
    return ret;
}

static struct DetailsDialogHandle*
findDetailsDialogFromWidget( struct cbdata * cbdata, gpointer w )
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

/***
****
***/

static void           appsetup( TrWindow * wind,
                                GSList *   args,
                                struct     cbdata *,
                                gboolean   paused,
                                gboolean   minimized );

static void           winsetup( struct cbdata * cbdata,
                                TrWindow *      wind );

static void           wannaquit( gpointer vdata );

static void           setupdrag( GtkWidget *    widget,
                                 struct cbdata *data );

static void           gotdrag( GtkWidget *       widget,
                               GdkDragContext *  dc,
                               gint              x,
                               gint              y,
                               GtkSelectionData *sel,
                               guint             info,
                               guint             time,
                               gpointer          gdata );

static void coreerr( TrCore *, guint, const char *, struct cbdata * );

static void           onAddTorrent( TrCore *,
                                    tr_ctor *,
                                    gpointer );

static void           prefschanged( TrCore *     core,
                                    const char * key,
                                    gpointer     data );

static gboolean       updatemodel( gpointer gdata );

/***
****
***/

#ifdef HAVE_LIBGCONF
 #include <gconf/gconf.h>
 #include <gconf/gconf-client.h>
#endif

static void
registerMagnetLinkHandler( void )
{
#ifdef HAVE_LIBGCONF
    GError * err;
    GConfValue * value;
    GConfClient * client = gconf_client_get_default( );
    const char * key = "/desktop/gnome/url-handlers/magnet/command";

    /* if there's already a manget handler registered, don't do anything */
    value = gconf_client_get( client, key, NULL );
    if( value != NULL )
    {
        gconf_value_free( value );
        return;
    }

    err = NULL;
    if( !gconf_client_set_string( client, key, "transmission '%s'", &err ) )
    {
        tr_inf( "Unable to register Transmission as default magnet link handler: \"%s\"", err->message );
        g_clear_error( &err );
    }
    else
    {
        gconf_client_set_bool( client, "/desktop/gnome/url-handlers/magnet/needs_terminal", FALSE, NULL );
        gconf_client_set_bool( client, "/desktop/gnome/url-handlers/magnet/enabled", TRUE, NULL );
        tr_inf( "Transmission registered as default magnet link handler" );
    }
#endif
}

/***
****
***/

struct counts_data
{
    int    totalCount;
    int    activeCount;
    int    inactiveCount;
};

static void
accumulateStatusForeach( GtkTreeModel *      model,
                         GtkTreePath  * path UNUSED,
                         GtkTreeIter *       iter,
                         gpointer            user_data )
{
    int                  activity = 0;
    struct counts_data * counts = user_data;

    ++counts->totalCount;

    gtk_tree_model_get( model, iter, MC_ACTIVITY, &activity, -1 );

    if( activity == TR_STATUS_STOPPED )
        ++counts->inactiveCount;
    else
        ++counts->activeCount;
}

static void
accumulateCanUpdateForeach( GtkTreeModel *      model,
                            GtkTreePath  * path UNUSED,
                            GtkTreeIter *       iter,
                            gpointer            accumulated_status )
{
    tr_torrent * tor;
    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    *(int*)accumulated_status |= tr_torrentCanManualUpdate( tor );
}

static void
refreshActions( struct cbdata * data )
{
    int canUpdate;
    struct counts_data counts;
    GtkTreeSelection * s = data->sel;

    counts.activeCount = 0;
    counts.inactiveCount = 0;
    counts.totalCount = 0;
    gtk_tree_selection_selected_foreach( s, accumulateStatusForeach, &counts );
    action_sensitize( "pause-torrent", counts.activeCount != 0 );
    action_sensitize( "start-torrent", counts.inactiveCount != 0 );
    action_sensitize( "remove-torrent", counts.totalCount != 0 );
    action_sensitize( "delete-torrent", counts.totalCount != 0 );
    action_sensitize( "verify-torrent", counts.totalCount != 0 );
    action_sensitize( "show-torrent-properties", counts.totalCount != 0 );
    action_sensitize( "open-torrent-folder", counts.totalCount == 1 );
    action_sensitize( "relocate-torrent", counts.totalCount == 1 );
    action_sensitize( "copy-magnet-link-to-clipboard", counts.totalCount == 1 );

    canUpdate = 0;
    gtk_tree_selection_selected_foreach( s, accumulateCanUpdateForeach, &canUpdate );
    action_sensitize( "update-tracker", canUpdate != 0 );

    {
        GtkTreeView *  view = gtk_tree_selection_get_tree_view( s );
        GtkTreeModel * model = gtk_tree_view_get_model( view );
        const int torrentCount = gtk_tree_model_iter_n_children( model, NULL ) != 0;
        action_sensitize( "select-all", torrentCount != 0 );
        action_sensitize( "deselect-all", torrentCount != 0 );
    }

    {
        tr_session * session = tr_core_session( data->core );
        const int active = tr_sessionGetActiveTorrentCount( session );
        const int total = tr_sessionCountTorrents( session );
        action_sensitize( "pause-all-torrents", active != 0 );
        action_sensitize( "start-all-torrents", active != total );
    }
}

static void
selectionChangedCB( GtkTreeSelection * s UNUSED, gpointer data )
{
    refreshActions( data );
}

static void
onMainWindowSizeAllocated( GtkWidget *            window,
                           GtkAllocation  * alloc UNUSED,
                           gpointer         gdata UNUSED )
{
    const gboolean isMaximized = window->window
                            && ( gdk_window_get_state( window->window )
                                 & GDK_WINDOW_STATE_MAXIMIZED );

    pref_int_set( PREF_KEY_MAIN_WINDOW_IS_MAXIMIZED, isMaximized );

    if( !isMaximized )
    {
        int x, y, w, h;
        gtk_window_get_position( GTK_WINDOW( window ), &x, &y );
        gtk_window_get_size( GTK_WINDOW( window ), &w, &h );
        pref_int_set( PREF_KEY_MAIN_WINDOW_X, x );
        pref_int_set( PREF_KEY_MAIN_WINDOW_Y, y );
        pref_int_set( PREF_KEY_MAIN_WINDOW_WIDTH, w );
        pref_int_set( PREF_KEY_MAIN_WINDOW_HEIGHT, h );
    }
}

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
            g_message( _( "Got signal %d; trying to shut down cleanly.  Do it again if it gets stuck." ), sig );
            doAction( "quit", sighandler_cbdata );
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

static tr_rpc_callback_status
onRPCChanged( tr_session            * session UNUSED,
              tr_rpc_callback_type    type,
              struct tr_torrent     * tor,
              void                  * gdata )
{
    struct cbdata * cbdata = gdata;
    gdk_threads_enter( );

    switch( type )
    {
        case TR_RPC_TORRENT_ADDED:
            tr_core_add_torrent( cbdata->core, tr_torrent_new_preexisting( tor ), TRUE );
            break;

        case TR_RPC_TORRENT_REMOVING:
            tr_core_torrent_destroyed( cbdata->core, tr_torrentId( tor ) );
            break;

        case TR_RPC_SESSION_CHANGED:
        case TR_RPC_TORRENT_CHANGED:
        case TR_RPC_TORRENT_MOVED:
        case TR_RPC_TORRENT_STARTED:
        case TR_RPC_TORRENT_STOPPED:
            /* nothing interesting to do here */
            break;
    }

    gdk_threads_leave( );
    return TR_RPC_OK;
}

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
    const char * domain = MY_NAME;
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
        fprintf( stderr, "%s %s\n", g_get_application_name( ), LONG_VERSION_STRING );
        return 0;
    }

    if( configDir == NULL )
        configDir = (char*) tr_getDefaultConfigDir( MY_NAME );

    tr_notify_init( );
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

        for( l = argfiles; l; l = l->next )
            delegated |= gtr_dbus_add_torrent( l->data );

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
            err = g_strdup( _( "Transmission is already running, but is not responding.  To start a new session, you must first close the existing Transmission process." ) );
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
        if(( str = pref_string_get( PREF_KEY_DIR_WATCH )))
            gtr_mkdir_with_parents( str, 0777 );
        if(( str = pref_string_get( TR_PREFS_KEY_DOWNLOAD_DIR )))
            gtr_mkdir_with_parents( str, 0777 );
        if(( str = pref_string_get( TR_PREFS_KEY_INCOMPLETE_DIR )))
            gtr_mkdir_with_parents( str, 0777 );

        /* initialize the libtransmission session */
        session = tr_sessionInit( "gtk", configDir, TRUE, pref_get_all( ) );
        pref_flag_set( TR_PREFS_KEY_ALT_SPEED_ENABLED, tr_sessionUsesAltSpeed( session ) );
        pref_int_set( TR_PREFS_KEY_PEER_PORT, tr_sessionGetPeerPort( session ) );
        cbdata->core = tr_core_new( session );

        /* init the ui manager */
        myUIManager = gtk_ui_manager_new ( );
        actions_init ( myUIManager, cbdata );
        gtk_ui_manager_add_ui_from_string ( myUIManager, fallback_ui_file, -1, NULL );
        gtk_ui_manager_ensure_update ( myUIManager );
        gtk_window_set_default_icon_name ( MY_NAME );

        /* create main window now to be a parent to any error dialogs */
        win = GTK_WINDOW( tr_window_new( myUIManager, cbdata->core ) );
        g_signal_connect( win, "size-allocate", G_CALLBACK( onMainWindowSizeAllocated ), cbdata );

        appsetup( win, argfiles, cbdata, startpaused, startminimized );
        tr_sessionSetRPCCallback( session, onRPCChanged, cbdata );

        /* on startup, check & see if it's time to update the blocklist */
        if( pref_flag_get( PREF_KEY_BLOCKLIST_UPDATES_ENABLED )
            && ( time( NULL ) - pref_int_get( "blocklist-date" ) > ( 60 * 60 * 24 * 7 ) ) )
                tr_core_blocklist_update( cbdata->core );

        /* if there's no magnet link handler registered, register us */
        registerMagnetLinkHandler( );

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
appsetup( TrWindow *      wind,
          GSList *        torrentFiles,
          struct cbdata * cbdata,
          gboolean        forcepause,
          gboolean        isIconified )
{
    const pref_flag_t start =
        forcepause ? PREF_FLAG_FALSE : PREF_FLAG_DEFAULT;
    const pref_flag_t prompt = PREF_FLAG_DEFAULT;

    /* fill out cbdata */
    cbdata->wind         = NULL;
    cbdata->icon         = NULL;
    cbdata->msgwin       = NULL;
    cbdata->prefs        = NULL;
    cbdata->timer        = 0;
    cbdata->isClosing    = 0;
    cbdata->errqueue     = NULL;
    cbdata->dupqueue     = NULL;
    cbdata->isIconified  = isIconified;

    if( isIconified )
        pref_flag_set( PREF_KEY_SHOW_TRAY_ICON, TRUE );

    actions_set_core( cbdata->core );

    /* set up core handlers */
    g_signal_connect( cbdata->core, "error", G_CALLBACK( coreerr ), cbdata );
    g_signal_connect( cbdata->core, "add-torrent-prompt",
                      G_CALLBACK( onAddTorrent ), cbdata );
    g_signal_connect_swapped( cbdata->core, "quit",
                              G_CALLBACK( wannaquit ), cbdata );
    g_signal_connect( cbdata->core, "prefs-changed",
                      G_CALLBACK( prefschanged ), cbdata );

    /* add torrents from command-line and saved state */
    tr_core_load( cbdata->core, forcepause );
    tr_core_add_list( cbdata->core, torrentFiles, start, prompt, TRUE );
    torrentFiles = NULL;
    tr_core_torrents_added( cbdata->core );

    /* set up main window */
    winsetup( cbdata, wind );

    /* set up the icon */
    prefschanged( cbdata->core, PREF_KEY_SHOW_TRAY_ICON, cbdata );

    /* start model update timer */
    cbdata->timer = gtr_timeout_add_seconds( REFRESH_INTERVAL_SECONDS, updatemodel, cbdata );
    updatemodel( cbdata );

    /* either show the window or iconify it */
    if( !isIconified )
        gtk_widget_show( GTK_WIDGET( wind ) );
    else
    {
        gtk_window_set_skip_taskbar_hint( cbdata->wind,
                                          cbdata->icon != NULL );
        cbdata->isIconified = FALSE; // ensure that the next toggle iconifies
        action_toggle( "toggle-main-window", FALSE );
    }

    if( !pref_flag_get( PREF_KEY_USER_HAS_GIVEN_INFORMED_CONSENT ) )
    {
        GtkWidget * w = gtk_message_dialog_new( GTK_WINDOW( wind ),
                                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_INFO,
                                                GTK_BUTTONS_NONE,
                                                "%s",
            _( "Transmission is a file sharing program.  When you run a torrent, its data will be made available to others by means of upload.  And of course, any content you share is your sole responsibility.\n\nYou probably knew this, so we won't tell you again." ) );
        gtk_dialog_add_button( GTK_DIALOG( w ), GTK_STOCK_QUIT, GTK_RESPONSE_REJECT );
        gtk_dialog_add_button( GTK_DIALOG( w ), _( "I _Accept" ), GTK_RESPONSE_ACCEPT );
        gtk_dialog_set_default_response( GTK_DIALOG( w ), GTK_RESPONSE_ACCEPT );
        switch( gtk_dialog_run( GTK_DIALOG( w ) ) ) {
            case GTK_RESPONSE_ACCEPT:
                /* only show it once */
                pref_flag_set( PREF_KEY_USER_HAS_GIVEN_INFORMED_CONSENT, TRUE );
                gtk_widget_destroy( w );
                break;
            default:
                exit( 0 );
        }
    }
}

static void
tr_window_present( GtkWindow * window )
{
#if GTK_CHECK_VERSION( 2, 8, 0 )
    gtk_window_present_with_time( window, gtk_get_current_event_time( ) );
#else
    gtk_window_present( window );
#endif
}

static void
toggleMainWindow( struct cbdata * cbdata,
                  gboolean        doPresent )
{
    GtkWindow * window = GTK_WINDOW( cbdata->wind );
    const int   doShow = cbdata->isIconified;
    static int  x = 0;
    static int  y = 0;

    if( doShow || doPresent )
    {
        cbdata->isIconified = 0;
        gtk_window_set_skip_taskbar_hint( window, FALSE );
        gtk_window_move( window, x, y );
        gtk_widget_show( GTK_WIDGET( window ) );
        tr_window_present( window );
    }
    else
    {
        gtk_window_get_position( window, &x, &y );
        gtk_window_set_skip_taskbar_hint( window, TRUE );
        gtk_widget_hide( GTK_WIDGET( window ) );
        cbdata->isIconified = 1;
    }
}

static gboolean
winclose( GtkWidget * w    UNUSED,
          GdkEvent * event UNUSED,
          gpointer         gdata )
{
    struct cbdata * cbdata = gdata;

    if( cbdata->icon != NULL )
        action_activate ( "toggle-main-window" );
    else
        askquit( cbdata->core, cbdata->wind, wannaquit, cbdata );

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
        refreshActions( gdata );
}

static void
winsetup( struct cbdata * cbdata,
          TrWindow *      wind )
{
    GtkTreeModel *     model;
    GtkTreeSelection * sel;

    g_assert( NULL == cbdata->wind );
    cbdata->wind = GTK_WINDOW( wind );
    cbdata->sel = sel = GTK_TREE_SELECTION( tr_window_get_selection( cbdata->wind ) );

    g_signal_connect( sel, "changed", G_CALLBACK( selectionChangedCB ), cbdata );
    selectionChangedCB( sel, cbdata );
    model = tr_core_model( cbdata->core );
    g_signal_connect( model, "row-changed", G_CALLBACK( rowChangedCB ), cbdata );
    g_signal_connect( wind, "delete-event", G_CALLBACK( winclose ), cbdata );
    refreshActions( cbdata );

    setupdrag( GTK_WIDGET( wind ), cbdata );
}

static gboolean
onSessionClosed( gpointer gdata )
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
    if( cbdata->errqueue ) {
        g_slist_foreach( cbdata->errqueue, (GFunc)g_free, NULL );
        g_slist_free( cbdata->errqueue );
    }
    if( cbdata->dupqueue ) {
        g_slist_foreach( cbdata->dupqueue, (GFunc)g_free, NULL );
        g_slist_free( cbdata->dupqueue );
    }
    g_free( cbdata );

    gtk_main_quit( );

    return FALSE;
}

static gpointer
sessionCloseThreadFunc( gpointer gdata )
{
    /* since tr_sessionClose() is a blocking function,
     * call it from another thread... when it's done,
     * punt the GUI teardown back to the GTK+ thread */
    struct cbdata * cbdata = gdata;
    gdk_threads_enter( );
    tr_core_close( cbdata->core );
    gtr_idle_add( onSessionClosed, gdata );
    gdk_threads_leave( );
    return NULL;
}

static void
do_exit_cb( GtkWidget *w  UNUSED,
            gpointer data UNUSED )
{
    exit( 0 );
}

static void
wannaquit( gpointer vdata )
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
    w = gtr_button_new_from_stock( GTK_STOCK_QUIT, _( "_Quit Now" ) );
    g_signal_connect( w, "clicked", G_CALLBACK( do_exit_cb ), NULL );
    gtk_container_add( GTK_CONTAINER( b ), w );
    gtk_table_attach( GTK_TABLE( p ), b, 1, 2, 2, 3, GTK_FILL, GTK_FILL, 0, 10 );

    gtk_widget_show_all( r );
    gtk_widget_grab_focus( w );

    /* clear the UI */
    gtk_list_store_clear( GTK_LIST_STORE( tr_core_model( cbdata->core ) ) );

    /* ensure the window is in its previous position & size.
     * this seems to be necessary because changing the main window's
     * child seems to unset the size */
    gtk_window_resize( cbdata->wind, pref_int_get( PREF_KEY_MAIN_WINDOW_WIDTH ),
                                     pref_int_get( PREF_KEY_MAIN_WINDOW_HEIGHT ) );
    gtk_window_move( cbdata->wind, pref_int_get( PREF_KEY_MAIN_WINDOW_X ),
                                   pref_int_get( PREF_KEY_MAIN_WINDOW_Y ) );

    /* shut down libT */
    g_thread_create( sessionCloseThreadFunc, vdata, TRUE, NULL );
}

static void
gotdrag( GtkWidget         * widget UNUSED,
         GdkDragContext *           dc,
         gint                x      UNUSED,
         gint                y      UNUSED,
         GtkSelectionData *         sel,
         guint               info   UNUSED,
         guint                      time,
         gpointer                   gdata )
{
    struct cbdata * data = gdata;
    GSList *        paths = NULL;
    GSList *        freeme = NULL;

#if 0
    int             i;
    char *          sele = gdk_atom_name( sel->selection );
    char *          targ = gdk_atom_name( sel->target );
    char *          type = gdk_atom_name( sel->type );

    g_message( "dropped file: sel=%s targ=%s type=%s fmt=%i len=%i",
               sele, targ, type, sel->format, sel->length );
    g_free( sele );
    g_free( targ );
    g_free( type );
    if( sel->format == 8 )
    {
        for( i = 0; i < sel->length; ++i )
            fprintf( stderr, "%02X ", sel->data[i] );
        fprintf( stderr, "\n" );
    }
#endif

    if( ( sel->format == 8 )
      && ( sel->selection == gdk_atom_intern( "XdndSelection", FALSE ) ) )
    {
        int      i;
        char *   str = g_strndup( (char*)sel->data, sel->length );
        gchar ** files = g_strsplit_set( str, "\r\n", -1 );
        for( i = 0; files && files[i]; ++i )
        {
            char * filename;
            if( !*files[i] ) /* empty filename... */
                continue;

            /* decode the filename */
            filename = decode_uri( files[i] );
            freeme = g_slist_prepend( freeme, filename );
            if( !g_utf8_validate( filename, -1, NULL ) )
                continue;

            /* walk past "file://", if present */
            if( g_str_has_prefix( filename, "file:" ) ) {
                filename += 5;
                while( g_str_has_prefix( filename, "//" ) )
                    ++filename;
            }

            g_debug( "got from drag: [%s]", filename );

            if( g_file_test( filename, G_FILE_TEST_EXISTS ) )
                paths = g_slist_prepend( paths, g_strdup( filename ) );
            else
                tr_core_add_from_url( data->core, filename );
        }

        /* try to add any torrents we found */
        if( paths )
        {
            paths = g_slist_reverse( paths );
            tr_core_add_list_defaults( data->core, paths, TRUE );
            tr_core_torrents_added( data->core );
        }

        freestrlist( freeme );
        g_strfreev( files );
        g_free( str );
    }

    gtk_drag_finish( dc, ( NULL != paths ), FALSE, time );
}

static void
setupdrag( GtkWidget *    widget,
           struct cbdata *data )
{
    GtkTargetEntry targets[] = {
        { (char*)"STRING",          0, 0 },
        { (char*)"text/plain",      0, 0 },
        { (char*)"text/uri-list",   0, 0 },
    };

    g_signal_connect( widget, "drag_data_received", G_CALLBACK(
                          gotdrag ), data );

    gtk_drag_dest_set( widget, GTK_DEST_DEFAULT_ALL, targets,
                       G_N_ELEMENTS( targets ), GDK_ACTION_COPY | GDK_ACTION_MOVE );
}

static void
flushAddTorrentErrors( GtkWindow *  window,
                       const char * primary,
                       GSList **    files )
{
    GString *   s = g_string_new( NULL );
    GSList *    l;
    GtkWidget * w;

    if( g_slist_length( *files ) > 1 ) {
        for( l=*files; l!=NULL; l=l->next )
            g_string_append_printf( s, "\xE2\x88\x99 %s\n", (const char*)l->data );
    } else {
        for( l=*files; l!=NULL; l=l->next )
            g_string_append_printf( s, "%s\n", (const char*)l->data );
    }
    w = gtk_message_dialog_new( window,
                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_CLOSE,
                                "%s", primary );
    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( w ),
                                              "%s", s->str );
    g_signal_connect_swapped( w, "response",
                              G_CALLBACK( gtk_widget_destroy ), w );
    gtk_widget_show_all( w );
    g_string_free( s, TRUE );

    g_slist_foreach( *files, (GFunc)g_free, NULL );
    g_slist_free( *files );
    *files = NULL;
}

static void
showTorrentErrors( struct cbdata * cbdata )
{
    if( cbdata->errqueue )
        flushAddTorrentErrors( GTK_WINDOW( cbdata->wind ),
                               ngettext( "Couldn't add corrupt torrent",
                                         "Couldn't add corrupt torrents",
                                         g_slist_length( cbdata->errqueue ) ),
                               &cbdata->errqueue );

    if( cbdata->dupqueue )
        flushAddTorrentErrors( GTK_WINDOW( cbdata->wind ),
                               ngettext( "Couldn't add duplicate torrent",
                                         "Couldn't add duplicate torrents",
                                         g_slist_length( cbdata->dupqueue ) ),
                               &cbdata->dupqueue );
}

static void
coreerr( TrCore * core UNUSED, guint code, const char * msg, struct cbdata * c )
{
    switch( code )
    {
        case TR_PARSE_ERR:
            c->errqueue =
                g_slist_append( c->errqueue, g_path_get_basename( msg ) );
            break;

        case TR_PARSE_DUPLICATE:
            c->dupqueue = g_slist_append( c->dupqueue, g_strdup( msg ) );
            break;

        case TR_CORE_ERR_NO_MORE_TORRENTS:
            showTorrentErrors( c );
            break;

        default:
            g_assert_not_reached( );
            break;
    }
}

#if GTK_CHECK_VERSION( 2, 8, 0 )
static void
on_main_window_focus_in( GtkWidget      * widget UNUSED,
                         GdkEventFocus  * event  UNUSED,
                         gpointer                gdata )
{
    struct cbdata * cbdata = gdata;

    if( cbdata->wind )
        gtk_window_set_urgency_hint( GTK_WINDOW( cbdata->wind ), FALSE );
}

#endif

static void
onAddTorrent( TrCore *  core,
              tr_ctor * ctor,
              gpointer  gdata )
{
    struct cbdata * cbdata = gdata;
    GtkWidget *     w = addSingleTorrentDialog( cbdata->wind, core, ctor );

#if GTK_CHECK_VERSION( 2, 8, 0 )
    g_signal_connect( w, "focus-in-event",
                      G_CALLBACK( on_main_window_focus_in ),  cbdata );
    if( cbdata->wind )
        gtk_window_set_urgency_hint( cbdata->wind, TRUE );
#endif
}

static void
prefschanged( TrCore * core UNUSED,
              const char *  key,
              gpointer      data )
{
    struct cbdata  * cbdata = data;
    tr_session     * tr     = tr_core_session( cbdata->core );

    if( !strcmp( key, TR_PREFS_KEY_ENCRYPTION ) )
    {
        tr_sessionSetEncryption( tr, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_DOWNLOAD_DIR ) )
    {
        tr_sessionSetDownloadDir( tr, pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_MSGLEVEL ) )
    {
        tr_setMessageLevel( pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_PORT ) )
    {
        tr_sessionSetPeerPort( tr, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_BLOCKLIST_ENABLED ) )
    {
        tr_blocklistSetEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, PREF_KEY_SHOW_TRAY_ICON ) )
    {
        const int show = pref_flag_get( key );
        if( show && !cbdata->icon )
            cbdata->icon = tr_icon_new( cbdata->core );
        else if( !show && cbdata->icon ) {
            g_object_unref( cbdata->icon );
            cbdata->icon = NULL;
        }
    }
    else if( !strcmp( key, TR_PREFS_KEY_DSPEED_ENABLED ) )
    {
        tr_sessionLimitSpeed( tr, TR_DOWN, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_DSPEED ) )
    {
        tr_sessionSetSpeedLimit( tr, TR_DOWN, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_USPEED_ENABLED ) )
    {
        tr_sessionLimitSpeed( tr, TR_UP, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_USPEED ) )
    {
        tr_sessionSetSpeedLimit( tr, TR_UP, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RATIO_ENABLED ) )
    {
        tr_sessionSetRatioLimited( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RATIO ) )
    {
        tr_sessionSetRatioLimit( tr, pref_double_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PORT_FORWARDING ) )
    {
        tr_sessionSetPortForwardingEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEX_ENABLED ) )
    {
        tr_sessionSetPexEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_DHT_ENABLED ) )
    {
        tr_sessionSetDHTEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_PORT ) )
    {
        tr_sessionSetRPCPort( tr, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_ENABLED ) )
    {
        tr_sessionSetRPCEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_WHITELIST ) )
    {
        tr_sessionSetRPCWhitelist( tr, pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_WHITELIST_ENABLED ) )
    {
        tr_sessionSetRPCWhitelistEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_USERNAME ) )
    {
        tr_sessionSetRPCUsername( tr, pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_PASSWORD ) )
    {
        tr_sessionSetRPCPassword( tr, pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_RPC_AUTH_REQUIRED ) )
    {
        tr_sessionSetRPCPasswordEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY ) )
    {
        tr_sessionSetProxy( tr, pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_TYPE ) )
    {
        tr_sessionSetProxyType( tr, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_ENABLED ) )
    {
        tr_sessionSetProxyEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_AUTH_ENABLED ) )
    {
        tr_sessionSetProxyAuthEnabled( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_USERNAME ) )
    {
        tr_sessionSetProxyUsername( tr, pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_PASSWORD ) )
    {
        tr_sessionSetProxyPassword( tr, pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PROXY_PORT ) )
    {
        tr_sessionSetProxyPort( tr, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_UP ) )
    {
        tr_sessionSetAltSpeed( tr, TR_UP, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_DOWN ) )
    {
        tr_sessionSetAltSpeed( tr, TR_DOWN, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_ENABLED ) )
    {
        const gboolean b = pref_flag_get( key );
        tr_sessionUseAltSpeed( tr, b );
        action_toggle( key, b );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN ) )
    {
        tr_sessionSetAltSpeedBegin( tr, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_TIME_END ) )
    {
        tr_sessionSetAltSpeedEnd( tr, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED ) )
    {
        tr_sessionUseAltSpeedTime( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_ALT_SPEED_TIME_DAY ) )
    {
        tr_sessionSetAltSpeedDay( tr, pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START ) )
    {
        tr_sessionSetPeerPortRandomOnStart( tr, pref_flag_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_INCOMPLETE_DIR ) )
    {
        tr_sessionSetIncompleteDir( tr, pref_string_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED ) )
    {
        tr_sessionSetIncompleteDirEnabled( tr, pref_flag_get( key ) );
    }
}

static gboolean
updatemodel( gpointer gdata )
{
    struct cbdata *data = gdata;
    const gboolean done = data->isClosing || global_sigcount;

    if( !done )
    {
        /* update the torrent data in the model */
        tr_core_update( data->core );

        /* update the main window's statusbar and toolbar buttons */
        if( data->wind != NULL )
            tr_window_update( data->wind );

        /* update the actions */
        refreshActions( data );

        /* update the status tray icon */
        if( data->icon != NULL )
            tr_icon_refresh( data->icon );
    }

    return !done;
}

static void
aboutDialogActivateLink( GtkAboutDialog * dialog    UNUSED,
                         const gchar *              link_,
                         gpointer         user_data UNUSED )
{
    gtr_open_file( link_ );
}

static void
about( GtkWindow * parent )
{
    const char *authors[] =
    {
        "Charles Kerr (Backend; GTK+)",
        "Mitchell Livingston (Backend; OS X)",
        "Kevin Glowacz (Web client)",
        NULL
    };

    const char *website_url = "http://www.transmissionbt.com/";

    gtk_about_dialog_set_url_hook( aboutDialogActivateLink, NULL, NULL );

    gtk_show_about_dialog( parent,
                           "name", g_get_application_name( ),
                           "comments",
                           _( "A fast and easy BitTorrent client" ),
                           "version", LONG_VERSION_STRING,
                           "website", website_url,
                           "website-label", website_url,
                           "copyright",
                           _( "Copyright 2005-2009 The Transmission Project" ),
                           "logo-icon-name", MY_NAME,
#ifdef SHOW_LICENSE
                           "license", LICENSE,
                           "wrap-license", TRUE,
#endif
                           "authors", authors,
                           /* Translators: translate "translator-credits" as
                              your name
                              to have it appear in the credits in the "About"
                              dialog */
                           "translator-credits", _( "translator-credits" ),
                           NULL );
}

static void
startTorrentForeach( GtkTreeModel *      model,
                     GtkTreePath  * path UNUSED,
                     GtkTreeIter *       iter,
                     gpointer       data UNUSED )
{
    tr_torrent * tor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    tr_torrentStart( tor );
}

static void
stopTorrentForeach( GtkTreeModel *      model,
                    GtkTreePath  * path UNUSED,
                    GtkTreeIter *       iter,
                    gpointer       data UNUSED )
{
    tr_torrent * tor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    tr_torrentStop( tor );
}

static void
updateTrackerForeach( GtkTreeModel *      model,
                      GtkTreePath  * path UNUSED,
                      GtkTreeIter *       iter,
                      gpointer       data UNUSED )
{
    tr_torrent * tor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    tr_torrentManualUpdate( tor );
}

static void
openFolderForeach( GtkTreeModel *           model,
                   GtkTreePath  * path      UNUSED,
                   GtkTreeIter *            iter,
                   gpointer       user_data UNUSED )
{
    TrTorrent * gtor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT, &gtor, -1 );
    tr_torrent_open_folder( gtor );
    g_object_unref( G_OBJECT( gtor ) );
}

static void
recheckTorrentForeach( GtkTreeModel *      model,
                       GtkTreePath  * path UNUSED,
                       GtkTreeIter *       iter,
                       gpointer       data UNUSED )
{
    TrTorrent * gtor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT, &gtor, -1 );
    tr_torrentVerify( tr_torrent_handle( gtor ) );
    g_object_unref( G_OBJECT( gtor ) );
}

static gboolean
msgwinclosed( void )
{
    action_toggle( "toggle-message-log", FALSE );
    return FALSE;
}

static void
accumulateSelectedTorrents( GtkTreeModel *      model,
                            GtkTreePath  * path UNUSED,
                            GtkTreeIter *       iter,
                            gpointer            gdata )
{
    GSList **   data = ( GSList** ) gdata;
    TrTorrent * tor = NULL;

    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    *data = g_slist_prepend( *data, tor );
}

static void
removeSelected( struct cbdata * data,
                gboolean        delete_files )
{
    GSList *           l = NULL;
    GtkTreeSelection * s = tr_window_get_selection( data->wind );

    gtk_tree_selection_selected_foreach( s, accumulateSelectedTorrents, &l );
    gtk_tree_selection_unselect_all( s );
    if( l )
    {
        l = g_slist_reverse( l );
        confirmRemove( data->wind, data->core, l, delete_files );
    }
}

static void
startAllTorrents( struct cbdata * data )
{
    tr_session * session = tr_core_session( data->core );
    const char * cmd = "{ \"method\": \"torrent-start\" }";
    tr_rpc_request_exec_json( session, cmd, strlen( cmd ), NULL, NULL );
}

static void
pauseAllTorrents( struct cbdata * data )
{
    tr_session * session = tr_core_session( data->core );
    const char * cmd = "{ \"method\": \"torrent-stop\" }";
    tr_rpc_request_exec_json( session, cmd, strlen( cmd ), NULL, NULL );
}

static tr_torrent*
getFirstSelectedTorrent( struct cbdata * data )
{
    tr_torrent * tor = NULL;
    GtkTreeSelection * s = tr_window_get_selection( data->wind );
    GtkTreeModel * m;
    GList * l = gtk_tree_selection_get_selected_rows( s, &m );
    if( l != NULL ) {
        GtkTreePath * p = l->data;
        GtkTreeIter i;
        if( gtk_tree_model_get_iter( m, &i, p ) )
            gtk_tree_model_get( m, &i, MC_TORRENT_RAW, &tor, -1 );
    }
    g_list_foreach( l, (GFunc)gtk_tree_path_free, NULL );
    g_list_free( l );
    return tor;
}

static void
detailsClosed( gpointer gdata, GObject * dead )
{
    struct cbdata * data = gdata;
    struct DetailsDialogHandle * h = findDetailsDialogFromWidget( data, dead );

    if( h != NULL )
    {
        data->details = g_slist_remove( data->details, h );
        g_free( h->key );
        g_free( h );
    }
}

static void
copyMagnetLinkToClipboard( GtkWidget * w, tr_torrent * tor )
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
doAction( const char * action_name, gpointer user_data )
{
    struct cbdata * data = user_data;
    gboolean        changed = FALSE;

    if( !strcmp( action_name, "add-torrent-from-url" ) )
    {
        addURLDialog( data->wind, data->core );
    }
    else if(  !strcmp( action_name, "add-torrent-menu" )
      || !strcmp( action_name, "add-torrent-toolbar" ) )
    {
        addDialog( data->wind, data->core );
    }
    else if( !strcmp( action_name, "show-stats" ) )
    {
        GtkWidget * dialog = stats_dialog_create( data->wind, data->core );
        gtk_widget_show( dialog );
    }
    else if( !strcmp( action_name, "donate" ) )
    {
        gtr_open_file( "http://www.transmissionbt.com/donate.php" );
    }
    else if( !strcmp( action_name, "start-torrent" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, startTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if( !strcmp( action_name, "pause-all-torrents" ) )
    {
        pauseAllTorrents( data );
    }
    else if( !strcmp( action_name, "start-all-torrents" ) )
    {
        startAllTorrents( data );
    }
    else if( !strcmp( action_name, "copy-magnet-link-to-clipboard" ) )
    {
        tr_torrent * tor = getFirstSelectedTorrent( data );
        if( tor != NULL )
        {
            copyMagnetLinkToClipboard( GTK_WIDGET( data->wind ), tor );
        }
    }
    else if( !strcmp( action_name, "relocate-torrent" ) )
    {
        tr_torrent * tor = getFirstSelectedTorrent( data );
        if( tor != NULL )
        {
            GtkWindow * parent = GTK_WINDOW( data->wind );
            GtkWidget * w = gtr_relocate_dialog_new( parent, tor );
            gtk_widget_show( w );
        }
    }
    else if( !strcmp( action_name, "pause-torrent" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, stopTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if( !strcmp( action_name, "verify-torrent" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, recheckTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if( !strcmp( action_name, "open-torrent-folder" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, openFolderForeach, data );
    }
    else if( !strcmp( action_name, "show-torrent-properties" ) )
    {
        GtkWidget * w;
        GSList * ids = getSelectedTorrentIds( data );
        struct DetailsDialogHandle * h = findDetailsDialogFromIds( data, ids );
        if( h != NULL )
            w = h->dialog;
        else {
            h = g_new( struct DetailsDialogHandle, 1 );
            h->key = getDetailsDialogKey( ids );
            h->dialog = w = torrent_inspector_new( GTK_WINDOW( data->wind ), data->core );
            torrent_inspector_set_torrents( w, ids );
            data->details = g_slist_append( data->details, h );
            g_object_weak_ref( G_OBJECT( w ), detailsClosed, data );
        }
        gtk_window_present( GTK_WINDOW( w ) );
        g_slist_free( ids );
    }
    else if( !strcmp( action_name, "update-tracker" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_selected_foreach( s, updateTrackerForeach,
                                             data->wind );
    }
    else if( !strcmp( action_name, "new-torrent" ) )
    {
        GtkWidget * w = make_meta_ui( GTK_WINDOW( data->wind ), data->core );
        gtk_widget_show_all( w );
    }
    else if( !strcmp( action_name, "remove-torrent" ) )
    {
        removeSelected( data, FALSE );
    }
    else if( !strcmp( action_name, "delete-torrent" ) )
    {
        removeSelected( data, TRUE );
    }
    else if( !strcmp( action_name, "quit" ) )
    {
        askquit( data->core, data->wind, wannaquit, data );
    }
    else if( !strcmp( action_name, "select-all" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_select_all( s );
    }
    else if( !strcmp( action_name, "deselect-all" ) )
    {
        GtkTreeSelection * s = tr_window_get_selection( data->wind );
        gtk_tree_selection_unselect_all( s );
    }
    else if( !strcmp( action_name, "edit-preferences" ) )
    {
        if( NULL == data->prefs )
        {
            data->prefs = tr_prefs_dialog_new( G_OBJECT( data->core ),
                                               data->wind );
            g_signal_connect( data->prefs, "destroy",
                              G_CALLBACK( gtk_widget_destroyed ), &data->prefs );
            gtk_widget_show( GTK_WIDGET( data->prefs ) );
        }
    }
    else if( !strcmp( action_name, "toggle-message-log" ) )
    {
        if( !data->msgwin )
        {
            GtkWidget * win = msgwin_new( data->core );
            g_signal_connect( win, "destroy", G_CALLBACK( msgwinclosed ),
                              NULL );
            data->msgwin = win;
        }
        else
        {
            action_toggle( "toggle-message-log", FALSE );
            gtk_widget_destroy( data->msgwin );
            data->msgwin = NULL;
        }
    }
    else if( !strcmp( action_name, "show-about-dialog" ) )
    {
        about( data->wind );
    }
    else if( !strcmp ( action_name, "help" ) )
    {
        char * url = gtr_get_help_url( );
        gtr_open_file( url );
        g_free( url );
    }
    else if( !strcmp( action_name, "toggle-main-window" ) )
    {
        toggleMainWindow( data, FALSE );
    }
    else if( !strcmp( action_name, "present-main-window" ) )
    {
        toggleMainWindow( data, TRUE );
    }
    else g_error ( "Unhandled action: %s", action_name );

    if( changed )
        updatemodel( data );
}
