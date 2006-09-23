/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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
#include "transmission.h"
#include "util.h"

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
  GtkTreeView *view;
  GtkStatusbar *bar;
  GtkWidget **buttons;
  guint timer;
  gboolean prefsopen;
  gboolean msgwinopen;
  GtkWidget *stupidpopuphack;
  gboolean closing;
};

struct exitdata {
  struct cbdata *cbdata;
  time_t started;
  guint timer;
};

GList *
readargs(int argc, char **argv);

void
makewind(GtkWidget *wind, TrBackend *back, benc_val_t *state, GList *args);
void
quittransmission(struct cbdata *data);
GtkWidget *
makewind_toolbar(struct cbdata *data);
GtkWidget *
makewind_list(struct cbdata *data, GObject **sizehack);
gboolean
winclose(GtkWidget *widget, GdkEvent *event, gpointer gdata);
gboolean
exitcheck(gpointer gdata);
void
setupdrag(GtkWidget *widget, struct cbdata *data);
void
gotdrag(GtkWidget *widget, GdkDragContext *dc, gint x, gint y,
        GtkSelectionData *sel, guint info, guint time, gpointer gdata);
static void
stylekludge(GObject *obj, GParamSpec *spec, gpointer gdata);
void
fixbuttons(GtkTreeSelection *sel, gpointer gdata);
void
dfname(GtkTreeViewColumn *col, GtkCellRenderer *rend, GtkTreeModel *model,
       GtkTreeIter *iter, gpointer gdata);
void
dfprog(GtkTreeViewColumn *col, GtkCellRenderer *rend, GtkTreeModel *model,
       GtkTreeIter *iter, gpointer gdata);

gboolean
updatemodel(gpointer gdata);
gboolean
listclick(GtkWidget *widget, GdkEventButton *event, gpointer gdata);
gboolean
listpopup(GtkWidget *widget, gpointer gdata);
void
dopopupmenu(GdkEventButton *event, struct cbdata *data);
void
boolwindclosed(GtkWidget *widget SHUTUP, gpointer gdata);
void
actionclick(GtkWidget *widget, gpointer gdata);
gint
intrevcmp(gconstpointer a, gconstpointer b);
void
doubleclick(GtkWidget *widget, GtkTreePath *path, GtkTreeViewColumn *col,
            gpointer gdata);

void
addtorrents(void *vdata, void *state, GList *files,
            const char *dir, guint flags);
void
savetorrents(struct cbdata *data);
void
orstatus(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
         gpointer gdata);
void
istorsel(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
         gpointer gdata);
void
safepipe(void);
void
setupsighandlers(void);
void
fatalsig(int sig);

#define LIST_ACTION           "torrent-list-action"
enum listact {
  ACT_OPEN, ACT_START, ACT_STOP, ACT_DELETE, ACT_INFO, ACT_PREF, ACT_DEBUG };

struct { const gchar *name; const gchar *id; enum listact act; gboolean nomenu;
  int avail; const char *ttext; const char *tpriv; }
actionitems[] = {
  {N_("Add"),         GTK_STOCK_ADD,          ACT_OPEN,   FALSE,  0,
   N_("Add a new torrent"), "XXX"},
  {N_("Start"),       GTK_STOCK_EXECUTE,      ACT_START,  FALSE,
   TR_STATUS_INACTIVE,
   N_("Start a torrent that is not running"), "XXX"},
  {N_("Stop"),        GTK_STOCK_STOP,         ACT_STOP,   FALSE,
   TR_STATUS_ACTIVE,
   N_("Stop a torrent that is running"), "XXX"},
  {N_("Remove"),      GTK_STOCK_REMOVE,       ACT_DELETE, FALSE, ~0,
   N_("Remove a torrent"), "XXX"},
  {N_("Properties"),  GTK_STOCK_PROPERTIES,   ACT_INFO,   FALSE, ~0,
   N_("Show additional information about a torrent"), "XXX"},
  {N_("Preferences"), GTK_STOCK_PREFERENCES,  ACT_PREF,   TRUE,   0,
   N_("Customize application behavior"), "XXX"},
  {N_("Open debug window"), NULL, ACT_DEBUG,  FALSE,  0, NULL, NULL},
};

