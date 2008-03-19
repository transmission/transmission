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

#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#endif

#include "actions.h"
#include "conf.h"
#include "details.h"
#include "dialogs.h"
#include "hig.h"
#include "ipc.h"
#include "makemeta-ui.h"
#include "msgwin.h"
#include "notify.h"
#include "open-dialog.h"
#include "stats.h"
#include "tr-core.h"
#include "tr-icon.h"
#include "tr-prefs.h"
#include "tr-torrent.h"
#include "tr-window.h"
#include "util.h"
#include "ui.h"

#include <libtransmission/transmission.h>
#include <libtransmission/version.h>

/* interval in milliseconds to update the torrent list display */
#define UPDATE_INTERVAL         1666

/* interval in milliseconds to check for stopped torrents and update display */
#define EXIT_CHECK_INTERVAL     500

#if GTK_CHECK_VERSION(2,8,0)
#define SHOW_LICENSE
static const char * LICENSE = 
"The Transmission binaries and most of its source code is distributed "
"license. "
"\n\n"
"Some files are copyrighted by Charles Kerr and are covered by "
"the GPL version 2.  Works owned by the Transmission project "
"are granted a special exemption to clause 2(b) so that the bulk "
"of its code can remain under the MIT license.  This exemption does "
"not extend to original or derived works not owned by the "
"Transmission project. "
"\n\n"
"Permission is hereby granted, free of charge, to any person obtaining "
"a copy of this software and associated documentation files (the "
"'Software'), to deal in the Software without restriction, including "
"without limitation the rights to use, copy, modify, merge, publish, "
"distribute, sublicense, and/or sell copies of the Software, and to "
"permit persons to whom the Software is furnished to do so, subject to "
"the following conditions: "
"\n\n"
"The above copyright notice and this permission notice shall be included "
"in all copies or substantial portions of the Software. "
"\n\n"
"THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, "
"EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
"MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. "
"IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY "
"CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, "
"TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE "
"SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";
#endif

struct cbdata
{
    gboolean       minimized;
    gboolean       closing;
    guint          timer;
    guint          idle_hide_mainwindow_tag;
    gpointer       icon;
    GtkWindow    * wind;
    TrCore       * core;
    GtkWidget    * msgwin;
    GtkWidget    * prefs;
    GSList       * errqueue;
    GHashTable   * tor2details;
    GHashTable   * details2tor;
};

#define CBDATA_PTR "callback-data-pointer"

static GtkUIManager * myUIManager = NULL;

static gboolean
sendremote( GSList * files, gboolean sendquit );
static void
appsetup( TrWindow * wind, GSList * args,
          struct cbdata *,
          gboolean paused, gboolean minimized );
static void
winsetup( struct cbdata * cbdata, TrWindow * wind );
static void
wannaquit( void * vdata );
static void
setupdrag(GtkWidget *widget, struct cbdata *data);
static void
gotdrag(GtkWidget *widget, GdkDragContext *dc, gint x, gint y,
        GtkSelectionData *sel, guint info, guint time, gpointer gdata);

static void
coreerr( TrCore * core, enum tr_core_err code, const char * msg,
         gpointer gdata );
static void
onAddTorrent( TrCore *, tr_ctor *, gpointer );
static void
prefschanged( TrCore * core, const char * key, gpointer data );
static gboolean
updatemodel(gpointer gdata);

struct counts_data
{
    int totalCount;
    int activeCount;
    int inactiveCount;
};

static void
accumulateStatusForeach( GtkTreeModel * model,
                         GtkTreePath  * path UNUSED,
                         GtkTreeIter  * iter,
                         gpointer       user_data )
{
    int status = 0;
    struct counts_data * counts = user_data;

    ++counts->totalCount;

    gtk_tree_model_get( model, iter, MC_STATUS, &status, -1 );

    if( TR_STATUS_IS_ACTIVE( status ) )
        ++counts->activeCount;
    else
        ++counts->inactiveCount;
}

static void
accumulateCanUpdateForeach (GtkTreeModel * model,
                            GtkTreePath  * path UNUSED,
                            GtkTreeIter  * iter,
                            gpointer       accumulated_status)
{
    tr_torrent * tor;
    gtk_tree_model_get( model, iter, MC_TORRENT_RAW, &tor, -1 );
    *(int*)accumulated_status |= tr_torrentCanManualUpdate( tor );
}

