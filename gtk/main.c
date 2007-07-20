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

#include <sys/param.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "actions.h"
#include "conf.h"
#include "dialogs.h"
#include "ipc.h"
#include "makemeta-ui.h"
#include "msgwin.h"
#include "torrent-inspector.h"
#include "tr_cell_renderer_progress.h"
#include "tr_core.h"
#include "tr_icon.h"
#include "tr_prefs.h"
#include "tr_torrent.h"
#include "tr_window.h"
#include "util.h"
#include "ui.h"

#include <libtransmission/transmission.h>
#include <libtransmission/version.h>

/* time in seconds to wait for torrents to stop when exiting */
#define TRACKER_EXIT_TIMEOUT    10

/* interval in milliseconds to update the torrent list display */
#define UPDATE_INTERVAL         1000

/* interval in milliseconds to check for stopped torrents and update display */
#define EXIT_CHECK_INTERVAL     500

/* number of fatal signals required to cause an immediate exit */
#define SIGCOUNT_MAX            3

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

struct cbdata {
    GtkWindow    * wind;
    TrCore       * core;
    GtkWidget    * icon;
    TrPrefs      * prefs;
    guint          timer;
    gboolean       msgwinopen;
    gboolean       closing;
    GList        * errqueue;
};

struct exitdata {
    struct cbdata * cbdata;
    time_t          started;
    guint           timer;
};

#define CBDATA_PTR              "callback-data-pointer"

static GtkUIManager * myUIManager = NULL;

static sig_atomic_t global_sigcount = 0;

static GList *
readargs( int argc, char ** argv, gboolean * sendquit, gboolean * paused );
static gboolean
sendremote( GList * files, gboolean sendquit );
static void
gtksetup( int * argc, char *** argv, struct cbdata* );
static void
appsetup( TrWindow * wind, benc_val_t * state, GList * args,
          struct cbdata * , gboolean paused );
static void
winsetup( struct cbdata * cbdata, TrWindow * wind );
static void
makeicon( struct cbdata * cbdata );
static void
wannaquit( void * vdata );
static gboolean
exitcheck(gpointer gdata);
static void
setupdrag(GtkWidget *widget, struct cbdata *data);
static void
gotdrag(GtkWidget *widget, GdkDragContext *dc, gint x, gint y,
        GtkSelectionData *sel, guint info, guint time, gpointer gdata);

static void
coreerr( TrCore * core, enum tr_core_err code, const char * msg,
         gpointer gdata );
static void
coreprompt( TrCore *, GList *, enum tr_torrent_action, gboolean, gpointer );
static void
corepromptdata( TrCore *, uint8_t *, size_t, gboolean, gpointer );
static void
readinitialprefs( struct cbdata * cbdata );
static void
prefschanged( TrCore * core, int id, gpointer data );
static void
setpex( tr_torrent_t * tor, void * arg );
static gboolean
updatemodel(gpointer gdata);
static void
boolwindclosed(GtkWidget *widget, gpointer gdata);
static GList *
getselection( struct cbdata * cbdata );

static void
safepipe(void);
static void
setupsighandlers(void);
static void
fatalsig(int sig);

static void
accumulateStatusForeach (GtkTreeModel * model,
                         GtkTreePath  * path UNUSED,
                         GtkTreeIter  * iter,
                         gpointer       accumulated_status)
{
    int status = 0;
    gtk_tree_model_get( model, iter, MC_STAT, &status, -1 );
    *(int*)accumulated_status |= status;
}

static void
refreshTorrentActions( GtkTreeSelection * s )
{
    int status = 0;
    gtk_tree_selection_selected_foreach( s, accumulateStatusForeach, &status );
    action_sensitize( "stop-torrent", (status & TR_STATUS_ACTIVE) != 0);
    action_sensitize( "start-torrent", (status & TR_STATUS_INACTIVE) != 0);
    action_sensitize( "remove-torrent", status != 0);
    action_sensitize( "recheck-torrent", status != 0);
    action_sensitize( "show-torrent-inspector", status != 0);
}

static void
selectionChangedCB( GtkTreeSelection * s, gpointer unused UNUSED )
{
    refreshTorrentActions( s );
}

