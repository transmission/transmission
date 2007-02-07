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
#include "tr_backend.h"
#include "tr_torrent.h"
#include "tr_cell_renderer_progress.h"
#include "tr_window.h"
#include "transmission.h"
#include "util.h"

#include "img_icon_full.h"

/* time in seconds to wait for torrents to stop when exiting */
#define TRACKER_EXIT_TIMEOUT    10

/* interval in milliseconds to update the torrent list display */
#define UPDATE_INTERVAL         1000

/* interval in milliseconds to check for stopped torrents and update display */
#define EXIT_CHECK_INTERVAL     500

struct cbdata {
  TrBackend *back;
  GtkWindow *wind;
  GtkTreeModel *model;
  guint timer;
  gboolean prefsopen;
  gboolean msgwinopen;
  gboolean closing;
};

struct exitdata {
  struct cbdata *cbdata;
  time_t started;
  guint timer;
};

enum action
{
    ACT_OPEN = 0,
    ACT_START,
    ACT_STOP,
    ACT_DELETE,
    ACT_INFO,
    ACT_PREF,
    ACT_DEBUG,
    ACTION_COUNT,
};

struct
{
    const char * label;
    const char * icon;
    int          flags;
    const char * tooltip;
}
actions[] =
{
    { N_("Add"),         GTK_STOCK_ADD,         ACTF_WHEREVER | ACTF_ALWAYS,
      N_("Add a new torrent") },
    { N_("Start"),       GTK_STOCK_EXECUTE,     ACTF_WHEREVER | ACTF_INACTIVE,
      N_("Start a torrent that is not running") },
    { N_("Stop"),        GTK_STOCK_STOP,        ACTF_WHEREVER | ACTF_ACTIVE,
      N_("Stop a torrent that is running") },
    { N_("Remove"),      GTK_STOCK_REMOVE,      ACTF_WHEREVER | ACTF_WHATEVER,
      N_("Remove a torrent") },
    { N_("Properties"),  GTK_STOCK_PROPERTIES,  ACTF_WHEREVER | ACTF_WHATEVER,
      N_("Show additional information about a torrent") },
    { N_("Preferences"), GTK_STOCK_PREFERENCES, ACTF_WHEREVER | ACTF_ALWAYS,
      N_("Customize application behavior") },
    { N_("Open debug window"), NULL,            ACTF_MENU     | ACTF_ALWAYS,
      NULL },
};

#define CBDATA_PTR              "callback-data-pointer"

#define SIGCOUNT_MAX            3

static sig_atomic_t global_sigcount = 0;

static GList *
readargs(int argc, char **argv);

static void
makewind(TrWindow *wind, TrBackend *back, benc_val_t *state, GList *args);
static void
quittransmission(struct cbdata *data);
static gboolean
winclose(GtkWidget *widget, GdkEvent *event, gpointer gdata);
static gboolean
exitcheck(gpointer gdata);
static void
setupdrag(GtkWidget *widget, struct cbdata *data);
static void
gotdrag(GtkWidget *widget, GdkDragContext *dc, gint x, gint y,
        GtkSelectionData *sel, guint info, guint time, gpointer gdata);