static void
refreshTorrentActions( GtkTreeSelection * s )
{
    int canUpdate;
    struct counts_data counts;

    counts.activeCount = 0;
    counts.inactiveCount = 0;
    counts.totalCount = 0;
    gtk_tree_selection_selected_foreach( s, accumulateStatusForeach, &counts );
    action_sensitize( "pause-torrent", counts.activeCount!=0 );
    action_sensitize( "start-torrent", counts.inactiveCount!=0 );
    action_sensitize( "remove-torrent", counts.totalCount!=0 );
    action_sensitize( "delete-torrent", counts.totalCount!=0 );
    action_sensitize( "verify-torrent", counts.totalCount!=0 );
    action_sensitize( "show-torrent-details", counts.totalCount==1 );

    canUpdate = 0;
    gtk_tree_selection_selected_foreach( s, accumulateCanUpdateForeach, &canUpdate );
    action_sensitize( "update-tracker", canUpdate!=0 );

    {
        GtkTreeView * view = gtk_tree_selection_get_tree_view( s );
        GtkTreeModel * model = gtk_tree_view_get_model( view );
        const int torrentCount = gtk_tree_model_iter_n_children( model, NULL ) != 0;
        action_sensitize( "select-all", torrentCount!=0 );
        action_sensitize( "deselect-all", torrentCount!=0 );
    }
}

static void
selectionChangedCB( GtkTreeSelection * s, gpointer unused UNUSED )
{
    refreshTorrentActions( s );
}

static void
windowStateChanged( GtkWidget * widget UNUSED, GdkEventWindowState * event, gpointer gdata )
{
    if( event->changed_mask & GDK_WINDOW_STATE_ICONIFIED )
    {
        struct cbdata * cbdata = gdata;
        cbdata->minimized = ( event->new_window_state & GDK_WINDOW_STATE_ICONIFIED ) ? 1 : 0;
    }
}

static sig_atomic_t global_sigcount = 0;

static void
fatalsig( int sig )
{
    static const int SIGCOUNT_MAX = 3; /* revert to default handler after this many */

    if( ++global_sigcount >= SIGCOUNT_MAX )
    {
        signal( sig, SIG_DFL );
        raise( sig );
    }
}


static void
setupsighandlers( void )
{
#ifdef G_OS_WIN32
  const int sigs[] = { SIGINT, SIGTERM };
#else
  const int sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM };
#endif
  guint i;

  for( i=0; i<G_N_ELEMENTS(sigs); ++i )
      signal( sigs[i], fatalsig );
}