#define CBDATA_PTR              "callback-data-pointer"

#define SIGCOUNT_MAX            3

static sig_atomic_t global_sigcount = 0;

int
main(int argc, char **argv) {
  GtkWidget *mainwind, *preferr, *stateerr;
  char *err;
  TrBackend *back;
  benc_val_t *state;
  const char *pref;
  long intval;
  GList *argfiles;
  gboolean didinit, didlock;

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

  gtk_rc_parse_string(
    "style \"transmission-standard\" {\n"
    " GtkDialog::action-area-border = 6\n"
    " GtkDialog::button-spacing = 12\n"
    " GtkDialog::content-area-border = 6\n"
    "}\n"
    "widget \"TransmissionDialog\" style \"transmission-standard\"\n");

  if(didinit || cf_init(tr_getPrefsDirectory(), &err)) {
    if(didlock || cf_lock(&err)) {

      /* create main window now so any error dialogs can be it's children */
      mainwind = gtk_window_new(GTK_WINDOW_TOPLEVEL);
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

      /* set the upload limit */
      setlimit(back);

      /* set the listening port */
      if(NULL != (pref = cf_getpref(PREF_PORT)) &&
         0 < (intval = strtol(pref, NULL, 10)) && 0xffff >= intval)
        tr_setBindPort(tr_backend_handle(back), intval);

      makewind(mainwind, back, state, argfiles);

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

void
makewind(GtkWidget *wind, TrBackend *back, benc_val_t *state, GList *args) {
  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  GtkWidget *status = gtk_statusbar_new();
  struct cbdata *data = g_new0(struct cbdata, 1);
  GtkWidget *list;
  GtkRequisition req;
  gint width, height;
  GObject *sizehack;
  GdkScreen *screen;

  g_object_ref(G_OBJECT(back));
  data->back = back;
  data->wind = GTK_WINDOW(wind);
  data->timer = 0;
  /* filled in by makewind_list */
  data->model = NULL;
  data->view = NULL;
  data->bar = GTK_STATUSBAR(status);
  data->buttons = NULL;
  data->prefsopen = FALSE;
  data->msgwinopen = FALSE;
  data->stupidpopuphack = NULL;
  data->closing = FALSE;

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_ALWAYS);

  gtk_box_pack_start(GTK_BOX(vbox), makewind_toolbar(data), FALSE, FALSE, 0);

  sizehack = NULL;
  list = makewind_list(data, &sizehack);
  gtk_container_add(GTK_CONTAINER(scroll), list);
  gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

  gtk_statusbar_push(GTK_STATUSBAR(status), 0, "");
  gtk_box_pack_start(GTK_BOX(vbox), status, FALSE, FALSE, 0);

  gtk_container_set_focus_child(GTK_CONTAINER(vbox), scroll);
  gtk_container_add(GTK_CONTAINER(wind), vbox);
  gtk_window_set_title(data->wind, g_get_application_name());
  g_signal_connect(G_OBJECT(wind), "delete_event", G_CALLBACK(winclose), data);

  setupdrag(list, data);

  addtorrents(data, state, args, NULL, addactionflag(cf_getpref(PREF_ADDIPC)));

  data->timer = g_timeout_add(UPDATE_INTERVAL, updatemodel, data);
  updatemodel(data);

  /* some evil magic to try to get a nice initial window size */
  gtk_widget_show_all(vbox);
  gtk_widget_realize(wind);
  gtk_widget_size_request(list, &req);
  height = req.height;
  gtk_widget_size_request(scroll, &req);
  height -= req.height;
  gtk_widget_size_request(wind, &req);
  height += req.height;
  screen = gtk_widget_get_screen(wind);
  width = MIN(req.width, gdk_screen_get_width(screen) / 2);
  height = MIN(height, gdk_screen_get_height(screen) / 5 * 4);
  if(height > req.width)
    height = MIN(height, width * 8 / 5);
  else
    height = MAX(height, width * 5 / 8);
  height = (height > req.width ?
            MIN(height, width * 8 / 5) : MAX(height, width * 5 / 8));
  g_object_set(sizehack, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_widget_show_now(wind);
  gtk_window_resize(GTK_WINDOW(wind), width, height);

  /* set up the ipc socket now that we're ready to get torrents from it */
  ipc_socket_setup(GTK_WINDOW(wind), addtorrents, data);
}

void
quittransmission(struct cbdata *data) {
  g_object_unref(G_OBJECT(data->back));
  gtk_widget_destroy(GTK_WIDGET(data->wind));
  if(0 < data->timer)
    g_source_remove(data->timer);
  if(NULL != data->stupidpopuphack)
    gtk_widget_destroy(data->stupidpopuphack);
  g_free(data->buttons);
  g_free(data);
  gtk_main_quit();
}

GtkWidget *
makewind_toolbar(struct cbdata *data) {
  GtkWidget *bar = gtk_toolbar_new();
  GtkToolItem *item;
  unsigned int ii;

  gtk_toolbar_set_tooltips(GTK_TOOLBAR(bar), TRUE);
  gtk_toolbar_set_show_arrow(GTK_TOOLBAR(bar), FALSE);

  data->buttons = g_new(GtkWidget*, ALEN(actionitems));

  for(ii = 0; ii < ALEN(actionitems); ii++) {
    if( NULL == actionitems[ii].id ) {
      data->buttons[ii] = NULL;
      continue;
    }
    item = gtk_tool_button_new_from_stock(actionitems[ii].id);
    data->buttons[ii] = GTK_WIDGET(item);
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(item),
                              gettext(actionitems[ii].name));
    gtk_tool_item_set_tooltip(GTK_TOOL_ITEM(item), GTK_TOOLBAR(bar)->tooltips,
                              gettext(actionitems[ii].ttext),
                              actionitems[ii].tpriv);
    g_object_set_data(G_OBJECT(item), LIST_ACTION,
                      GINT_TO_POINTER(actionitems[ii].act));
    g_signal_connect(G_OBJECT(item), "clicked", G_CALLBACK(actionclick), data);
    gtk_toolbar_insert(GTK_TOOLBAR(bar), item, -1);
  }

  return bar;
}

/* XXX check for unused data in model */
enum {
  MC_NAME, MC_SIZE, MC_STAT, MC_ERR, MC_TERR,
  MC_PROG, MC_DRATE, MC_URATE, MC_ETA, MC_PEERS,
  MC_UPEERS, MC_DPEERS, MC_DOWN, MC_UP,
  MC_TORRENT, MC_ROW_COUNT,
};

GtkWidget *
makewind_list(struct cbdata *data, GObject **sizehack) {
  GType types[] = {
    /* info->name, info->totalSize, status,     error,      trackerError, */
    G_TYPE_STRING, G_TYPE_UINT64,   G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING,
    /* progress,  rateDownload, rateUpload,   eta,        peersTotal, */
    G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_INT, G_TYPE_INT,
    /* peersUploading, peersDownloading, downloaded,    uploaded */
    G_TYPE_INT,        G_TYPE_INT,       G_TYPE_UINT64, G_TYPE_UINT64,
    /* the torrent object */
    TR_TORRENT_TYPE};
  GtkListStore *store;
  GtkWidget *view;
  GtkTreeViewColumn *col;
  GtkTreeSelection *sel;
  GtkCellRenderer *namerend, *progrend;
  char *str;

  g_assert(MC_ROW_COUNT == ALEN(types));

  store = gtk_list_store_newv(MC_ROW_COUNT, types);
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  /* XXX do I need to worry about reference counts anywhere else? */
  g_object_unref(G_OBJECT(store));
  data->model = GTK_TREE_MODEL(store);
  data->view = GTK_TREE_VIEW(view);

  namerend = gtk_cell_renderer_text_new();
  *sizehack = G_OBJECT(namerend);
  /* note that this renderer is set to ellipsize, just not here */
  col = gtk_tree_view_column_new_with_attributes(_("Name"), namerend, NULL);
  gtk_tree_view_column_set_cell_data_func(col, namerend, dfname, NULL, NULL);
  gtk_tree_view_column_set_expand(col, TRUE);
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  progrend = tr_cell_renderer_progress_new();
  /* this string is only used to determing the size of the progress bar */
  str = g_markup_printf_escaped("<big>%s</big>", _("  fnord    fnord  "));
  g_object_set(progrend, "bar-sizing", str, NULL);
  g_free(str);
  col = gtk_tree_view_column_new_with_attributes(_("Progress"), progrend, NULL);
  gtk_tree_view_column_set_cell_data_func(col, progrend, dfprog, NULL, NULL);
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  /* XXX this shouldn't be necessary */
  g_signal_connect(view, "notify", G_CALLBACK(stylekludge), progrend);

  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);
  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_set_mode(GTK_TREE_SELECTION(sel), GTK_SELECTION_MULTIPLE);
  g_signal_connect(G_OBJECT(sel), "changed", G_CALLBACK(fixbuttons), data);
  g_signal_connect(G_OBJECT(view), "button-press-event",
                   G_CALLBACK(listclick), data);
  g_signal_connect(G_OBJECT(view), "popup-menu", G_CALLBACK(listpopup), data);
  g_signal_connect(G_OBJECT(view), "row-activated",
                   G_CALLBACK(doubleclick), data);
  gtk_widget_show_all(view);

  return view;
}

gboolean
winclose(GtkWidget *widget SHUTUP, GdkEvent *event SHUTUP, gpointer gdata) {
  struct cbdata *data = gdata;
  struct exitdata *edata;
  unsigned int ii;
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
    because actionclick() adds a reference when it removes a torrent
    from the model and calls tr_torrent_stop_polite() on it.
  */
  if(gtk_tree_model_get_iter_first(data->model, &iter)) {
    do
      gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor, -1);
    while(gtk_tree_model_iter_next(data->model, &iter));
  }

  /* try to politely stop all the torrents */
  tr_backend_stop_torrents(data->back);

  /* set things up to wait for torrents to stop */
  edata = g_new0(struct exitdata, 1);
  edata->cbdata = data;
  edata->started = time(NULL);
  /* check if torrents are still running */
  if(exitcheck(edata)) {
    /* yes, start the exit timer and disable widgets */
    edata->timer = g_timeout_add(EXIT_CHECK_INTERVAL, exitcheck, edata);
    for(ii = 0; ii < ALEN(actionitems); ii++)
      if( NULL != data->buttons[ii] )
        gtk_widget_set_sensitive(data->buttons[ii], FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(data->view), FALSE);
  }

  /* returning FALSE means to destroy the window */
  return TRUE;
}