static gboolean
updatemodel(gpointer gdata);
static void
boolwindclosed(GtkWidget *widget SHUTUP, gpointer gdata);
static void
windact(GtkWidget *widget, int action, gpointer gdata);
static void
handleaction(struct cbdata *data, enum action action);

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
main(int argc, char **argv) {
  GtkWidget *mainwind, *preferr, *stateerr;
  char *err;
  TrBackend *back;
  benc_val_t *state;
  GList *argfiles;
  gboolean didinit, didlock;
  GdkPixbuf * icon;

  safepipe();

  argfiles = readargs(argc, argv);

  didinit = cf_init(tr_getPrefsDirectory(), NULL);
  didlock = FALSE;
  if(NULL != argfiles && didinit && !(didlock = cf_lock(NULL)))
    return !ipc_sendfiles_blocking(argfiles);

  setupsighandlers();

  gtk_init(&argc, &argv);

  bindtextdomain("transmission-gtk", LOCALEDIR);
  bind_textdomain_codeset("transmission-gtk", "UTF-8");
  textdomain("transmission-gtk");

  g_set_application_name(_("Transmission"));
#if 0
  /* this isn't used in transmission-gtk itself, it's for the .desktop file */
  N_("BitTorrent Client");
  /* this too */
  N_("A free, lightweight client with a simple, intuitive interface");
#endif

  gtk_rc_parse_string(
    "style \"transmission-standard\" {\n"
    " GtkDialog::action-area-border = 6\n"
    " GtkDialog::button-spacing = 12\n"
    " GtkDialog::content-area-border = 6\n"
    "}\n"
    "widget \"TransmissionDialog\" style \"transmission-standard\"\n");

  icon = gdk_pixbuf_new_from_inline( -1, tr_icon_full, FALSE, NULL );
  gtk_window_set_default_icon( icon );
  g_object_unref( icon );

  if(didinit || cf_init(tr_getPrefsDirectory(), &err)) {
    if(didlock || cf_lock(&err)) {

      /* create main window now so any error dialogs can be it's children */
      mainwind = tr_window_new();
      preferr = NULL;
      stateerr = NULL;

      cf_loadprefs(&err);
      if(NULL != err) {
        preferr = errmsg(GTK_WINDOW(mainwind), "%s", err);
        g_free(err);
      }
      state = cf_loadstate(&err);
      if(NULL != err) {
        stateerr = errmsg(GTK_WINDOW(mainwind), "%s", err);
        g_free(err);
      }

      /* set libT message level */
      msgwin_loadpref();

      back = tr_backend_new();

      /* apply a few prefs */
      applyprefs(back);

      makewind( TR_WINDOW( mainwind ), back, state, argfiles );

      if(NULL != state)
        cf_freestate(state);
      g_object_unref(back);

      if(NULL != preferr)
        gtk_widget_show_all(preferr);
      if(NULL != stateerr)
        gtk_widget_show_all(stateerr);
    } else {
      gtk_widget_show(errmsg_full(NULL, (callbackfunc_t)gtk_main_quit,
                                  NULL, "%s", err));
      g_free(err);
    }
  } else {
    gtk_widget_show(errmsg_full(NULL, (callbackfunc_t)gtk_main_quit,
                                NULL, "%s", err));
    g_free(err);
  }

  if(NULL != argfiles)
    freestrlist(argfiles);

  gtk_main();

  return 0;
}

GList *
readargs(int argc, char **argv) {
  char *name;

  if(NULL == (name = strrchr(argv[0], '/')) || '\0' == *(++name))
    name = argv[0];

  while(0 < --argc) {
    argv++;
    if(0 == strcmp("--", *argv))
      return checkfilenames(argc - 1, argv + 1);
    else if('-' != argv[0][0])
      return checkfilenames(argc, argv);
    else if(0 == strcmp("-v", *argv) || 0 == strcmp("--version", *argv)) {
      printf("%s %s (%d) http://transmission.m0k.org/\n",
             name, VERSION_STRING, VERSION_REVISION);
      exit(0);
    }
    else if(0 == strcmp("-h", *argv) || 0 == strcmp("--help", *argv)) {
      printf("usage: %1$s [-hv] [files...]\n\n"
"If %1$s is already running then a second copy will not be\n"
"started, any torrents on the command-line will be opened in the first.\n",
             name);
      exit(0);
    }
  }

  return NULL;
}

static void
makewind( TrWindow * wind, TrBackend * back, benc_val_t * state, GList * args)
{
  GType types[] = {
    /* info->name, info->totalSize, status,     error,      errorString, */
    G_TYPE_STRING, G_TYPE_UINT64,   G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING,
    /* progress,  rateDownload, rateUpload,   eta,        peersTotal, */
    G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_INT, G_TYPE_INT,
    /* peersUploading, peersDownloading, downloaded,    uploaded */
    G_TYPE_INT,        G_TYPE_INT,       G_TYPE_UINT64, G_TYPE_UINT64,
    /* the torrent object */
    TR_TORRENT_TYPE};
  struct cbdata *data = g_new0(struct cbdata, 1);
  GtkListStore *store;
  unsigned int ii;
  GtkWidget *drag;

  g_assert(MC_ROW_COUNT == ALEN(types));
  store = gtk_list_store_newv(MC_ROW_COUNT, types);

  g_object_ref(G_OBJECT(back));
  data->back = back;
  data->wind = GTK_WINDOW(wind);
  data->timer = 0;
  data->model = GTK_TREE_MODEL(store);
  data->prefsopen = FALSE;
  data->msgwinopen = FALSE;
  data->closing = FALSE;

  g_assert( ACTION_COUNT == ALEN( actions ) );
  for( ii = 0; ii < ALEN( actions ); ii++ )
  {
      tr_window_action_add( wind, ii, actions[ii].flags,
                            gettext( actions[ii].label ),
                            actions[ii].icon,
                            gettext( actions[ii].tooltip ) );
  }
  g_object_set( wind, "model", data->model,
                      "double-click-action", ACT_INFO, NULL);

  g_signal_connect( wind, "action",       G_CALLBACK( windact  ), data );
  g_signal_connect( wind, "delete_event", G_CALLBACK( winclose ), data );

  g_object_get( wind, "drag-widget", &drag, NULL );
  setupdrag( drag, data );

  addtorrents(data, state, args, NULL, addactionflag(cf_getpref(PREF_ADDIPC)));

  data->timer = g_timeout_add(UPDATE_INTERVAL, updatemodel, data);
  updatemodel(data);

  /* this shows the window */
  tr_window_size_hack( wind );

  /* set up the ipc socket now that we're ready to get torrents from it */
  ipc_socket_setup(GTK_WINDOW(wind), addtorrents, data);
}