int
main( int argc, char ** argv )
{
    char * err;
    struct cbdata * cbdata;
    GSList * argfiles;
    GError * gerr;
    gboolean didinit = FALSE;
    gboolean didlock = FALSE;
    gboolean sendquit = FALSE;
    gboolean startpaused = FALSE;
    gboolean startminimized = FALSE;
    char * domain = "transmission";
    GOptionEntry entries[] = {
        { "paused", 'p', 0, G_OPTION_ARG_NONE, &startpaused,
          _("Start with all torrents paused"), NULL },
        { "quit", 'q', 0, G_OPTION_ARG_NONE, &sendquit,
          _( "Ask the running instance to quit"), NULL },
#ifdef STATUS_ICON_SUPPORTED
        { "minimized", 'm', 0, G_OPTION_ARG_NONE, &startminimized,
          _( "Start minimized in system tray"), NULL },
#endif
        { NULL, 0, 0, 0, NULL, NULL, NULL }
    };

    cbdata = g_new0( struct cbdata, 1 );
    cbdata->tor2details = g_hash_table_new( g_str_hash, g_str_equal );
    cbdata->details2tor = g_hash_table_new( g_direct_hash, g_direct_equal );

    /* bind the gettext domain */
    bindtextdomain( domain, TRANSMISSIONLOCALEDIR );
    bind_textdomain_codeset( domain, "UTF-8" );
    textdomain( domain );
    g_set_application_name( _( "Transmission" ) );

    /* initialize gtk */
    g_thread_init( NULL );
    gerr = NULL;
    if( !gtk_init_with_args( &argc, &argv, _("[torrent files]"), entries, domain, &gerr ) ) {
        g_message( "%s", gerr->message );
        g_clear_error( &gerr );
        return 0;
    }

    tr_notify_init( );

    didinit = cf_init( tr_getPrefsDirectory(), NULL ); /* must come before actions_init */
    tr_prefs_init_global( );
    myUIManager = gtk_ui_manager_new ();
    actions_init ( myUIManager, cbdata );
    gtk_ui_manager_add_ui_from_string (myUIManager, fallback_ui_file, -1, NULL);
    gtk_ui_manager_ensure_update (myUIManager);
    gtk_window_set_default_icon_name ( "transmission" );

    argfiles = checkfilenames( argc-1, argv+1 );
    didlock = didinit && sendremote( argfiles, sendquit );
    setupsighandlers( ); /* set up handlers for fatal signals */

    if( ( didinit || cf_init( tr_getPrefsDirectory(), &err ) ) &&
        ( didlock || cf_lock( &err ) ) )
    {
        cbdata->core = tr_core_new( );

        /* create main window now to be a parent to any error dialogs */
        GtkWindow * mainwind = GTK_WINDOW( tr_window_new( myUIManager, cbdata->core ) );
        g_signal_connect( mainwind, "window-state-event", G_CALLBACK(windowStateChanged), cbdata );

        appsetup( mainwind, argfiles, cbdata, startpaused, startminimized );
    }
    else
    {
        gtk_widget_show( errmsg_full( NULL, (callbackfunc_t)gtk_main_quit,
                                      NULL, "%s", err ) );
        g_free( err );
    }

    freestrlist(argfiles);

    gtk_main();

    return 0;
}

static gboolean
sendremote( GSList * files, gboolean sendquit )
{
    const gboolean didlock = cf_lock( NULL );

    /* send files if there's another instance, otherwise start normally */
    if( !didlock && files )
        exit( ipc_sendfiles_blocking( files ) ? 0 : 1 );

    /* either send a quit message or exit if no other instance */
    if( sendquit )
        exit( didlock ? 0 : !ipc_sendquit_blocking() );

    return didlock;
}

static void
appsetup( TrWindow * wind, GSList * torrentFiles,
          struct cbdata * cbdata,
          gboolean forcepause, gboolean minimized )
{
    const pref_flag_t start = forcepause ? PREF_FLAG_FALSE : PREF_FLAG_DEFAULT;
    const pref_flag_t prompt = PREF_FLAG_DEFAULT;

    /* fill out cbdata */
    cbdata->wind       = NULL;
    cbdata->icon       = NULL;
    cbdata->msgwin     = NULL;
    cbdata->prefs      = NULL;
    cbdata->timer      = 0;
    cbdata->closing    = FALSE;
    cbdata->errqueue   = NULL;
    cbdata->minimized  = minimized;

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
    tr_core_add_list( cbdata->core, torrentFiles, start, prompt );
    torrentFiles = NULL;
    tr_core_torrents_added( cbdata->core );

    /* set up the ipc socket */
    ipc_socket_setup( GTK_WINDOW( wind ), cbdata->core );

    /* set up main window */
    winsetup( cbdata, wind );

    /* set up the system tray */
    cbdata->icon = tr_icon_new( cbdata->core );

    /* start model update timer */
    cbdata->timer = g_timeout_add( UPDATE_INTERVAL, updatemodel, cbdata );
    updatemodel( cbdata );

    /* show the window */
    if( minimized ) {
        gtk_window_iconify( wind );
        gtk_window_set_skip_taskbar_hint( cbdata->wind, cbdata->icon != NULL );
    }
    gtk_widget_show( GTK_WIDGET( wind ) );
}


/**
 * hideMainWindow, and the timeout hack in toggleMainWindow,
 * are loosely cribbed from Colin Walters' tr-shell.c in Rhythmbox
 */
