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

#include "conf.h"
#include "dialogs.h"
#include "ipc.h"
#include "msgwin.h"
#include "tr_cell_renderer_progress.h"
#include "tr_core.h"
#include "tr_icon.h"
#include "tr_prefs.h"
#include "tr_torrent.h"
#include "tr_window.h"
#include "util.h"

#include "transmission.h"

#include "img_icon_full.h"

/* time in seconds to wait for torrents to stop when exiting */
#define TRACKER_EXIT_TIMEOUT    10

/* interval in milliseconds to update the torrent list display */
#define UPDATE_INTERVAL         1000

/* interval in milliseconds to check for stopped torrents and update display */
#define EXIT_CHECK_INTERVAL     500

/* number of fatal signals required to cause an immediate exit */
#define SIGCOUNT_MAX            3

struct cbdata {
    GtkWindow    * wind;
    TrCore       * core;
    TrIcon       * icon;
    TrPrefs      * prefs;
    guint          timer;
    gboolean       msgwinopen;
    gboolean       closing;
};

struct exitdata {
    struct cbdata * cbdata;
    time_t          started;
    guint           timer;
};

enum
{
    ACT_OPEN = 0,
    ACT_START,
    ACT_STOP,
    ACT_DELETE,
    ACT_SEPARATOR1,
    ACT_INFO,
    ACT_DEBUG,
    ACT_SEPARATOR2,
    ACT_PREF,
    ACT_SEPARATOR3,
    ACT_CLOSE,
    ACT_QUIT,
    ACT_ICON,
    ACTION_COUNT,
};

struct
{
    const char * label;
    const char * icon;
    guint        key;
    int          flags;
    const char * tooltip;
}
actions[] =
{
    { NULL,        GTK_STOCK_ADD,       'o', ACTF_WHEREVER | ACTF_ALWAYS,
      N_("Add a new torrent") },
    { N_("Start"), GTK_STOCK_EXECUTE,     0, ACTF_WHEREVER | ACTF_INACTIVE,
      N_("Start a torrent that is not running") },
    { NULL,        GTK_STOCK_STOP,        0, ACTF_WHEREVER | ACTF_ACTIVE,
      N_("Stop a torrent that is running") },
    { NULL,        GTK_STOCK_REMOVE,      0, ACTF_WHEREVER | ACTF_WHATEVER,
      N_("Remove a torrent") },
    { NULL,        NULL,                  0, ACTF_SEPARATOR, NULL },
    { NULL,        GTK_STOCK_PROPERTIES,  0, ACTF_WHEREVER | ACTF_WHATEVER,
      N_("Show additional information about a torrent") },
    { N_("Open debug window"), NULL,      0, ACTF_MENU     | ACTF_ALWAYS,
      NULL },
    { NULL,        NULL,                  0, ACTF_SEPARATOR, NULL },
    { NULL,        GTK_STOCK_PREFERENCES, 0, ACTF_WHEREVER | ACTF_ALWAYS,
      N_("Customize application behavior") },
    { NULL,        NULL,                  0, ACTF_SEPARATOR, NULL },
    { NULL,        GTK_STOCK_CLOSE,       0, ACTF_MENU     | ACTF_ALWAYS,
      N_("Close the main window") },
    { NULL,        GTK_STOCK_QUIT,        0, ACTF_MENU     | ACTF_ALWAYS,
      N_("Exit the program") },
    /* this isn't a terminator for the list, it's ACT_ICON */
    { NULL,        NULL,                  0, 0, NULL },
};

#define CBDATA_PTR              "callback-data-pointer"

static sig_atomic_t global_sigcount = 0;

static GList *
readargs( int argc, char ** argv, gboolean * sendquit, gboolean * paused );
static gboolean
sendremote( GList * files, gboolean sendquit );
static void
gtksetup( int * argc, char *** argv );
static void
appsetup( TrWindow * wind, benc_val_t * state, GList * args, gboolean paused );
static void
winsetup( struct cbdata * cbdata, TrWindow * wind );
static void
iconclick( struct cbdata * cbdata );
static void
makeicon( struct cbdata * cbdata );
static gboolean
winclose( GtkWidget * widget, GdkEvent * event, gpointer gdata );
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
readinitialprefs( struct cbdata * cbdata );
static void
prefschanged( GtkWidget * widget, int id, gpointer data );
static void
setpex( tr_torrent_t * tor, void * arg );
static gboolean
updatemodel(gpointer gdata);
static void
boolwindclosed(GtkWidget *widget, gpointer gdata);
static void
windact(GtkWidget *widget, int action, gpointer gdata);
static GList *
getselection( struct cbdata * cbdata );
static void
handleaction( struct cbdata *data, int action );