int
main( int argc, char ** argv )
{
    struct cbdata * cbdata = g_new (struct cbdata, 1);
    char       * err;
    benc_val_t * state;
    GList      * argfiles;
    gboolean     didinit, didlock, sendquit, startpaused;

    safepipe();                 /* ignore SIGPIPE */
    argfiles = readargs( argc, argv, &sendquit, &startpaused );
    didinit = cf_init( tr_getPrefsDirectory(), NULL );
    didlock = FALSE;
    if( didinit )
    {
        /* maybe send remote commands, also try cf_lock() */
        didlock = sendremote( argfiles, sendquit );
    }
    setupsighandlers();         /* set up handlers for fatal signals */
    gtksetup( &argc, &argv, cbdata );   /* set up gtk and gettext */

    if( ( didinit || cf_init( tr_getPrefsDirectory(), &err ) ) &&
        ( didlock || cf_lock( &err ) ) )
    {
        GtkWindow  * mainwind;

        /* create main window now to be a parent to any error dialogs */
        mainwind = GTK_WINDOW( tr_window_new( myUIManager ) );

        /* try to load prefs and saved state */
        cf_loadprefs( &err );
        if( NULL != err )
        {
            errmsg( mainwind, "%s", err );
            g_free( err );
        }
        state = cf_loadstate( &err );
        if( NULL != err )
        {
            errmsg( mainwind, "%s", err );
            g_free( err );
        }

        msgwin_loadpref();      /* set message level here before tr_init() */
        appsetup( mainwind, state, argfiles, cbdata, startpaused );
        cf_freestate( state );
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

GList *
readargs( int argc, char ** argv, gboolean * sendquit, gboolean * startpaused )
{
    struct option opts[] =
    {
        { "help",    no_argument, NULL, 'h' },
        { "paused",  no_argument, NULL, 'p' },
        { "quit",    no_argument, NULL, 'q' },
        { "version", no_argument, NULL, 'v' },
        { NULL, 0, NULL, 0 }
    };
    int          opt;
    const char * name;

    *sendquit    = FALSE;
    *startpaused = FALSE;

    gtk_parse_args( &argc, &argv );
    name = g_get_prgname();

    while( 0 <= ( opt = getopt_long( argc, argv, "hpqv", opts, NULL ) ) )
    {
        switch( opt )
        {
            case 'p':
                *startpaused = TRUE;
                break;
            case 'q':
                *sendquit = TRUE;
                break;
            case 'v':
            case 'h':
                printf(
_("usage: %s [-hpq] [files...]\n"
  "\n"
  "Transmission %s http://transmission.m0k.org/\n"
  "A free, lightweight BitTorrent client with a simple, intuitive interface\n"
  "\n"
  "  -h --help    display this message and exit\n"
  "  -p --paused  start with all torrents paused\n"
  "  -q --quit    request that the running %s instance quit\n"
  "\n"
  "Only one instance of %s may run at one time. Multiple\n"
  "torrent files may be loaded at startup by adding them to the command\n"
  "line. If %s is already running, those torrents will be\n"
  "opened in the running instance.\n"),
                        name, LONG_VERSION_STRING,
                        name, name, name );
                exit(0);
                break;
        }
    }

    argc -= optind;
    argv += optind;

    return checkfilenames( argc, argv );
}

static gboolean
sendremote( GList * files, gboolean sendquit )
{
    gboolean didlock;

    didlock = cf_lock( NULL );

    if( NULL != files )
    {
        /* send files if there's another instance, otherwise start normally */
        if( !didlock )
        {
            exit( ipc_sendfiles_blocking( files ) ? 0 : 1 );
        }
    }

    if( sendquit )
    {
        /* either send a quit message or exit if no other instance */
        if( !didlock )
        {
            exit( ipc_sendquit_blocking() ? 0 : 1 );
        }
        exit( 0 );
    }

    return didlock;
}

static void
gtksetup( int * argc, char *** argv, struct cbdata * callback_data )
{

    bindtextdomain( "transmission-gtk", LOCALEDIR );
    bind_textdomain_codeset( "transmission-gtk", "UTF-8" );
    textdomain( "transmission-gtk" );

    g_set_application_name( _("Transmission") );
    gtk_init( argc, argv );

    /* connect up the actions */
    myUIManager = gtk_ui_manager_new ();
    actions_init ( myUIManager, callback_data );
    gtk_ui_manager_add_ui_from_string (myUIManager, fallback_ui_file, -1, NULL);
    gtk_ui_manager_ensure_update (myUIManager);

    /* tweak some style properties in dialogs to get closer to the GNOME HiG */
    gtk_rc_parse_string(
        "style \"transmission-standard\"\n"
        "{\n"
        "    GtkDialog::action-area-border  = 6\n"
        "    GtkDialog::button-spacing      = 12\n"
        "    GtkDialog::content-area-border = 6\n"
        "}\n"
        "widget \"TransmissionDialog\" style \"transmission-standard\"\n" );

    gtk_window_set_default_icon_name ( "ICON_TRANSMISSION" );
}

static void
appsetup( TrWindow * wind, benc_val_t * state, GList * args,
          struct cbdata * cbdata, gboolean paused )
{
    enum tr_torrent_action action;

    /* fill out cbdata */
    cbdata->wind       = NULL;
    cbdata->core       = tr_core_new();
    cbdata->icon       = NULL;
    cbdata->prefs      = NULL;
    cbdata->timer      = 0;
    cbdata->msgwinopen = FALSE;
    cbdata->closing    = FALSE;
    cbdata->errqueue   = NULL;

    /* set up core handlers */
    g_signal_connect( cbdata->core, "error", G_CALLBACK( coreerr ), cbdata );
    g_signal_connect( cbdata->core, "directory-prompt",
                      G_CALLBACK( coreprompt ), cbdata );
    g_signal_connect( cbdata->core, "directory-prompt-data",
                      G_CALLBACK( corepromptdata ), cbdata );
    g_signal_connect_swapped( cbdata->core, "quit",
                              G_CALLBACK( wannaquit ), cbdata );
    g_signal_connect( cbdata->core, "prefs-changed",
                      G_CALLBACK( prefschanged ), cbdata );

    /* apply a few prefs */
    readinitialprefs( cbdata );

    /* add torrents from command-line and saved state */
    if( NULL != state )
    {
        tr_core_load( cbdata->core, state, paused );
    }
    if( NULL != args )
    {
        action = toraddaction( tr_prefs_get( PREF_ID_ADDIPC ) );
        tr_core_add_list( cbdata->core, args, action, paused );
    }
    tr_core_torrents_added( cbdata->core );

    /* set up the ipc socket */
    ipc_socket_setup( GTK_WINDOW( wind ), cbdata->core );

    /* set up main window */
    winsetup( cbdata, wind );

    /* start model update timer */
    cbdata->timer = g_timeout_add( UPDATE_INTERVAL, updatemodel, cbdata );
    updatemodel( cbdata );

    /* show the window */
    gtk_widget_show( GTK_WIDGET(wind) );
}

static gboolean
winclose( GtkWidget * widget UNUSED, GdkEvent * event UNUSED, gpointer gdata )
{
    struct cbdata * cbdata = (struct cbdata *) gdata;

    if( cbdata->icon != NULL )
        gtk_widget_hide( GTK_WIDGET( cbdata->wind ) );
    else
        askquit( cbdata->core, cbdata->wind, wannaquit, cbdata );

    return TRUE; /* don't propagate event further */
}

static void
rowChangedCB( GtkTreeModel  * model UNUSED,
              GtkTreePath   * path UNUSED,
              GtkTreeIter   * iter UNUSED,
              gpointer        sel)
{
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
    gtk_tree_view_set_model ( gtk_tree_selection_get_tree_view(sel), model );
    g_signal_connect( model, "row-changed", G_CALLBACK(rowChangedCB), sel );
    g_signal_connect( wind, "delete-event", G_CALLBACK( winclose ), cbdata );
    
    setupdrag( GTK_WIDGET(wind), cbdata );
}

static void
makeicon( struct cbdata * cbdata )
{
    if( NULL == cbdata->icon )
        cbdata->icon = tr_icon_new( );
}

static void
wannaquit( void * vdata )
{
  struct cbdata * data;
  struct exitdata *edata;

  data = vdata;
  if( data->closing )
  {
      return;
  }
  data->closing = TRUE;

  /* stop the update timer */
  if(0 < data->timer)
    g_source_remove(data->timer);
  data->timer = 0;

  /* pause torrents and stop nat traversal */
  tr_core_shutdown( data->core );

  /* set things up to wait for torrents to stop */
  edata = g_new0(struct exitdata, 1);
  edata->cbdata = data;
  edata->started = time(NULL);
  /* check if torrents are still running */
  if(exitcheck(edata)) {
    /* yes, start the exit timer and disable widgets */
    edata->timer = g_timeout_add(EXIT_CHECK_INTERVAL, exitcheck, edata);
    if( NULL != data->wind )
    {
        gtk_widget_set_sensitive( GTK_WIDGET( data->wind ), FALSE );
    }
  }
}

static gboolean
exitcheck( gpointer gdata )
{
    struct exitdata    * edata;
    struct cbdata      * cbdata;

    edata  = gdata;
    cbdata = edata->cbdata;

    /* keep waiting until we're ready to quit or we hit the exit timeout */
    if( time( NULL ) - edata->started < TRACKER_EXIT_TIMEOUT )
    {
        if( !tr_core_quiescent( cbdata->core ) )
        {
            updatemodel( cbdata );
            return TRUE;
        }
    }

    /* exit otherwise */
    if( 0 < edata->timer )
    {
        g_source_remove( edata->timer );
    }
    g_free( edata );
    /* note that cbdata->prefs holds a reference to cbdata->core, and
       it's destruction may trigger callbacks that use cbdata->core */
    if( NULL != cbdata->prefs )
    {
        gtk_widget_destroy( GTK_WIDGET( cbdata->prefs ) );
    }
    if( NULL != cbdata->wind )
    {
        gtk_widget_destroy( GTK_WIDGET( cbdata->wind ) );
    }
    g_object_unref( cbdata->core );
    if( NULL != cbdata->icon )
    {
        g_object_unref( cbdata->icon );
    }
    g_assert( 0 == cbdata->timer );
    if( NULL != cbdata->errqueue )
    {
        g_list_foreach( cbdata->errqueue, (GFunc) g_free, NULL );
        g_list_free( cbdata->errqueue );
    }
    g_free( cbdata );
    gtk_main_quit();

    return FALSE;
}

static void
gotdrag(GtkWidget *widget SHUTUP, GdkDragContext *dc, gint x SHUTUP,
        gint y SHUTUP, GtkSelectionData *sel, guint info SHUTUP, guint time,
        gpointer gdata) {
  struct cbdata *data = gdata;
  char prefix[] = "file:";
  char *files, *decoded, *deslashed, *hostless;
  int ii, len;
  GList *errs;
  struct stat sb;
  int prelen = strlen(prefix);
  GList *paths, *freeables;
  enum tr_torrent_action action;

#ifdef DND_DEBUG
  char *sele = gdk_atom_name(sel->selection);
  char *targ = gdk_atom_name(sel->target);
  char *type = gdk_atom_name(sel->type);

  fprintf(stderr, "dropped file: sel=%s targ=%s type=%s fmt=%i len=%i\n",
          sele, targ, type, sel->format, sel->length);
  g_free(sele);
  g_free(targ);
  g_free(type);
  if(8 == sel->format) {
    for(ii = 0; ii < sel->length; ii++)
      fprintf(stderr, "%02X ", sel->data[ii]);
    fprintf(stderr, "\n");
  }
#endif

  errs = NULL;
  paths = NULL;
  freeables = NULL;
  if(gdk_atom_intern("XdndSelection", FALSE) == sel->selection &&
     8 == sel->format) {
    /* split file list on carriage returns and linefeeds */
    files = g_new(char, sel->length + 1);
    memcpy(files, sel->data, sel->length);
    files[sel->length] = '\0';
    for(ii = 0; '\0' != files[ii]; ii++)
      if('\015' == files[ii] || '\012' == files[ii])
        files[ii] = '\0';

    /* try to get a usable filename out of the URI supplied and add it */
    for(ii = 0; ii < sel->length; ii += len + 1) {
      if('\0' == files[ii])
        len = 0;
      else {
        len = strlen(files + ii);
        /* de-urlencode the URI */
        decoded = urldecode(files + ii, len);
        freeables = g_list_append(freeables, decoded);
        if(g_utf8_validate(decoded, -1, NULL)) {
          /* remove the file: prefix */
          if(prelen < len && 0 == strncmp(prefix, decoded, prelen)) {
            deslashed = decoded + prelen;
            /* trim excess / characters from the beginning */
            while('/' == deslashed[0] && '/' == deslashed[1])
              deslashed++;
            /* if the file doesn't exist, the first part might be a hostname */
            if(0 > g_stat(deslashed, &sb) &&
               NULL != (hostless = strchr(deslashed + 1, '/')) &&
               0 == g_stat(hostless, &sb))
              deslashed = hostless;
            /* finally, add it to the list of torrents to try adding */
            paths = g_list_append(paths, deslashed);
          }
        }
      }
    }

    /* try to add any torrents we found */
    if( NULL != paths )
    {
        action = toraddaction( tr_prefs_get( PREF_ID_ADDSTD ) );
        tr_core_add_list( data->core, paths, action, FALSE );
        tr_core_torrents_added( data->core );
        g_list_free(paths);
    }
    freestrlist(freeables);
    g_free(files);
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
coreerr( TrCore * core SHUTUP, enum tr_core_err code, const char * msg,
         gpointer gdata )
{
    struct cbdata * cbdata = gdata;
    char          * joined;

    switch( code )
    {
        case TR_CORE_ERR_ADD_TORRENT:
            cbdata->errqueue = g_list_append( cbdata->errqueue,
                                              g_strdup( msg ) );
            return;
        case TR_CORE_ERR_NO_MORE_TORRENTS:
            if( NULL != cbdata->errqueue )
            {
                joined = joinstrlist( cbdata->errqueue, "\n" );
                errmsg( cbdata->wind,
                        ngettext( "Failed to load torrent file:\n%s",
                                  "Failed to load torrent files:\n%s",
                                  g_list_length( cbdata->errqueue ) ),
                        joined );
                g_list_foreach( cbdata->errqueue, (GFunc) g_free, NULL );
                g_list_free( cbdata->errqueue );
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

void
coreprompt( TrCore * core, GList * paths, enum tr_torrent_action act,
            gboolean paused, gpointer gdata )
{
    struct cbdata * cbdata = gdata;

    promptfordir( cbdata->wind, core, paths, NULL, 0, act, paused );
}

void
corepromptdata( TrCore * core, uint8_t * data, size_t size,
                gboolean paused, gpointer gdata )
{
    struct cbdata * cbdata = gdata;

    promptfordir( cbdata->wind, core, NULL, data, size, TR_TOR_LEAVE, paused );
}

static void
readinitialprefs( struct cbdata * cbdata )
{
    int prefs[] =
    {
        PREF_ID_PORT,
        PREF_ID_USEDOWNLIMIT,
        PREF_ID_USEUPLIMIT,
        PREF_ID_NAT,
        PREF_ID_ICON,
        PREF_ID_PEX,
    };
    int ii;

    for( ii = 0; ALEN( prefs ) > ii; ii++ )
    {
        prefschanged( NULL, prefs[ii], cbdata );
    }
}

static void
prefschanged( TrCore * core SHUTUP, int id, gpointer data )
{
    struct cbdata * cbdata;
    tr_handle_t   * tr;
    gboolean        boolval;

    cbdata = data;
    tr     = tr_core_handle( cbdata->core );

    switch( id )
    {
        case PREF_ID_PORT:
            tr_setBindPort( tr, tr_prefs_get_int_with_default( id ) );
            break;

        case PREF_ID_USEDOWNLIMIT:
            tr_setUseGlobalSpeedLimit( tr, TR_DOWN,
                tr_prefs_get_bool_with_default( PREF_ID_USEDOWNLIMIT ) );
            break;

        case PREF_ID_DOWNLIMIT:
            tr_setGlobalSpeedLimit( tr, TR_DOWN,
                tr_prefs_get_int_with_default( PREF_ID_DOWNLIMIT ) );
            break;

        case PREF_ID_USEUPLIMIT:
            tr_setUseGlobalSpeedLimit( tr, TR_UP,
                tr_prefs_get_bool_with_default( PREF_ID_USEUPLIMIT ) );
            break;

        case PREF_ID_UPLIMIT:
            tr_setGlobalSpeedLimit( tr, TR_UP,
                tr_prefs_get_int_with_default( PREF_ID_UPLIMIT ) );
            break;

        case PREF_ID_NAT:
            tr_natTraversalEnable( tr, tr_prefs_get_bool_with_default( id ) );
            break;

        case PREF_ID_ICON:
            if( tr_prefs_get_bool_with_default( id ) )
            {
                makeicon( cbdata );
            }
            else if( NULL != cbdata->icon )
            {
g_message ("foo");
                g_object_unref( cbdata->icon );
                cbdata->icon = NULL;
            }
            break;

        case PREF_ID_PEX:
            boolval = tr_prefs_get_bool_with_default( id );
            tr_torrentIterate( tr, setpex, &boolval );
            break;

        case PREF_ID_DIR:
        case PREF_ID_ASKDIR:
        case PREF_ID_ADDSTD:
        case PREF_ID_ADDIPC:
        case PREF_ID_MSGLEVEL:
        case PREF_MAX_ID:
            break;
    }
}

void
setpex( tr_torrent_t * tor, void * arg )
{
    gboolean * val;

    val = arg;
    tr_torrentDisablePex( tor, !(*val) );
}

gboolean
updatemodel(gpointer gdata) {
  struct cbdata *data = gdata;
  float up, down;

  if( !data->closing && 0 < global_sigcount )
  {
      wannaquit( data );
      return FALSE;
  }

  /* update the torrent data in the model */
  tr_core_update( data->core );

  /* update the main window's statusbar and toolbar buttons */
  if( NULL != data->wind )
  {
      tr_torrentRates( tr_core_handle( data->core ), &down, &up );
      tr_window_update( data->wind, down, up );
  }

  /* update the message window */
  msgwin_update();

  return TRUE;
}

static void
boolwindclosed(GtkWidget *widget SHUTUP, gpointer gdata) {
  gboolean *preachy_gcc = gdata;
  
  *preachy_gcc = FALSE;
}

/* returns a GList containing a GtkTreeRowReference to each selected row */
static GList *
getselection( struct cbdata * cbdata )
{
    GList * rows = NULL;

    if( NULL != cbdata->wind )
    {
        GList * ii;
        GtkTreeSelection *s = tr_window_get_selection(cbdata->wind);
        GtkTreeModel * model = tr_core_model( cbdata->core );
        rows = gtk_tree_selection_get_selected_rows( s, NULL );
        for( ii = rows; NULL != ii; ii = ii->next )
        {
            GtkTreeRowReference * ref = gtk_tree_row_reference_new(
                model, ii->data );
            gtk_tree_path_free( ii->data );
            ii->data = ref;
        }
    }

    return rows;
}

static void
about ( void )
{
  GtkWidget * w = gtk_about_dialog_new ();
  GtkAboutDialog * a = GTK_ABOUT_DIALOG (w);
  const char *authors[] = { "Eric Petit (Back-end; OS X)",
                            "Josh Elsasser (Back-end; GTK+)",
                            "Mitchell Livingston (Back-end; OS X)",
                            "Charles Kerr (Back-end; GTK+)",
                            "Bryan Varner (BeOS)", 
                            NULL };
  gtk_about_dialog_set_version (a, LONG_VERSION_STRING );
#ifdef SHOW_LICENSE
  gtk_about_dialog_set_license (a, LICENSE);
  gtk_about_dialog_set_wrap_license (a, TRUE);
#endif
  gtk_about_dialog_set_website (a, "http://transmission.m0k.org/");
  gtk_about_dialog_set_copyright (a, _("Copyright 2005-2007 The Transmission Project"));
  gtk_about_dialog_set_authors (a, authors);
  gtk_about_dialog_set_translator_credits (a, _("translator-credits"));
  g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
  gtk_widget_show_all (w);
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
showInfoForeach (GtkTreeModel * model,
                 GtkTreePath  * path UNUSED,
                 GtkTreeIter  * iter,
                 gpointer       data UNUSED)
{
    TrTorrent * tor = NULL;
    GtkWidget * w;
    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    w = torrent_inspector_new( GTK_WINDOW(data), tor );
    gtk_widget_show( w );
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
    tr_torrentRecheck( tr_torrent_handle( gtor ) );
    g_object_unref( G_OBJECT( gtor ) );
}

void
doAction ( const char * action_name, gpointer user_data )
{
    struct cbdata * data = (struct cbdata *) user_data;
    gboolean changed = FALSE;

    if (!strcmp (action_name, "add-torrent"))
    {
        makeaddwind( data->wind, data->core );
    }
    else if (!strcmp (action_name, "start-torrent"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, startTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if (!strcmp (action_name, "stop-torrent"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, stopTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if (!strcmp (action_name, "recheck-torrent"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, recheckTorrentForeach, NULL );
        changed |= gtk_tree_selection_count_selected_rows( s ) != 0;
    }
    else if (!strcmp (action_name, "show-torrent-inspector"))
    {
        GtkTreeSelection * s = tr_window_get_selection(data->wind);
        gtk_tree_selection_selected_foreach( s, showInfoForeach, data->wind );
    }
    else if (!strcmp (action_name, "create-torrent"))
    {
        GtkWidget * w = make_meta_ui( GTK_WINDOW( data->wind ), tr_core_handle( data->core ) );
        gtk_widget_show_all( w );
    }
    else if (!strcmp (action_name, "remove-torrent"))
    {
        /* this modifies the model's contents, so can't use foreach */
        GList *l, *sel = getselection( data );
        GtkTreeModel *model = tr_core_model( data->core );
        for( l=sel; l!=NULL; l=l->next )
        {
            GtkTreeIter iter;
            GtkTreeRowReference * reference = (GtkTreeRowReference *) l->data;
            GtkTreePath * path = gtk_tree_row_reference_get_path( reference );
            gtk_tree_model_get_iter( model, &iter, path );
            tr_core_delete_torrent( data->core, &iter );
            gtk_tree_row_reference_free( reference );
            changed = TRUE;
        }
        g_list_free( sel );
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
    else if (!strcmp (action_name, "edit-preferences"))
    {
        if( NULL == data->prefs )
        {
            data->prefs = tr_prefs_new_with_parent( G_OBJECT( data->core ),
                                                    data->wind );
            g_signal_connect( data->prefs, "destroy",
                             G_CALLBACK( gtk_widget_destroyed ), &data->prefs );
            gtk_widget_show( GTK_WIDGET( data->prefs ) );
        }
    }
    else if (!strcmp (action_name, "show-debug-window"))
    {
        if( !data->msgwinopen )
        {
            GtkWidget * win = msgwin_create( data->core );
            g_signal_connect( win, "destroy", G_CALLBACK( boolwindclosed ),
                                &data->msgwinopen );
            data->msgwinopen = TRUE;
        }
    }
    else if (!strcmp (action_name, "show-about-dialog"))
    {
        about();
    }
    else if (!strcmp (action_name, "toggle-main-window"))
    {
        GtkWidget * w = GTK_WIDGET (data->wind);
        if (GTK_WIDGET_VISIBLE(w))
            gtk_widget_hide (w);
        else
            gtk_window_present (GTK_WINDOW(w));
    }
    else g_error ("Unhandled action: %s", action_name );

    if(changed)
    {
        updatemodel( data );
        tr_core_save( data->core );
    }
}


static void
safepipe(void) {
  struct sigaction sa;

  memset(&sa, 0,  sizeof(sa));
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);
}

static void
setupsighandlers(void) {
  int sigs[] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM};
  struct sigaction sa;
  int ii;

  memset(&sa, 0,  sizeof(sa));
  sa.sa_handler = fatalsig;
  for(ii = 0; ii < ALEN(sigs); ii++)
    sigaction(sigs[ii], &sa, NULL);
}

static void
fatalsig(int sig) {
  struct sigaction sa;

  if(SIGCOUNT_MAX <= ++global_sigcount) {
    memset(&sa, 0,  sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, NULL);
    raise(sig);
  }
}