static gboolean
idle_hide_mainwindow( gpointer window )
{
    gtk_widget_hide( window );
    return FALSE;
}
static void
hideMainWindow( struct cbdata * cbdata )
{
#if defined(STATUS_ICON_SUPPORTED) && defined(GDK_WINDOWING_X11)
    GdkRectangle  bounds;
    gulong        data[4];
    Display      *dpy;
    GdkWindow    *gdk_window;

    gtk_status_icon_get_geometry( GTK_STATUS_ICON( cbdata->icon ), NULL, &bounds, NULL );
    gdk_window = GTK_WIDGET (cbdata->wind)->window;
    dpy = gdk_x11_drawable_get_xdisplay (gdk_window);

    data[0] = bounds.x;
    data[1] = bounds.y;
    data[2] = bounds.width;
    data[3] = bounds.height;

    XChangeProperty (dpy,
                     GDK_WINDOW_XID (gdk_window),
                     gdk_x11_get_xatom_by_name_for_display (gdk_drawable_get_display (gdk_window),
                     "_NET_WM_ICON_GEOMETRY"),
                     XA_CARDINAL, 32, PropModeReplace,
                     (guchar*)&data, 4);

    gtk_window_set_skip_taskbar_hint( cbdata->wind, TRUE );
#endif
    gtk_window_iconify( cbdata->wind );
}

static void
clearTag( guint * tag )
{
    if( *tag )
        g_source_remove( *tag );
    *tag = 0;
}

static void
toggleMainWindow( struct cbdata * cbdata )
{
    GtkWindow * window = GTK_WINDOW( cbdata->wind );
    const int hide = !cbdata->minimized;
    static int x=0, y=0;

    if( hide )
    {
        gtk_window_get_position( window, &x, &y );
        clearTag( &cbdata->idle_hide_mainwindow_tag );
        hideMainWindow( cbdata );
        cbdata->idle_hide_mainwindow_tag = g_timeout_add( 250, idle_hide_mainwindow, window );
    }
    else
    {
        gtk_window_set_skip_taskbar_hint( window, FALSE );
        gtk_window_move( window, x, y );
        gtk_widget_show( GTK_WIDGET( window ) );
        gtk_window_deiconify( window );
#if GTK_CHECK_VERSION(2,8,0)
        gtk_window_present_with_time( window, gtk_get_current_event_time( ) );
#else
        gtk_window_present( window );
#endif
    }
}

static gboolean
winclose( GtkWidget * w UNUSED, GdkEvent * event UNUSED, gpointer gdata )
{
    struct cbdata * cbdata = gdata;

    if( cbdata->icon != NULL )
        action_activate ("toggle-main-window");
    else
        askquit( cbdata->core, cbdata->wind, wannaquit, cbdata );

    return TRUE; /* don't propagate event further */
}

static void
rowChangedCB( GtkTreeModel  * model UNUSED,
              GtkTreePath   * path,
              GtkTreeIter   * iter UNUSED,
              gpointer        sel)
{
    if( gtk_tree_selection_path_is_selected ( sel, path ) )
        refreshTorrentActions( GTK_TREE_SELECTION(sel) );
}

static void
winsetup( struct cbdata * cbdata, TrWindow * wind )
{
    GtkTreeModel * model;
    GtkTreeSelection * sel;

    g_assert( NULL == cbdata->wind );
    cbdata->wind = GTK_WINDOW( wind );

    sel = tr_window_get_selection( cbdata->wind );
    g_signal_connect( sel, "changed", G_CALLBACK(selectionChangedCB), NULL );
    selectionChangedCB( sel, NULL );
    model = tr_core_model( cbdata->core );
    g_signal_connect( model, "row-changed", G_CALLBACK(rowChangedCB), sel );
    g_signal_connect( wind, "delete-event", G_CALLBACK( winclose ), cbdata );
    refreshTorrentActions( sel );
    
    setupdrag( GTK_WIDGET(wind), cbdata );
}

static gpointer
quitThreadFunc( gpointer gdata )
{
    struct cbdata * cbdata = gdata;

    tr_close( tr_core_handle( cbdata->core ) );

    /* shutdown the gui */
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

    g_hash_table_destroy( cbdata->details2tor );
    g_hash_table_destroy( cbdata->tor2details );
    g_free( cbdata );

    /* exit the gtk main loop */
    gtk_main_quit( );
    return NULL;
}

/* since there are no buttons in the dialog, gtk tries to
 * select one of the labels, which looks ugly... so force
 * the dialog's primary and secondary labels to be unselectable */