gboolean
exitcheck(gpointer gdata) {
  struct exitdata *data = gdata;

  /* keep going if we still have torrents and haven't hit the exit timeout */
  if( time( NULL ) - data->started < TRACKER_EXIT_TIMEOUT &&
     ( !tr_backend_torrents_stopped( data->cbdata->back ) ||
       TR_NAT_TRAVERSAL_DISABLED != natstat ) ) {
    updatemodel( data->cbdata );
    return TRUE;
  }

  /* exit otherwise */
  if(0 < data->timer)
    g_source_remove(data->timer);
  quittransmission(data->cbdata);
  g_free(data);

  return FALSE;
}

void
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

void
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

/* kludge to have the progress bars notice theme changes */
static void
stylekludge(GObject *obj, GParamSpec *spec, gpointer gdata) {
  if(0 == strcmp("style", spec->name)) {
    tr_cell_renderer_progress_reset_style(TR_CELL_RENDERER_PROGRESS(gdata));
    gtk_widget_queue_draw(GTK_WIDGET(obj));
  }
}

/* disable buttons the user shouldn't be able to click on */
void
fixbuttons(GtkTreeSelection *sel, gpointer gdata) {
  struct cbdata *data = gdata;
  gboolean selected;
  unsigned int ii;
  int status;

  if(NULL == sel)
    sel = gtk_tree_view_get_selection(data->view);
  status = 0;
  gtk_tree_selection_selected_foreach(sel, orstatus, &status);
  selected = (0 < gtk_tree_selection_count_selected_rows(sel));

  for(ii = 0; ii < ALEN(actionitems); ii++)
    if(actionitems[ii].avail)
      gtk_widget_set_sensitive(data->buttons[ii],
                               (selected && (actionitems[ii].avail & status)));
}