static void
quittransmission( struct cbdata * data )
{
    g_object_unref( G_OBJECT( data->back ) );
    if( NULL != data->wind )
    {
        gtk_widget_destroy( GTK_WIDGET( data->wind ) );
    }
    g_object_unref( data->model );
    if( 0 < data->timer )
    {
        g_source_remove( data->timer );
    }
    g_free( data );
    gtk_main_quit();
}

gboolean
winclose(GtkWidget *widget SHUTUP, GdkEvent *event SHUTUP, gpointer gdata) {
  struct cbdata *data = gdata;
  struct exitdata *edata;
  GtkTreeIter iter;
  TrTorrent *tor;

  data->closing = TRUE;

  /* stop the update timer */
  if(0 < data->timer)
    g_source_remove(data->timer);
  data->timer = 0;

  /*
    Add a reference to all torrents in the list, which will be removed
    when the politely-stopped signal is emitted.  This is necessary
    because a reference is added when a torrent is removed
    from the model and tr_torrent_stop_polite() is called on it.
  */
  if(gtk_tree_model_get_iter_first(data->model, &iter)) {
    do
      gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor, -1);
    while(gtk_tree_model_iter_next(data->model, &iter));
  }

  /* try to politely stop all the torrents */
  tr_backend_stop_torrents(data->back);

  /* shut down nat traversal */
  tr_natTraversalEnable(tr_backend_handle(data->back), 0);

  /* set things up to wait for torrents to stop */
  edata = g_new0(struct exitdata, 1);
  edata->cbdata = data;
  edata->started = time(NULL);
  /* check if torrents are still running */
  if(exitcheck(edata)) {
    /* yes, start the exit timer and disable widgets */
    edata->timer = g_timeout_add(EXIT_CHECK_INTERVAL, exitcheck, edata);
    gtk_widget_set_sensitive( GTK_WIDGET( data->wind ), FALSE );
  }

  /* returning FALSE means to destroy the window */
  return TRUE;
}