static void
deselectLabels( GtkWidget * w, gpointer unused UNUSED )
{
    if( GTK_IS_LABEL( w ) )
        gtk_label_set_selectable( GTK_LABEL(w), FALSE );
    else if( GTK_IS_CONTAINER( w ) )
        gtk_container_foreach( GTK_CONTAINER(w), deselectLabels, NULL );
}

static void
do_exit_cb( GtkWidget *w UNUSED, gpointer data UNUSED )
{
    exit( 0 );
}

static void
wannaquit( void * vdata )
{
    GtkWidget * r, * p, * b, * w, *c;
    struct cbdata * cbdata = vdata;

    /* stop the update timer */
    if( cbdata->timer ) {
        g_source_remove( cbdata->timer );
        cbdata->timer = 0;
    }

    c = GTK_WIDGET( cbdata->wind );
    gtk_container_remove( GTK_CONTAINER( c ), gtk_bin_get_child( GTK_BIN( c ) ) );

    r = gtk_alignment_new(0.5, 0.5, 0.01, 0.01);
    gtk_container_add(GTK_CONTAINER(c), r);

    p = gtk_table_new(3, 2, FALSE);
    gtk_table_set_col_spacings( GTK_TABLE( p ), GUI_PAD_BIG );
    gtk_container_add( GTK_CONTAINER( r ), p );

    w = gtk_image_new_from_stock( GTK_STOCK_NETWORK, GTK_ICON_SIZE_DIALOG );
    gtk_table_attach_defaults(GTK_TABLE(p), w, 0, 1, 0, 2 );

    w = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL( w ), _( "<b>Closing Connections</b>" ) );
    gtk_misc_set_alignment( GTK_MISC( w ), 0.0, 0.5 );
    gtk_table_attach_defaults( GTK_TABLE( p ), w, 1, 2, 0, 1 );

    w = gtk_label_new( _( "Sending upload/download totals to tracker..." ) );
    gtk_misc_set_alignment( GTK_MISC( w ), 0.0, 0.5 );
    gtk_table_attach_defaults( GTK_TABLE( p ), w, 1, 2, 1, 2 );

    b = gtk_alignment_new(0.0, 1.0, 0.01, 0.01);
    w = gtk_button_new_with_label( _( "_Quit Immediately" ) );
    gtk_button_set_image( GTK_BUTTON(w), gtk_image_new_from_stock( GTK_STOCK_QUIT, GTK_ICON_SIZE_BUTTON ) );
    g_signal_connect(w, "clicked", G_CALLBACK(do_exit_cb), NULL);
    gtk_container_add(GTK_CONTAINER(b), w);
    gtk_table_attach(GTK_TABLE(p), b, 1, 2, 2, 3, GTK_FILL, GTK_FILL, 0, 10 );

    gtk_widget_show_all(r);

    /* clear the UI */
    gtk_list_store_clear( GTK_LIST_STORE( tr_core_model( cbdata->core ) ) );

    /* shut down libT */
    g_thread_create( quitThreadFunc, vdata, TRUE, NULL );
}

static void
gotdrag( GtkWidget         * widget UNUSED,
         GdkDragContext    * dc,
         gint                x UNUSED,
         gint                y UNUSED,
         GtkSelectionData  * sel,
         guint               info UNUSED,
         guint               time,
         gpointer            gdata )
{
    struct cbdata * data = gdata;
    GSList * paths = NULL;
    GSList * freeme = NULL;

#if 0
    int i;
    char *sele = gdk_atom_name(sel->selection);
    char *targ = gdk_atom_name(sel->target);
    char *type = gdk_atom_name(sel->type);

    g_message( "dropped file: sel=%s targ=%s type=%s fmt=%i len=%i",
               sele, targ, type, sel->format, sel->length );
    g_free(sele);
    g_free(targ);
    g_free(type);
    if( sel->format == 8 ) {
        for( i=0; i<sel->length; ++i )
            fprintf(stderr, "%02X ", sel->data[i]);
        fprintf(stderr, "\n");
    }
#endif

    if( ( sel->format == 8 ) &&
        ( sel->selection == gdk_atom_intern( "XdndSelection", FALSE ) ) )
    {
        int i;
        char * str = g_strndup( (char*)sel->data, sel->length );
        gchar ** files = g_strsplit_set( str, "\r\n", -1 );
        for( i=0; files && files[i]; ++i )
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

            /* if the file doesn't exist, the first part
               might be a hostname ... walk past it. */
            if( !g_file_test( filename, G_FILE_TEST_EXISTS ) ) {
                char * pch = strchr( filename + 1, '/' );
                if( pch != NULL )
                    filename = pch;
            }

            /* finally, add it to the list of torrents to try adding */
            if( g_file_test( filename, G_FILE_TEST_EXISTS ) )
                paths = g_slist_prepend( paths, g_strdup( filename ) );
        }

        /* try to add any torrents we found */
        if( paths )
        {
            paths = g_slist_reverse( paths );
            tr_core_add_list_defaults( data->core, paths );
            tr_core_torrents_added( data->core );
        }

        freestrlist( freeme );
        g_strfreev( files );
        g_free( str );
    }

    gtk_drag_finish(dc, (NULL != paths), FALSE, time);
}