void
dfname(GtkTreeViewColumn *col SHUTUP, GtkCellRenderer *rend,
       GtkTreeModel *model, GtkTreeIter *iter, gpointer gdata SHUTUP) {
  char *name, *mb, *terr, *str, *top, *bottom, *timestr;
  guint64 size;
  gfloat prog;
  int status, err, eta, tpeers, upeers, dpeers;

  gtk_tree_model_get(model, iter, MC_NAME, &name, MC_STAT, &status,
    MC_ERR, &err, MC_SIZE, &size, MC_PROG, &prog, MC_ETA, &eta,
    MC_PEERS, &tpeers, MC_UPEERS, &upeers, MC_DPEERS, &dpeers, -1);

  if(0 > tpeers)
    tpeers = 0;
  if(0 > upeers)
    upeers = 0;
  if(0 > dpeers)
    dpeers = 0;
  mb = readablesize(size);
  prog *= 100;

  if(status & TR_STATUS_CHECK)
    top = g_strdup_printf(_("Checking existing files (%.1f%%)"), prog);
  else if(status & TR_STATUS_DOWNLOAD) {
    if(0 > eta)
      top = g_strdup_printf(_("Stalled (%.1f%%)"), prog);
    else {
      timestr = readabletime(eta);
      top = g_strdup_printf(_("Finishing in %s (%.1f%%)"), timestr, prog);
      g_free(timestr);
    }
  }
  else if(status & TR_STATUS_SEED)
    top = g_strdup_printf(ngettext("Seeding, uploading to %d of %d peer",
                                   "Seeding, uploading to %d of %d peers",
                                   tpeers), dpeers, tpeers);
  else if(status & TR_STATUS_STOPPING)
    top = g_strdup(_("Stopping..."));
  else if(status & TR_STATUS_PAUSE)
    top = g_strdup_printf(_("Stopped (%.1f%%)"), prog);
  else {
    top = g_strdup("");
    g_assert_not_reached();
  }

  if(TR_NOERROR != err) {
    gtk_tree_model_get(model, iter, MC_TERR, &terr, -1);
    bottom = g_strconcat(_("Error: "), terr, NULL);
    g_free(terr);
  }
  else if(status & TR_STATUS_DOWNLOAD)
    bottom = g_strdup_printf(ngettext("Downloading from %i of %i peer",
                                      "Downloading from %i of %i peers",
                                      tpeers), upeers, tpeers);
  else
    bottom = NULL;

  str = g_markup_printf_escaped("<big>%s (%s)</big>\n<small>%s\n%s</small>",
                                name, mb, top, (NULL == bottom ? "" : bottom));
  g_object_set(rend, "markup", str, NULL);
  g_free(name);
  g_free(mb);
  g_free(str);
  g_free(top);
  if(NULL != bottom)
    g_free(bottom);
}