static void
addtorrents(void *vdata, void *state, GList *files,
            const char *dir, guint flags);
static void
savetorrents(struct cbdata *data);
static void
safepipe(void);
static void
setupsighandlers(void);
static void
fatalsig(int sig);

int
main( int argc, char ** argv )
{
    GtkWindow  * mainwind;
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
    gtksetup( &argc, &argv );   /* set up gtk and gettext */

    if( ( didinit || cf_init( tr_getPrefsDirectory(), &err ) ) &&
        ( didlock || cf_lock( &err ) ) )
    {
        /* create main window now to be a parent to any error dialogs */
        mainwind = GTK_WINDOW( tr_window_new() );

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
        appsetup( TR_WINDOW( mainwind ), state, argfiles, startpaused );
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
  "Transmission %s (r%d) http://transmission.m0k.org/\n"
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
                        name, VERSION_STRING, VERSION_REVISION,
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
gtksetup( int * argc, char *** argv )
{
    GdkPixbuf * icon;

    gtk_init( argc, argv );

    bindtextdomain( "transmission-gtk", LOCALEDIR );
    bind_textdomain_codeset( "transmission-gtk", "UTF-8" );
    textdomain( "transmission-gtk" );

    g_set_application_name( _("Transmission") );

    /* tweak some style properties in dialogs to get closer to the GNOME HiG */
    gtk_rc_parse_string(
        "style \"transmission-standard\"\n"
        "{\n"
        "    GtkDialog::action-area-border  = 6\n"
        "    GtkDialog::button-spacing      = 12\n"
        "    GtkDialog::content-area-border = 6\n"
        "}\n"
        "widget \"TransmissionDialog\" style \"transmission-standard\"\n" );

    icon = gdk_pixbuf_new_from_inline( -1, tr_icon_full, FALSE, NULL );
    gtk_window_set_default_icon( icon );
    g_object_unref( icon );
}

static void
appsetup( TrWindow * wind, benc_val_t * state, GList * args, gboolean paused )
{
    struct cbdata * cbdata;
    guint           flags;

    /* fill out cbdata */
    cbdata = g_new0( struct cbdata, 1 );
    cbdata->wind       = NULL;
    cbdata->core       = tr_core_new();
    cbdata->icon       = NULL;
    cbdata->prefs      = NULL;
    cbdata->timer      = 0;
    cbdata->msgwinopen = FALSE;
    cbdata->closing    = FALSE;

    /* apply a few prefs */
    readinitialprefs( cbdata );

    /* set up main window */
    winsetup( cbdata, wind );

    /* add torrents from command-line and saved state */
    flags = addactionflag( tr_prefs_get( PREF_ID_ADDIPC ) );
    g_assert( !( flags & ( TR_TORNEW_PAUSED | TR_TORNEW_RUNNING ) ) );
    if( paused )
    {
        flags |= TR_TORNEW_PAUSED;
    }
    addtorrents( cbdata, state, args, NULL, flags );

    /* start model update timer */
    cbdata->timer = g_timeout_add( UPDATE_INTERVAL, updatemodel, cbdata );
    updatemodel( cbdata );

    /* set up the ipc socket now that we're ready to get torrents from it */
    ipc_socket_setup( GTK_WINDOW( wind ), addtorrents, wannaquit, cbdata );

    /* show the window */
    tr_window_show( wind );
}

static void
winsetup( struct cbdata * cbdata, TrWindow * wind )
{
    int ii;
    GtkWidget  * drag;

    g_assert( ACTION_COUNT == ALEN( actions ) );
    g_assert( NULL == cbdata->wind );
    cbdata->wind = GTK_WINDOW( wind );
    for( ii = 0; ii < ALEN( actions ); ii++ )
    {
        tr_window_action_add( wind, ii, actions[ii].flags,
                              gettext( actions[ii].label ), actions[ii].icon,
                              gettext( actions[ii].tooltip ),
                              actions[ii].key );
    }
    g_object_set( wind, "model", tr_core_model( cbdata->core ),
                        "double-click-action", ACT_INFO, NULL);

    g_signal_connect( wind, "action",       G_CALLBACK( windact  ), cbdata );
    g_signal_connect( wind, "delete-event", G_CALLBACK( winclose ), cbdata );
    
    g_object_get( wind, "drag-widget", &drag, NULL );
    setupdrag( drag, cbdata );
}

static void
iconclick( struct cbdata * cbdata )
{
    GtkWidget * win;

    if( NULL != cbdata->wind )
    {
        /* close window  */
        winclose( NULL, NULL, cbdata );
    }
    else
    {
        /* create window */
        win = tr_window_new();
        winsetup( cbdata, TR_WINDOW( win ) );
        tr_window_show( TR_WINDOW( win ) );
    }
}

static void
makeicon( struct cbdata * cbdata )
{
    TrIcon * icon;
    int      ii;

    if( NULL != cbdata->icon )
    {
        return;
    }

    icon = tr_icon_new();
    for( ii = 0; ii < ALEN( actions ); ii++ )
    {
        tr_icon_action_add( TR_ICON( icon ), ii, actions[ii].flags,
                              gettext( actions[ii].label ), actions[ii].icon );
    }
    g_object_set( icon, "activate-action", ACT_ICON, NULL);
    g_signal_connect( icon, "action", G_CALLBACK( windact ), cbdata );

    cbdata->icon = icon;
}

static gboolean
winclose( GtkWidget * widget SHUTUP, GdkEvent * event SHUTUP, gpointer gdata )
{
    struct cbdata * cbdata;

    cbdata = gdata;

    if( NULL != cbdata->icon && tr_icon_docked( cbdata->icon ) )
    {
        gtk_widget_destroy( GTK_WIDGET( cbdata->wind ) );
        cbdata->wind = NULL;
    }
    else
    {
        askquit( cbdata->wind, wannaquit, cbdata );
    }

    /* don't propagate event further */
    return TRUE;
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
    /* The prefs window need to be destroyed first as destroying it may
       trigger callbacks that use cbdata->core. Ick. */
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
        addtorrents( data, NULL, paths, NULL,
                     addactionflag( tr_prefs_get( PREF_ID_ADDSTD ) ) );
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
prefschanged( GtkWidget * widget SHUTUP, int id, gpointer data )
{
    struct cbdata * cbdata;
    tr_handle_t   * tr;
    int             num;
    gboolean        boolval;

    cbdata = data;
    tr     = tr_core_handle( cbdata->core );

    switch( id )
    {
        case PREF_ID_PORT:
            tr_setBindPort( tr, tr_prefs_get_int_with_default( id ) );
            break;

        case PREF_ID_USEDOWNLIMIT:
        case PREF_ID_DOWNLIMIT:
            num = -1;
            if( tr_prefs_get_bool_with_default( PREF_ID_USEDOWNLIMIT ) )
            {
                num = tr_prefs_get_int_with_default( PREF_ID_DOWNLIMIT );
            }
            tr_setGlobalDownloadLimit( tr, num );
            break;

        case PREF_ID_USEUPLIMIT:
        case PREF_ID_UPLIMIT:
            num = -1;
            if( tr_prefs_get_bool_with_default( PREF_ID_USEUPLIMIT ) )
            {
                num = tr_prefs_get_int_with_default( PREF_ID_UPLIMIT );
            }
            tr_setGlobalUploadLimit( tr, num );
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
      tr_window_update( TR_WINDOW( data->wind ), down, up );
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

static void
windact( GtkWidget * wind SHUTUP, int action, gpointer gdata )
{
    g_assert( 0 <= action );
    handleaction( gdata, action );
}

/* returns a GList containing a GtkTreeRowReference to each selected row */
static GList *
getselection( struct cbdata * cbdata )
{
    GtkTreeSelection    * sel;
    GList               * rows, * ii;
    GtkTreeRowReference * ref;

    if( NULL == cbdata->wind )
    {
        return NULL;
    }
    g_object_get( cbdata->wind, "selection", &sel, NULL );
    rows = gtk_tree_selection_get_selected_rows( sel, NULL );
    for( ii = rows; NULL != ii; ii = ii->next )
    {
        ref = gtk_tree_row_reference_new( tr_core_model( cbdata->core ), ii->data );
        gtk_tree_path_free( ii->data );
        ii->data = ref;
    }

    return rows;
}

static void
handleaction( struct cbdata * data, int act )
{
  GList *rows, *ii;
  GtkTreeModel * model;
  GtkTreePath *path;
  GtkTreeIter iter;
  TrTorrent *tor;
  int status;
  gboolean changed;
  GtkWidget * win;

  g_assert( ACTION_COUNT > act );

  switch( act )
  {
      case ACT_OPEN:
          makeaddwind( data->wind, addtorrents, data );
          return;
      case ACT_PREF:
          if( NULL != data->prefs )
          {
              return;
          }
          data->prefs = tr_prefs_new_with_parent( data->wind );
          g_signal_connect( data->prefs, "prefs-changed",
                            G_CALLBACK( prefschanged ), data );
          g_signal_connect( data->prefs, "destroy",
                            G_CALLBACK( gtk_widget_destroyed ), &data->prefs );
          gtk_widget_show( GTK_WIDGET( data->prefs ) );
          return;
      case ACT_DEBUG:
          if( !data->msgwinopen )
          {
              data->msgwinopen = TRUE;
              win = msgwin_create();
              g_signal_connect( win, "destroy", G_CALLBACK( boolwindclosed ),
                                &data->msgwinopen );
          }
          return;
      case ACT_ICON:
          iconclick( data );
          return;
      case ACT_CLOSE:
          if( NULL != data->wind )
          {
              winclose( NULL, NULL, data );
          }
          return;
      case ACT_QUIT:
          askquit( data->wind, wannaquit, data );
          return;
      case ACT_SEPARATOR1:
      case ACT_SEPARATOR2:
      case ACT_SEPARATOR3:
          return;
      case ACT_START:
      case ACT_STOP:
      case ACT_DELETE:
      case ACT_INFO:
      case ACTION_COUNT:
          break;
  }

  /* get a list of references to selected rows */
  rows = getselection( data );

  model = tr_core_model( data->core );
  changed = FALSE;
  for(ii = rows; NULL != ii; ii = ii->next) {
    if(NULL != (path = gtk_tree_row_reference_get_path(ii->data)) &&
       gtk_tree_model_get_iter( model, &iter, path ) )
    {
      gtk_tree_model_get( model , &iter, MC_TORRENT, &tor,
                          MC_STAT, &status, -1 );
      if( ACT_ISAVAIL( actions[act].flags, status ) )

      {
          switch( act )
          {
              case ACT_START:
                  tr_torrentStart( tr_torrent_handle( tor ) );
                  changed = TRUE;
                  break;
              case ACT_STOP:
                  tr_torrentStop( tr_torrent_handle( tor ) );
                  changed = TRUE;
                  break;
              case ACT_DELETE:
                  tr_core_delete_torrent( data->core, &iter );
                  changed = TRUE;
                  break;
              case ACT_INFO:
                  makeinfowind( data->wind, model, path, tor );
                  break;
              case ACT_OPEN:
              case ACT_PREF:
              case ACT_DEBUG:
              case ACT_ICON:
              case ACT_CLOSE:
              case ACT_QUIT:
              case ACT_SEPARATOR1:
              case ACT_SEPARATOR2:
              case ACT_SEPARATOR3:
              case ACTION_COUNT:
                  break;
          }
      }
      g_object_unref(tor);
    }
    if(NULL != path)
      gtk_tree_path_free(path);
    gtk_tree_row_reference_free(ii->data);
  }
  g_list_free(rows);

  if(changed) {
    savetorrents(data);
    updatemodel(data);
  }
}

static void
addtorrents(void *vdata, void *state, GList *files,
            const char *dir, guint flags) {
  struct cbdata *data = vdata;
  GList *errlist, *ii;
  char *errstr;
  const char * pref;
  int added;

  errlist = NULL;
  added   = 0;

  if( NULL != state )
  {
      added += tr_core_load( data->core, state, &errlist );
  }

  if(NULL != files) {
    if( NULL == dir )
    {
        pref = tr_prefs_get( PREF_ID_ASKDIR );
        if( NULL != pref && strbool( pref ) )
        {
            promptfordir( data->wind, addtorrents, data, files, flags );
            files = NULL;
        }
        dir = getdownloaddir();
    }
    for(ii = g_list_first(files); NULL != ii; ii = ii->next) {
      errstr = NULL;
      if( tr_core_add_torrent( data->core, ii->data, dir, flags, &errstr ) )
      {
          added++;
      }
      if(NULL != errstr)
        errlist = g_list_append(errlist, errstr);
    }
  }

  if(NULL != errlist) {
    errstr = joinstrlist(errlist, "\n");
    errmsg( data->wind, ngettext( "Failed to load torrent file:\n%s",
                                  "Failed to load torrent files:\n%s",
                                  g_list_length( errlist ) ), errstr );
    g_list_foreach(errlist, (GFunc)g_free, NULL);
    g_list_free(errlist);
    g_free(errstr);
  }

  if( 0 < added )
  {
    updatemodel(data);
    savetorrents(data);
  }
}

static void
savetorrents( struct cbdata *data )
{
    char * errstr;

    tr_core_save( data->core, &errstr );
    if( NULL != errstr )
    {
        errmsg( data->wind, "%s", errstr );
        g_free( errstr );
    }
}

static void
safepipe(void) {
  struct sigaction sa;

  bzero(&sa, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);
}

static void
setupsighandlers(void) {
  int sigs[] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM};
  struct sigaction sa;
  int ii;

  bzero(&sa, sizeof(sa));
  sa.sa_handler = fatalsig;
  for(ii = 0; ii < ALEN(sigs); ii++)
    sigaction(sigs[ii], &sa, NULL);
}

static void
fatalsig(int sig) {
  struct sigaction sa;

  if(SIGCOUNT_MAX <= ++global_sigcount) {
    bzero(&sa, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, NULL);
    raise(sig);
  }
}