static void
setupdrag(GtkWidget *widget, struct cbdata *data) {
  GtkTargetEntry targets[] = {
    { "STRING",     0, 0 },
    { "text/plain", 0, 0 },
    { "text/uri-list", 0, 0 },
  };

  g_signal_connect(widget, "drag_data_received", G_CALLBACK(gotdrag), data);

  gtk_drag_dest_set(widget, GTK_DEST_DEFAULT_ALL, targets,
                    ALEN(targets), GDK_ACTION_COPY | GDK_ACTION_MOVE);
}

static void
coreerr( TrCore * core UNUSED, enum tr_core_err code, const char * msg,
         gpointer gdata )
{
    struct cbdata * cbdata = gdata;
    char          * joined;

    switch( code )
    {
        case TR_CORE_ERR_ADD_TORRENT:
            cbdata->errqueue = g_slist_append( cbdata->errqueue,
                                               g_strdup( msg ) );
            return;
        case TR_CORE_ERR_NO_MORE_TORRENTS:
            if( cbdata->errqueue )
            {
                joined = joinstrlist( cbdata->errqueue, "\n" );
                errmsg( cbdata->wind,
                        ngettext( "Failed to load torrent file: %s",
                                  "Failed to load torrent files: %s",
                                  g_slist_length( cbdata->errqueue ) ),
                        joined );
                g_slist_foreach( cbdata->errqueue, (GFunc) g_free, NULL );
                g_slist_free( cbdata->errqueue );
                cbdata->errqueue = NULL;
                g_free( joined );
            }
            return;
        case TR_CORE_ERR_SAVE_STATE:
            errmsg( cbdata->wind, "%s", msg );
            return;
    }

    g_assert_not_reached();
}

#if GTK_CHECK_VERSION(2,8,0)
static void
on_main_window_focus_in( GtkWidget      * widget UNUSED,
                         GdkEventFocus  * event UNUSED,
                         gpointer         gdata )
{
    struct cbdata * cbdata = gdata;
    gtk_window_set_urgency_hint( GTK_WINDOW( cbdata->wind ), FALSE );
}
#endif

static void
onAddTorrent( TrCore * core, tr_ctor * ctor, gpointer gdata )
{
    struct cbdata * cbdata = gdata;
    GtkWidget * w = openSingleTorrentDialog( cbdata->wind, core, ctor );
#if GTK_CHECK_VERSION(2,8,0)
    g_signal_connect( w, "focus-in-event", G_CALLBACK(on_main_window_focus_in),  cbdata );
    gtk_window_set_urgency_hint( cbdata->wind, TRUE );
#endif
}