void
dfprog(GtkTreeViewColumn *col SHUTUP, GtkCellRenderer *rend,
       GtkTreeModel *model, GtkTreeIter *iter, gpointer gdata SHUTUP) {
  char *dlstr, *ulstr, *str, *marked;
  gfloat prog, dl, ul;
  guint64 down, up;

  gtk_tree_model_get(model, iter, MC_PROG, &prog, MC_DRATE, &dl, MC_URATE, &ul,
                     MC_DOWN, &down, MC_UP, &up, -1);
  if(0.0 > prog)
    prog = 0.0;
  else if(1.0 < prog)
    prog = 1.0;

  ulstr = readablesize(ul * 1024.0);
  if(1.0 == prog) {
    dlstr = ratiostr(down, up);
    str = g_strdup_printf(_("Ratio: %s\nUL: %s/s"), dlstr, ulstr);
  } else {
    dlstr = readablesize(dl * 1024.0);
    str = g_strdup_printf(_("DL: %s/s\nUL: %s/s"), dlstr, ulstr);
  }
  marked = g_markup_printf_escaped("<small>%s</small>", str);
  g_object_set(rend, "markup", str, "progress", prog, NULL);
  g_free(dlstr);
  g_free(ulstr);
  g_free(str);
  g_free(marked);
}