gboolean
exitcheck(gpointer gdata) {
  struct exitdata *data = gdata;
  tr_handle_status_t * hstat;

  hstat = tr_handleStatus( tr_backend_handle( data->cbdata->back ) );

  /* keep going if we haven't hit the exit timeout and
     we either have torrents left or nat traversal is stopping */
  if( time( NULL ) - data->started < TRACKER_EXIT_TIMEOUT &&
      ( !tr_backend_torrents_stopped( data->cbdata->back ) ||
        TR_NAT_TRAVERSAL_DISABLED != hstat->natTraversalStatus ) ) {
    updatemodel(data->cbdata);
    return TRUE;
  }

  /* exit otherwise */
  if(0 < data->timer)
    g_source_remove(data->timer);
  quittransmission(data->cbdata);
  g_free(data);

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
    if(NULL != paths)
      addtorrents(data, NULL, paths, NULL,
                  addactionflag(cf_getpref(PREF_ADDSTD)));
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

gboolean
updatemodel(gpointer gdata) {
  struct cbdata *data = gdata;
  TrTorrent *tor;
  tr_stat_t *st;
  tr_info_t *in;
  GtkTreeIter iter;
  float up, down;

  if(0 < global_sigcount) {
    quittransmission(data);
    return FALSE;
  }

  if(gtk_tree_model_get_iter_first(data->model, &iter)) {
    do {
      gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor, -1);
      st = tr_torrent_stat(tor);
      in = tr_torrent_info(tor);
      g_object_unref(tor);
      /* XXX find out if setting the same data emits changed signal */
      gtk_list_store_set(GTK_LIST_STORE(data->model), &iter, MC_NAME, in->name,
        MC_SIZE, in->totalSize, MC_STAT, st->status, MC_ERR, st->error,
        MC_TERR, st->errorString, MC_PROG, st->progress,
        MC_DRATE, st->rateDownload, MC_URATE, st->rateUpload, MC_ETA, st->eta,
        MC_PEERS, st->peersTotal, MC_UPEERS, st->peersUploading,
        MC_DPEERS, st->peersDownloading, MC_DOWN, st->downloaded,
        MC_UP, st->uploaded, -1);
    } while(gtk_tree_model_iter_next(data->model, &iter));
  }

  /* update the main window's statusbar and toolbar buttons */
  tr_torrentRates( tr_backend_handle( data->back ), &down, &up );
  tr_window_update( TR_WINDOW(data->wind), down, up );

  /* check for politely stopped torrents unless we're exiting */
  if(!data->closing)
    tr_backend_torrents_stopped(data->back);

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

static void
handleaction( struct cbdata * data, enum action act )
{
  GtkTreeSelection *sel;
  GList *rows, *ii;
  GtkTreeRowReference *ref;
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
          if( !data->prefsopen )
          {
              data->prefsopen = TRUE;
              win = makeprefwindow( data->wind, data->back );
              g_signal_connect( win, "destroy", G_CALLBACK( boolwindclosed ),
                                &data->prefsopen );
          }
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
      case ACT_START:
      case ACT_STOP:
      case ACT_DELETE:
      case ACT_INFO:
      case ACTION_COUNT:
          break;
  }

  /* get a list of references to selected rows */
  g_object_get( data->wind, "selection", &sel, NULL );
  rows = gtk_tree_selection_get_selected_rows( sel, NULL );
  for(ii = rows; NULL != ii; ii = ii->next) {
    ref = gtk_tree_row_reference_new(data->model, ii->data);
    gtk_tree_path_free(ii->data);
    ii->data = ref;
  }

  changed = FALSE;
  for(ii = rows; NULL != ii; ii = ii->next) {
    if(NULL != (path = gtk_tree_row_reference_get_path(ii->data)) &&
       gtk_tree_model_get_iter(data->model, &iter, path)) {
      gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor,
                         MC_STAT, &status, -1);
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
                  /* tor will be unref'd in the politely_stopped handler */
                  g_object_ref( tor );
                  tr_torrent_stop_politely( tor );
                  if( TR_FLAG_SAVE & tr_torrent_info( tor )->flags )
                  {
                      tr_torrentRemoveSaved( tr_torrent_handle( tor ) );
                  }
                  gtk_list_store_remove( GTK_LIST_STORE( data->model ),
                                         &iter );
                  changed = TRUE;
                  break;
              case ACT_INFO:
                  makeinfowind( data->wind, tor );
                  break;
              case ACT_OPEN:
              case ACT_PREF:
              case ACT_DEBUG:
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

static const char *
defaultdir( void )
{
    static char * wd = NULL;
    const char  * dir;

    dir = cf_getpref( PREF_DIR );
    if( NULL == dir )
    {
        if( NULL == wd )
        {
            wd = g_new( char, MAX_PATH_LENGTH + 1 );
            if( NULL == getcwd( wd, MAX_PATH_LENGTH + 1 ) )
            {
                strcpy( wd, "." );
            }
        }
        dir = wd;
    }

    return dir;
}

static void
addtorrents(void *vdata, void *state, GList *files,
            const char *dir, guint flags) {
  struct cbdata *data = vdata;
  GList *torlist, *errlist, *ii;
  char *errstr;
  TrTorrent *tor;
  GtkTreeIter iter;
  const char * pref;

  errlist = NULL;
  torlist = NULL;

  if(NULL != state)
    torlist = tr_backend_load_state(data->back, state, &errlist);

  if(NULL != files) {
    if( NULL == dir )
    {
        pref = cf_getpref( PREF_ASKDIR );
        if( NULL != pref && strbool( pref ) )
        {
            promptfordir( data->wind, addtorrents, data,
                          files, flags, defaultdir() );
            files = NULL;
        }
        dir = defaultdir();
    }
    for(ii = g_list_first(files); NULL != ii; ii = ii->next) {
      errstr = NULL;
      tor = tr_torrent_new(G_OBJECT(data->back), ii->data, dir,
                           flags, &errstr);
      if(NULL != tor)
        torlist = g_list_append(torlist, tor);
      if(NULL != errstr)
        errlist = g_list_append(errlist, errstr);
    }
  }

  for(ii = g_list_first(torlist); NULL != ii; ii = ii->next) {
    gtk_list_store_append(GTK_LIST_STORE(data->model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(data->model), &iter,
                       MC_TORRENT, ii->data, -1);
    /* we will always ref a torrent before politely stopping it */
    g_signal_connect(ii->data, "politely_stopped",
                     G_CALLBACK(g_object_unref), data);
    g_object_unref(ii->data);
  }

  if(NULL != errlist) {
    errstr = joinstrlist(errlist, "\n");
    errmsg(data->wind, ngettext("Failed to load torrent file:\n%s",
                                "Failed to load torrent files:\n%s",
                                g_list_length(errlist)), errstr);
    g_list_foreach(errlist, (GFunc)g_free, NULL);
    g_list_free(errlist);
    g_free(errstr);
  }

  if(NULL != torlist) {
    updatemodel(data);
    savetorrents(data);
  }
}

static void
savetorrents( struct cbdata *data )
{
    char * errstr;

    tr_backend_save_state( data->back, &errstr );
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
  int sigs[] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGUSR1, SIGUSR2};
  struct sigaction sa;
  unsigned int ii;

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