static void
prefschanged( TrCore * core UNUSED, const char * key, gpointer data )
{
    struct cbdata * cbdata = data;
    tr_handle     * tr     = tr_core_handle( cbdata->core );

    if( !strcmp( key, PREF_KEY_ENCRYPTED_ONLY ) )
    {
        const gboolean crypto_only = pref_flag_get( key );
        tr_setEncryptionMode( tr, crypto_only ? TR_ENCRYPTION_REQUIRED
                                              : TR_ENCRYPTION_PREFERRED );
    }
    else if( !strcmp( key, PREF_KEY_PORT ) )
    {
        const int port = pref_int_get( key );
        tr_setBindPort( tr, port );
    }
    else if( !strcmp( key, PREF_KEY_DL_LIMIT_ENABLED ) )
    {
        const gboolean b = pref_flag_get( key );
        tr_setUseGlobalSpeedLimit( tr, TR_DOWN, b );
    }
    else if( !strcmp( key, PREF_KEY_DL_LIMIT ) )
    {
        const int limit = pref_int_get( key );
        tr_setGlobalSpeedLimit( tr, TR_DOWN, limit );
    }
    else if( !strcmp( key, PREF_KEY_UL_LIMIT_ENABLED ) )
    {
        const gboolean b = pref_flag_get( key );
        tr_setUseGlobalSpeedLimit( tr, TR_UP, b );
    }
    else if( !strcmp( key, PREF_KEY_UL_LIMIT ) )
    {
        const int limit = pref_int_get( key );
        tr_setGlobalSpeedLimit( tr, TR_UP, limit );
    }
    else if( !strcmp( key, PREF_KEY_NAT ) )
    {
        const gboolean enabled = pref_flag_get( key );
        tr_natTraversalEnable( tr, enabled );
    }
    else if( !strcmp( key, PREF_KEY_PEX ) )
    {
        const gboolean enabled = pref_flag_get( key );
        tr_setPexEnabled( tr_core_handle( cbdata->core ), enabled );
    }
}

gboolean
updatemodel(gpointer gdata) {
  struct cbdata *data = gdata;

  if( !data->closing && 0 < global_sigcount )
  {
      wannaquit( data );
      return FALSE;
  }

  /* update the torrent data in the model */
  tr_core_update( data->core );

  /* update the main window's statusbar and toolbar buttons */
  if( data->wind )
      tr_window_update( data->wind );

  return TRUE;
}

static void
about ( GtkWindow * parent )
{
    const char *authors[] =
    {
        "Charles Kerr (Backend; GTK+)",
        "Mitchell Livingston (Backend; OS X)",
        "Eric Petit (Backend; OS X)",
        "Josh Elsasser (Daemon; Backend; GTK+)",
        "Bryan Varner (BeOS)", 
        NULL
    };

    gtk_show_about_dialog( parent,
        "name", g_get_application_name(),
        "comments", _("A fast and easy BitTorrent client"),
        "version", LONG_VERSION_STRING,
        "website", "http://www.transmissionbt.com/",
        "copyright",_("Copyright 2005-2008 The Transmission Project"),
        "logo-icon-name", "transmission",
#ifdef SHOW_LICENSE
        "license", LICENSE,
        "wrap-license", TRUE,
#endif
        "authors", authors,
        /* Translators: translate "translator-credits" as your name
           to have it appear in the credits in the "About" dialog */
        "translator-credits", _("translator-credits"),
        NULL );
}

static void
startTorrentForeach (GtkTreeModel * model,
                     GtkTreePath  * path UNUSED,
                     GtkTreeIter  * iter,
                     gpointer       data UNUSED)
{
    TrTorrent * tor = NULL;
    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    tr_torrent_start( tor );
    g_object_unref( G_OBJECT( tor ) );
}

static void
stopTorrentForeach (GtkTreeModel * model,
                    GtkTreePath  * path UNUSED,
                    GtkTreeIter  * iter,
                    gpointer       data UNUSED)
{
    TrTorrent * tor = NULL;
    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    tr_torrent_stop( tor );
    g_object_unref( G_OBJECT( tor ) );
}

static void
updateTrackerForeach (GtkTreeModel * model,
                      GtkTreePath  * path UNUSED,
                      GtkTreeIter  * iter,
                      gpointer       data UNUSED)
{
    TrTorrent * tor = NULL;
    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    tr_manualUpdate( tr_torrent_handle( tor ) );
    g_object_unref( G_OBJECT( tor ) );
}

static void
detailsClosed( gpointer user_data, GObject * details )
{
    struct cbdata * data = user_data;
    gpointer hashString = g_hash_table_lookup( data->details2tor, details );
    g_hash_table_remove( data->details2tor, details );
    g_hash_table_remove( data->tor2details, hashString );
}