gboolean
updatemodel(gpointer gdata) {
  struct cbdata *data = gdata;
  TrTorrent *tor;
  tr_stat_t *st;
  tr_info_t *in;
  GtkTreeIter iter;
  float up, down;
  char *upstr, *downstr, *str;

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
        MC_TERR, st->trackerError, MC_PROG, st->progress,
        MC_DRATE, st->rateDownload, MC_URATE, st->rateUpload, MC_ETA, st->eta,
        MC_PEERS, st->peersTotal, MC_UPEERS, st->peersUploading,
        MC_DPEERS, st->peersDownloading, MC_DOWN, st->downloaded,
        MC_UP, st->uploaded, -1);
    } while(gtk_tree_model_iter_next(data->model, &iter));
  }

  /* update the status bar */
  tr_torrentRates(tr_backend_handle(data->back), &up, &down);
  downstr = readablesize(down * 1024.0);
  upstr = readablesize(up * 1024.0);
  str = g_strdup_printf(_("     Total DL: %s/s     Total UL: %s/s"),
                        upstr, downstr);
  gtk_statusbar_pop(data->bar, 0);
  gtk_statusbar_push(data->bar, 0, str);
  g_free(str);
  g_free(upstr);
  g_free(downstr);

  /* the status of the selected item may have changed, so update the buttons */
  fixbuttons(NULL, data);

  /* check for politely stopped torrents unless we're exiting */
  if(!data->closing)
    tr_backend_torrents_stopped(data->back);

  /* update the message window */
  msgwin_update();

  return TRUE;
}

/* show a popup menu for a right-click on the list */
gboolean
listclick(GtkWidget *widget SHUTUP, GdkEventButton *event, gpointer gdata) {
  struct cbdata *data = gdata;
  GtkTreeSelection *sel = gtk_tree_view_get_selection(data->view);
  GtkTreePath *path;
  GtkTreeIter iter;
  int status;
  TrTorrent *tor, *issel;

  if(GDK_BUTTON_PRESS == event->type && 3 == event->button) {
    /* find what row, if any, the user clicked on */
    if(!gtk_tree_view_get_path_at_pos(data->view, event->x, event->y, &path,
                                      NULL, NULL, NULL))
      gtk_tree_selection_unselect_all(sel);
    else {
      if(gtk_tree_model_get_iter(data->model, &iter, path)) {
        /* get torrent and status for the right-clicked row */
        gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor,
                           MC_STAT, &status, -1);
        issel = tor;
        gtk_tree_selection_selected_foreach(sel, istorsel, &issel);
        g_object_unref(tor);
        /* if the clicked row isn't selected, select only it */
        if(NULL != issel) {
          gtk_tree_selection_unselect_all(sel);
          gtk_tree_selection_select_iter(sel, &iter);
        }
      }
      gtk_tree_path_free(path);
    }
    dopopupmenu(event, data);
    return TRUE;
  }

  return FALSE;
}

gboolean
listpopup(GtkWidget *widget SHUTUP, gpointer gdata) {
  dopopupmenu(NULL, gdata);
  return TRUE;
}

void
dopopupmenu(GdkEventButton *event, struct cbdata *data) {
  GtkTreeSelection *sel = gtk_tree_view_get_selection(data->view);
  int count = gtk_tree_selection_count_selected_rows(sel);
  GtkWidget *menu = gtk_menu_new();
  GtkWidget *item;
  unsigned int ii;
  int status = 0;

  if(NULL != data->stupidpopuphack)
    gtk_widget_destroy(data->stupidpopuphack);
  data->stupidpopuphack = menu;

  status = 0;
  gtk_tree_selection_selected_foreach(sel, orstatus, &status);

  for(ii = 0; ii < ALEN(actionitems); ii++) {
    if(actionitems[ii].nomenu ||
       (actionitems[ii].avail &&
        (0 == count || !(actionitems[ii].avail & status))))
      continue;
    item = gtk_menu_item_new_with_label(gettext(actionitems[ii].name));
    /* set the action for the menu item */
    g_object_set_data(G_OBJECT(item), LIST_ACTION,
                      GINT_TO_POINTER(actionitems[ii].act));
    g_signal_connect(G_OBJECT(item), "activate",
                     G_CALLBACK(actionclick), data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  }

  gtk_widget_show_all(menu);

  gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                 (NULL == event ? 0 : event->button),
                 gdk_event_get_time((GdkEvent*)event));
}

void
boolwindclosed(GtkWidget *widget SHUTUP, gpointer gdata) {
  gboolean *preachy_gcc = gdata;
  
  *preachy_gcc = FALSE;
}