static void
showInfoForeach (GtkTreeModel * model,
                 GtkTreePath  * path UNUSED,
                 GtkTreeIter  * iter,
                 gpointer       user_data )
{
    const char * hashString;
    struct cbdata * data = user_data;
    TrTorrent * tor = NULL;
    GtkWidget * w;

    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    hashString = tr_torrent_info(tor)->hashString;
    w = g_hash_table_lookup( data->tor2details, hashString );
    if( w != NULL )
        gtk_window_present( GTK_WINDOW( w ) );
    else {
        w = torrent_inspector_new( GTK_WINDOW( data->wind ), tor );
        gtk_widget_show( w );
        g_hash_table_insert( data->tor2details, (gpointer)hashString, w );
        g_hash_table_insert( data->details2tor, w, (gpointer)hashString );
        g_object_weak_ref( G_OBJECT( w ), detailsClosed, data );
    }

    g_object_unref( G_OBJECT( tor ) );
}

static void
recheckTorrentForeach (GtkTreeModel * model,
                       GtkTreePath  * path UNUSED,
                       GtkTreeIter  * iter,
                       gpointer       data UNUSED)
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
accumulateSelectedTorrents( GtkTreeModel * model,
                            GtkTreePath  * path UNUSED,
                            GtkTreeIter  * iter,
                            gpointer       gdata )
{
    GSList ** data = ( GSList** ) gdata;
    TrTorrent * tor = NULL;
    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    *data = g_slist_prepend( *data, tor );
}

static void
removeSelected( struct cbdata * data, gboolean delete_files )
{
    GSList * l = NULL;
    GtkTreeSelection * s = tr_window_get_selection( data->wind );
    gtk_tree_selection_selected_foreach( s, accumulateSelectedTorrents, &l );
    gtk_tree_selection_unselect_all( s );
    if( l ) {
        l = g_slist_reverse( l );
        confirmRemove( data->wind, data->core, l, delete_files );
    }
}

void
doAction ( const char * action_name, gpointer user_data )
{
    struct cbdata * data = user_data;
    gboolean changed = FALSE;

    if ( !strcmp (action_name, "open-torrent-menu") || !strcmp( action_name, "open-torrent-toolbar" ))
    {
        openDialog( data->wind, data->core );
    }
    else if (!strcmp (action_name, "show-stats"))
    {
        GtkWidget * dialog = stats_dialog_create( data->wind,
                                                  data->core );
        gtk_widget_show( dialog );
    }
    else if (!strcmp (action_name, "start-torrent"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, startTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if (!strcmp (action_name, "pause-torrent"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, stopTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if (!strcmp (action_name, "verify-torrent"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, recheckTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if (!strcmp (action_name, "show-torrent-details"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, showInfoForeach, data );
    }
    else if (!strcmp( action_name, "update-tracker"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, updateTrackerForeach, data->wind );
    }
    else if (!strcmp (action_name, "new-torrent"))
    {
        GtkWidget * w = make_meta_ui( GTK_WINDOW( data->wind ),
                                      tr_core_handle( data->core ) );
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
    else if (!strcmp (action_name, "close"))
    {
        if( data->wind != NULL )
            winclose( NULL, NULL, data );
    }
    else if (!strcmp (action_name, "quit"))
    {
        askquit( data->core, data->wind, wannaquit, data );
    }
    else if (!strcmp (action_name, "select-all"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_select_all( s );
    }
    else if (!strcmp (action_name, "deselect-all"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_unselect_all( s );
    }
    else if (!strcmp (action_name, "edit-preferences"))
    {
        if( NULL == data->prefs )
        {
            data->prefs = tr_prefs_dialog_new( G_OBJECT(data->core), data->wind );
            g_signal_connect( data->prefs, "destroy",
                             G_CALLBACK( gtk_widget_destroyed ), &data->prefs );
            gtk_widget_show( GTK_WIDGET( data->prefs ) );
        }
    }
    else if (!strcmp (action_name, "toggle-message-log"))
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
            action_toggle("toggle-message-log", FALSE);
            gtk_widget_destroy( data->msgwin );
            data->msgwin = NULL;
        }
    }
    else if (!strcmp (action_name, "show-about-dialog"))
    {
        about( data->wind );
    }
    else if (!strcmp (action_name, "toggle-main-window"))
    {
        toggleMainWindow( data );
    }
    else g_error ("Unhandled action: %s", action_name );

    if( changed )
        updatemodel( data );
}