void
actionclick(GtkWidget *widget, gpointer gdata) {
  struct cbdata *data = gdata;
  enum listact act;
  GtkTreeSelection *sel;
  GList *rows, *ii;
  GtkTreeRowReference *ref;
  GtkTreePath *path;
  GtkTreeIter iter;
  TrTorrent *tor;
  unsigned int actoff, status;
  gboolean changed;
  GtkWidget * win;

  act = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), LIST_ACTION));

  switch(act) {
    case ACT_OPEN:
      makeaddwind(data->wind, addtorrents, data);
      return;
    case ACT_PREF:
      if( !data->prefsopen ) {
        data->prefsopen = TRUE;
        win = makeprefwindow( data->wind, data->back );
        g_signal_connect( win, "destroy", G_CALLBACK( boolwindclosed ),
                          &data->prefsopen );
      }
      return;
    case ACT_DEBUG:
      if( !data->msgwinopen ) {
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
      break;
  }

  sel = gtk_tree_view_get_selection(data->view);
  rows = gtk_tree_selection_get_selected_rows(sel, NULL);

  for(ii = rows; NULL != ii; ii = ii->next) {
    ref = gtk_tree_row_reference_new(data->model, ii->data);
    gtk_tree_path_free(ii->data);
    ii->data = ref;
  }

  for(actoff = 0; actoff < ALEN(actionitems); actoff++)
    if(actionitems[actoff].act == act)
      break;
  g_assert(actoff < ALEN(actionitems));

  changed = FALSE;
  for(ii = rows; NULL != ii; ii = ii->next) {
    if(NULL != (path = gtk_tree_row_reference_get_path(ii->data)) &&
       gtk_tree_model_get_iter(data->model, &iter, path)) {
      gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor,
                         MC_STAT, &status, -1);
      /* check if this action is valid for this torrent */
      if((!actionitems[actoff].avail || actionitems[actoff].avail & status) &&
         !actionitems[actoff].nomenu) {
        switch(act) {
          case ACT_START:
            tr_torrentStart(tr_torrent_handle(tor));
            changed = TRUE;
            break;
          case ACT_STOP:
            tr_torrentStop(tr_torrent_handle(tor));
            changed = TRUE;
            break;
          case ACT_DELETE:
            /* tor will be unref'd in the politely_stopped signal handler */
            g_object_ref(tor);
            tr_torrent_stop_politely(tor);
            if(TR_FSAVEPRIVATE & tr_torrent_info(tor)->flags)
              tr_torrentRemoveSaved(tr_torrent_handle(tor));
            gtk_list_store_remove(GTK_LIST_STORE(data->model), &iter);
            changed = TRUE;
            break;
          case ACT_INFO:
            makeinfowind(data->wind, tor);
            break;
          case ACT_OPEN:
          case ACT_PREF:
          case ACT_DEBUG:
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

gint
intrevcmp(gconstpointer a, gconstpointer b) {
  int aint = GPOINTER_TO_INT(a);
  int bint = GPOINTER_TO_INT(b);

  if(bint > aint)
    return 1;
  else if(bint < aint)
    return -1;
  else
    return 0;
}

void
doubleclick(GtkWidget *widget SHUTUP, GtkTreePath *path,
            GtkTreeViewColumn *col SHUTUP, gpointer gdata) {
  struct cbdata *data = gdata;
  GtkTreeIter iter;
  TrTorrent *tor;

  if(gtk_tree_model_get_iter(data->model, &iter, path)) {
    gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor, -1);
    makeinfowind(data->wind, tor);
    g_object_unref(tor);
  }
}

void
addtorrents(void *vdata, void *state, GList *files,
            const char *dir, guint flags) {
  struct cbdata *data = vdata;
  GList *torlist, *errlist, *ii;
  char *errstr;
  TrTorrent *tor;
  GtkTreeIter iter;
  char *wd;

  errlist = NULL;
  torlist = NULL;

  if(NULL != state)
    torlist = tr_backend_load_state(data->back, state, &errlist);

  if(NULL != files) {
    if(NULL == dir)
      dir = cf_getpref(PREF_DIR);
    wd = NULL;
    if(NULL == dir) {
      wd = g_new(char, MAX_PATH_LENGTH + 1);
      if(NULL == getcwd(wd, MAX_PATH_LENGTH + 1))
        dir = ".";
      else
        dir = wd;
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
    if(NULL != wd)
      g_free(wd);
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

void
savetorrents(struct cbdata *data) {
  char *errstr;

  tr_backend_save_state(data->back, &errstr);
  if(NULL != errstr) {
    errmsg(data->wind, "%s", errstr);
    g_free(errstr);
  }
}

/* use with gtk_tree_selection_selected_foreach to | status of selected rows */
void
orstatus(GtkTreeModel *model, GtkTreePath *path SHUTUP, GtkTreeIter *iter,
         gpointer gdata) {
  int *allstatus = gdata;
  int status;

  gtk_tree_model_get(model, iter, MC_STAT, &status, -1);
  *allstatus |= status;
}

/* data should be a TrTorrent**, will set torrent to NULL if it's selected */
void
istorsel(GtkTreeModel *model, GtkTreePath *path SHUTUP, GtkTreeIter *iter,
         gpointer gdata) {
  TrTorrent **torref = gdata;
  TrTorrent *tor;

  if(NULL != *torref) {
    gtk_tree_model_get(model, iter, MC_TORRENT, &tor, -1);
    if(tor == *torref)
      *torref = NULL;
    g_object_unref(tor);
  }
}

void
safepipe(void) {
  struct sigaction sa;

  bzero(&sa, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);
}

void
setupsighandlers(void) {
  int sigs[] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGUSR1, SIGUSR2};
  struct sigaction sa;
  unsigned int ii;

  bzero(&sa, sizeof(sa));
  sa.sa_handler = fatalsig;
  for(ii = 0; ii < ALEN(sigs); ii++)
    sigaction(sigs[ii], &sa, NULL);
}

void
fatalsig(int sig) {
  struct sigaction sa;

  if(SIGCOUNT_MAX <= ++global_sigcount) {
    bzero(&sa, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, NULL);
    raise(sig);
  }
}
