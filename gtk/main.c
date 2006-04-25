/*
  Copyright (c) 2005-2006 Joshua Elsasser. All rights reserved.
   
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   
  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

#include <sys/param.h>
#include <assert.h>
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
#include "transmission.h"
#include "trcellrenderertorrent.h"
#include "util.h"

#define TRACKER_EXIT_TIMEOUT    5

struct cbdata {
  tr_handle_t *tr;
  GtkWindow *wind;
  GtkTreeModel *model;
  GtkTreeView *view;
  GtkStatusbar *bar;
  GtkWidget **buttons;
  guint timer;
  gboolean prefsopen;
};

struct exitdata {
  struct cbdata *cbdata;
  time_t started;
  guint timer;
};

struct pieces {
  char p[120];
};

GList *
readargs(int argc, char **argv);

void
maketypes(void);
gpointer
tr_pieces_copy(gpointer);
void
tr_pieces_free(gpointer);

void
makewind(GtkWidget *wind, tr_handle_t *tr, GList *saved, GList *args);
GtkWidget *
makewind_toolbar(struct cbdata *data);
GtkWidget *
makewind_list(struct cbdata *data);
gboolean
winclose(GtkWidget *widget, GdkEvent *event, gpointer gdata);
gboolean
exitcheck(gpointer gdata);
void
stoptransmission(tr_handle_t *tr);
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
dopopupmenu(GdkEventButton *event, struct cbdata *data,
            GList *ids, int status);
void
killmenu(GtkWidget *menu, gpointer *gdata);
void
actionclick(GtkWidget *widget, gpointer gdata);
void
findtorrent(GtkTreeModel *model, tr_torrent_t *tor, GtkTreeIter *iter);
gint
intrevcmp(gconstpointer a, gconstpointer b);
void
doubleclick(GtkWidget *widget, GtkTreePath *path, GtkTreeViewColumn *col,
            gpointer gdata);

gboolean
addtorrent(void *vdata, const char *torrent, const char *dir, gboolean paused,
           GList **errs);
void
addedtorrents(void *vdata);
gboolean
savetorrents(tr_handle_t *tr, GtkWindow *wind);
void
orstatus(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
         gpointer gdata);
void
makeidlist(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
           gpointer gdata);
void
maketorrentlist(tr_torrent_t *tor, void *data);
void
safepipe(void);
void
setupsighandlers(void);
void
fatalsig(int sig);

#define TR_TYPE_PIECES_NAME     "tr-type-pieces"
#define TR_TYPE_PIECES          ((const GType)tr_type_pieces)
#define TR_PIECES(ptr)          ((struct pieces*)ptr)
GType tr_type_pieces;

#define LIST_ACTION           "torrent-list-action"
enum listact { ACT_OPEN, ACT_START, ACT_STOP, ACT_DELETE, ACT_INFO, ACT_PREF };
#define LIST_ACTION_FROM      "torrent-list-action-from"
enum listfrom { FROM_BUTTON, FROM_POPUP };

#define LIST_IDS              "torrent-list-ids"
#define LIST_MENU_WIDGET      "torrent-list-popup-menu-widget"

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
};

#define CBDATA_PTR              "callback-data-pointer"

#define SIGCOUNT_MAX            3

static sig_atomic_t global_sigcount = 0;
static int global_lastsig = 0;

int
main(int argc, char **argv) {
  GtkWidget *mainwind, *preferr, *stateerr;
  char *err;
  tr_handle_t *tr;
  GList *saved;
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

  tr = tr_init();

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

      if(!cf_loadprefs(&err)) {
        preferr = errmsg(GTK_WINDOW(mainwind), "%s", err);
        g_free(err);
      }
      saved = cf_loadstate(&err);
      if(NULL != err) {
        stateerr = errmsg(GTK_WINDOW(mainwind), "%s", err);
        g_free(err);
      }

      /* set the upload limit */
      setlimit(tr);

      /* set the listening port */
      if(NULL != (pref = cf_getpref(PREF_PORT)) &&
         0 < (intval = strtol(pref, NULL, 10)) && 0xffff >= intval)
        tr_setBindPort(tr, intval);

      maketypes();
      makewind(mainwind, tr, saved, argfiles);

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
  while(0 < --argc) {
    argv++;
    if(0 == strcmp("--", *argv))
      return checkfilenames(argc - 1, argv + 1);
    else if('-' != argv[0][0])
      return checkfilenames(argc, argv);
  }

  return NULL;
}

void
maketypes(void) {
  tr_type_pieces = g_boxed_type_register_static(
    TR_TYPE_PIECES_NAME, tr_pieces_copy, tr_pieces_free);
}

gpointer
tr_pieces_copy(gpointer data) {
  return g_memdup(data, sizeof(struct pieces));
}

void
tr_pieces_free(gpointer data) {
  g_free(data);
}

void
makewind(GtkWidget *wind, tr_handle_t *tr, GList *saved, GList *args) {
  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  GtkWidget *status = gtk_statusbar_new();
  struct cbdata *data = g_new0(struct cbdata, 1);
  GtkWidget *list;
  GtkRequisition req;
  GList *loaderrs, *ii;
  struct cf_torrentstate *ts;
  gint height;
  char *str;

  data->tr = tr;
  data->wind = GTK_WINDOW(wind);
  data->timer = -1;
  /* filled in by makewind_list */
  data->model = NULL;
  data->view = NULL;
  data->bar = GTK_STATUSBAR(status);
  data->buttons = NULL;
  data->prefsopen = FALSE;

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start(GTK_BOX(vbox), makewind_toolbar(data), FALSE, FALSE, 0);

  list = makewind_list(data);
  gtk_container_add(GTK_CONTAINER(scroll), list);
  gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

  gtk_statusbar_push(GTK_STATUSBAR(status), 0, "");
  gtk_box_pack_start(GTK_BOX(vbox), status, FALSE, FALSE, 0);

  gtk_container_add(GTK_CONTAINER(wind), vbox);
  gtk_window_set_title(data->wind, g_get_application_name());
  g_signal_connect(G_OBJECT(wind), "delete_event", G_CALLBACK(winclose), data);

  setupdrag(list, data);

  loaderrs = NULL;
  for(ii = g_list_first(saved); NULL != ii; ii = ii->next) {
    ts = ii->data;
    addtorrent(data, ts->ts_torrent, ts->ts_directory, ts->ts_paused,
               &loaderrs);
    cf_freestate(ts);
  }
  g_list_free(saved);

  for(ii = g_list_first(args); NULL != ii; ii = ii->next)
    addtorrent(data, ii->data, NULL, FALSE, &loaderrs);

  if(NULL != loaderrs) {
    str = joinstrlist(loaderrs, "\n");
    errmsg(GTK_WINDOW(wind), ngettext("Failed to load the torrent file %s",
                                      "Failed to load the torrent files:\n%s",
                                      g_list_length(loaderrs)), str);
    g_list_foreach(loaderrs, (GFunc)g_free, NULL);
    g_list_free(loaderrs);
    g_free(str);
    savetorrents(tr, GTK_WINDOW(wind));
  }

  data->timer = g_timeout_add(500, updatemodel, data);
  updatemodel(data);

  gtk_widget_show_all(vbox);
  gtk_widget_realize(wind);

  gtk_widget_size_request(list, &req);
  height = req.height;
  gtk_widget_size_request(scroll, &req);
  height -= req.height;
  gtk_widget_size_request(wind, &req);
  height += req.height;
  gtk_window_set_default_size(GTK_WINDOW(wind), -1, (height > req.width ?
     MIN(height, req.width * 8 / 5) : MAX(height, req.width * 5 / 8)));

  gtk_widget_show(wind);

  ipc_socket_setup(GTK_WINDOW(wind), addtorrent, addedtorrents, data);
}

GtkWidget *
makewind_toolbar(struct cbdata *data) {
  GtkWidget *bar = gtk_toolbar_new();
  GtkToolItem *item;
  unsigned int ii;

  gtk_toolbar_set_tooltips(GTK_TOOLBAR(bar), TRUE);
  gtk_toolbar_set_show_arrow(GTK_TOOLBAR(bar), FALSE);
  gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_BOTH);

  data->buttons = g_new(GtkWidget*, ALEN(actionitems));

  for(ii = 0; ii < ALEN(actionitems); ii++) {
    item = gtk_tool_button_new_from_stock(actionitems[ii].id);
    data->buttons[ii] = GTK_WIDGET(item);
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(item),
                              gettext(actionitems[ii].name));
    gtk_tool_item_set_tooltip(GTK_TOOL_ITEM(item), GTK_TOOLBAR(bar)->tooltips,
                              gettext(actionitems[ii].ttext),
                              actionitems[ii].tpriv);
    g_object_set_data(G_OBJECT(item), LIST_ACTION,
                      GINT_TO_POINTER(actionitems[ii].act));
    g_object_set_data(G_OBJECT(item), LIST_ACTION_FROM,
                      GINT_TO_POINTER(FROM_BUTTON));
    g_signal_connect(G_OBJECT(item), "clicked", G_CALLBACK(actionclick), data);
    gtk_toolbar_insert(GTK_TOOLBAR(bar), item, -1);
  }

  return bar;
}

/* XXX check for unused data in model */
enum {MC_NAME, MC_SIZE, MC_STAT, MC_ERR, MC_TERR, MC_PROG, MC_DRATE, MC_URATE,
      MC_ETA, MC_PEERS, MC_UPEERS, MC_DPEERS, MC_PIECES, MC_DOWN, MC_UP,
      MC_TORRENT, MC_ROW_COUNT};

GtkWidget *
makewind_list(struct cbdata *data) {
  GType types[] = {
    /* info->name, info->totalSize, status,     error,      trackerError, */
    G_TYPE_STRING, G_TYPE_UINT64,   G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING,
    /* progress,  rateDownload, rateUpload,   eta,        peersTotal, */
    G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_INT, G_TYPE_INT,
    /* peersUploading, peersDownloading, pieces,         downloaded, */
    G_TYPE_INT,        G_TYPE_INT,       TR_TYPE_PIECES, G_TYPE_UINT64,
    /* uploaded,   the handle for the torrent */
    G_TYPE_UINT64, G_TYPE_POINTER};
  GtkListStore *store;
  GtkWidget *view;
  GtkTreeViewColumn *col;
  GtkTreeSelection *sel;
  GtkCellRenderer *namerend, *progrend;
  char *str;

  assert(MC_ROW_COUNT == ALEN(types));

  store = gtk_list_store_newv(MC_ROW_COUNT, types);
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  /* XXX do I need to worry about reference counts anywhere else? */
  g_object_unref(G_OBJECT(store));
  data->model = GTK_TREE_MODEL(store);
  data->view = GTK_TREE_VIEW(view);

  namerend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes(_("Name"), namerend, NULL);
  gtk_tree_view_column_set_cell_data_func(col, namerend, dfname, NULL, NULL);
  gtk_tree_view_column_set_expand(col, TRUE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  progrend = tr_cell_renderer_torrent_new();
  /* this string is only used to determing the size of the progress bar */
  str = g_markup_printf_escaped("<big>%s</big>", _("  fnord    fnord  "));
  g_object_set(progrend, "label", str, NULL);
  g_free(str);
  col = gtk_tree_view_column_new_with_attributes(_("Progress"), progrend, NULL);
  gtk_tree_view_column_set_cell_data_func(col, progrend, dfprog, NULL, NULL);
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
  tr_stat_t *st;
  GtkTreeIter iter;
  tr_torrent_t *tor;
  gboolean going;

  if(0 >= data->timer)
    g_source_remove(data->timer);
  data->timer = -1;

  going = gtk_tree_model_get_iter_first(data->model, &iter);
  while(going) {
    gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor, -1);
    st = tr_torrentStat(tor);
    if(TR_STATUS_ACTIVE & st->status) {
      tr_torrentStop(tor);
      going = gtk_tree_model_iter_next(data->model, &iter);
    } else {
      tr_torrentClose(data->tr, tor);
      going = gtk_list_store_remove(GTK_LIST_STORE(data->model), &iter);
    }
  }

  /* XXX should disable widgets or something */

  /* try to wait until torrents stop before exiting */
  edata = g_new0(struct exitdata, 1);
  edata->cbdata = data;
  edata->started = time(NULL);
  edata->timer = g_timeout_add(500, exitcheck, edata);

  /* returning FALSE means to destroy the window */
  return TRUE;
}

gboolean
exitcheck(gpointer gdata) {
  struct exitdata *data = gdata;
  tr_stat_t *st;
  GtkTreeIter iter;
  tr_torrent_t *tor;
  gboolean go;

  go = gtk_tree_model_get_iter_first(data->cbdata->model, &iter);
  while(go) {
    gtk_tree_model_get(data->cbdata->model, &iter, MC_TORRENT, &tor, -1);
    st = tr_torrentStat(tor);
    if(!(TR_STATUS_PAUSE & st->status))
      go = gtk_tree_model_iter_next(data->cbdata->model, &iter);
    else {
      tr_torrentClose(data->cbdata->tr, tor);
      go = gtk_list_store_remove(GTK_LIST_STORE(data->cbdata->model), &iter);
    }
  }

  /* keep going if we still have torrents and haven't hit the exit timeout */
  if(0 < tr_torrentCount(data->cbdata->tr) &&
     time(NULL) - data->started < TRACKER_EXIT_TIMEOUT) {
    assert(gtk_tree_model_get_iter_first(data->cbdata->model, &iter));
    updatemodel(data->cbdata);
    return TRUE;
  }

  /* exit otherwise */

  if(0 >= data->timer)
    g_source_remove(data->timer);
  data->timer = -1;

  stoptransmission(data->cbdata->tr);

  gtk_widget_destroy(GTK_WIDGET(data->cbdata->wind));
  g_free(data->cbdata);
  g_free(data);
  gtk_main_quit();

  return FALSE;
}

void
stoptransmission(tr_handle_t *tr) {
  GList *list, *ii;

  list = NULL;
  tr_torrentIterate(tr, maketorrentlist, &list);
  for(ii = g_list_first(list); NULL != ii; ii = ii->next)
    tr_torrentClose(tr, ii->data);
  g_list_free(list);

  tr_close(tr);
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
  gboolean gotfile;
  struct stat sb;
  int prelen = strlen(prefix);

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
  gotfile = FALSE;
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
            /* finally, try to add it as a torrent */
            if(addtorrent(data, deslashed, NULL, FALSE, &errs))
              gotfile = TRUE;
          }
        }
        g_free(decoded);
      }
    }

    g_free(files);
    if(gotfile)
      addedtorrents(data);

    /* print any errors */
    if(NULL != errs) {
      files = joinstrlist(errs, "\n");
      errmsg(data->wind, ngettext("Failed to load the torrent file %s",
                                  "Failed to load the torrent files:\n%s",
                                  g_list_length(errs)), files);
      g_list_foreach(errs, (GFunc)g_free, NULL);
      g_list_free(errs);
      g_free(files);
    }
  }

  gtk_drag_finish(dc, gotfile, FALSE, time);
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
    tr_cell_renderer_torrent_reset_style(TR_CELL_RENDERER_TORRENT(gdata));
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
  char *name, *mb, *terr, *str, *top, *bottom;
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
      top = g_strdup_printf(_("Finishing in --:--:-- (%.1f%%)"), prog);
    else
      top = g_strdup_printf(_("Finishing in %02i:%02i:%02i (%.1f%%)"),
                            eta / 60 / 60, eta / 60 % 60, eta % 60, prog);
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
    assert("XXX unknown status");
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
  g_object_set(rend, "text", str, "value", prog, NULL);
  g_free(dlstr);
  g_free(ulstr);
  g_free(str);
  g_free(marked);
}

gboolean
updatemodel(gpointer gdata) {
  struct cbdata *data = gdata;
  tr_torrent_t *tor;
  tr_stat_t *st;
  tr_info_t *in;
  GtkTreeIter iter;
  float up, down;
  char *upstr, *downstr, *str;

  if(0 < global_sigcount) {
    stoptransmission(data->tr);
    global_sigcount = SIGCOUNT_MAX;
    raise(global_lastsig);
  }

  if(gtk_tree_model_get_iter_first(data->model, &iter)) {
    do {
      gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor, -1);
      st = tr_torrentStat(tor);
      in = tr_torrentInfo(tor);
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
  tr_torrentRates(data->tr, &up, &down);
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
  gpointer tor;
  GList *ids;

  if(GDK_BUTTON_PRESS == event->type && 3 == event->button) {
    /* find what row, if any, the user clicked on */
    if(!gtk_tree_view_get_path_at_pos(data->view, event->x, event->y, &path,
                                      NULL, NULL, NULL))
      /* no row was clicked, do the popup with no torrent IDs or status */
      dopopupmenu(event, data, NULL, 0);
    else {
      if(gtk_tree_model_get_iter(data->model, &iter, path)) {
        /* get ID and status for the right-clicked row */
        gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor,
                           MC_STAT, &status, -1);
        /* get a list of selected IDs */
        ids = NULL;
        gtk_tree_selection_selected_foreach(sel, makeidlist, &ids);
        /* is the clicked row selected? */
        if(NULL == g_list_find(ids, tor)) {
          /* no, do the popup for just the clicked row */
          g_list_free(ids);
          dopopupmenu(event, data, g_list_append(NULL, tor), status);
        } else {
          /* yes, do the popup for all the selected rows */
          gtk_tree_selection_selected_foreach(sel, orstatus, &status);
          dopopupmenu(event, data, ids, status);
        }
      }
      gtk_tree_path_free(path);
    }
    return TRUE;
  }

  return FALSE;
}

gboolean
listpopup(GtkWidget *widget SHUTUP, gpointer gdata) {
  struct cbdata *data = gdata;
  GtkTreeSelection *sel = gtk_tree_view_get_selection(data->view);
  GList *ids;
  int status;

  if(0 >= gtk_tree_selection_count_selected_rows(sel))
    dopopupmenu(NULL, data, NULL, 0);
  else {
    status = 0;
    gtk_tree_selection_selected_foreach(sel, orstatus, &status);
    ids = NULL;
    gtk_tree_selection_selected_foreach(sel, makeidlist, &ids);
    dopopupmenu(NULL, data, ids, status);
  }

  return TRUE;
}

void
dopopupmenu(GdkEventButton *event, struct cbdata *data,
            GList *ids, int status) {
  GtkWidget *menu = gtk_menu_new();
  GtkWidget *item;
  unsigned int ii;

  for(ii = 0; ii < ALEN(actionitems); ii++) {
    if(actionitems[ii].nomenu ||
       (actionitems[ii].avail &&
        (NULL == ids || !(actionitems[ii].avail & status))))
      continue;
    item = gtk_menu_item_new_with_label(gettext(actionitems[ii].name));
    /* set the action for the menu item */
    g_object_set_data(G_OBJECT(item), LIST_ACTION,
                      GINT_TO_POINTER(actionitems[ii].act));
    /* show that this action came from a popup menu */
    g_object_set_data(G_OBJECT(item), LIST_ACTION_FROM,
                      GINT_TO_POINTER(FROM_POPUP));
    /* set a glist of selected torrent's IDs */
    g_object_set_data(G_OBJECT(item), LIST_IDS, ids);
    /* set the menu widget, so the activate handler can destroy it */
    g_object_set_data(G_OBJECT(item), LIST_MENU_WIDGET, menu);
    g_signal_connect(G_OBJECT(item), "activate",
                     G_CALLBACK(actionclick), data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  }

  /* set up the glist to be freed when the menu is destroyed */
  g_object_set_data_full(G_OBJECT(menu), LIST_IDS, ids,
                         (GDestroyNotify)g_list_free);

  /* destroy the menu if the user doesn't select anything */
  g_signal_connect(menu, "selection-done", G_CALLBACK(killmenu), NULL);

  gtk_widget_show_all(menu);

  gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                 (NULL == event ? 0 : event->button),
                 gdk_event_get_time((GdkEvent*)event));
}

void
killmenu(GtkWidget *menu, gpointer *gdata SHUTUP) {
  gtk_widget_destroy(menu);
}

void
actionclick(GtkWidget *widget, gpointer gdata) {
  struct cbdata *data = gdata;
  GtkTreeSelection *sel = gtk_tree_view_get_selection(data->view);
  enum listact act =
    GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), LIST_ACTION));
  enum listfrom from =
    GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), LIST_ACTION_FROM));
  unsigned int actindex;
  tr_stat_t *sb;
  GList *ids, *ii;
  tr_torrent_t *tor;
  gboolean updatesave;
  GtkTreeIter iter;

  /* destroy the popup menu, if any */
  if(FROM_POPUP == from)
    gtk_widget_destroy(g_object_get_data(G_OBJECT(widget), LIST_MENU_WIDGET));

  switch(act) {
    case ACT_OPEN:
      makeaddwind(data->wind, addtorrent, addedtorrents, data);
      return;
    case ACT_PREF:
      if(!data->prefsopen)
        makeprefwindow(data->wind, data->tr, &data->prefsopen);
      return;
    default:
      break;
  }

  switch(from) {
    case FROM_BUTTON:
      ids = NULL;
      gtk_tree_selection_selected_foreach(sel, makeidlist, &ids);
      break;
    case FROM_POPUP:
      ids = g_object_get_data(G_OBJECT(widget), LIST_IDS);
      break;
    default:
      assert(!"unknown action source");
      break;
  }

  for(actindex = 0; actindex < ALEN(actionitems); actindex++)
    if(actionitems[actindex].act == act)
      break;
  assert(actindex < ALEN(actionitems));

  updatesave = FALSE;

  for(ii = g_list_sort(ids, intrevcmp); NULL != ii; ii = ii->next) {
    tor = ii->data;
    sb = tr_torrentStat(tor);

    /* check if this action is valid for this torrent */
    if(actionitems[actindex].nomenu ||
       (actionitems[actindex].avail &&
        !(actionitems[actindex].avail & sb->status)))
      continue;

    switch(act) {
      case ACT_START:
        tr_torrentStart(tor);
        updatesave = TRUE;
        break;
      case ACT_STOP:
        tr_torrentStop(tor);
        updatesave = TRUE;
        break;
      case ACT_DELETE:
        if(TR_STATUS_ACTIVE & sb->status)
          tr_torrentStop(tor);
        tr_torrentClose(data->tr, tor);
        findtorrent(data->model, tor, &iter);
        gtk_list_store_remove(GTK_LIST_STORE(data->model), &iter);
        updatesave = TRUE;
        /* XXX should only unselect deleted rows */
        gtk_tree_selection_unselect_all(
          gtk_tree_view_get_selection(data->view));
        break;
      case ACT_INFO:
        makeinfowind(data->wind, tor);
        break;
      default:
        assert(!"unknown type");
        break;
    }
  }

  if(updatesave) {
    savetorrents(data->tr, data->wind);
    updatemodel(data);
  }

  if(FROM_BUTTON == from)
    g_list_free(ids);
}

void
findtorrent(GtkTreeModel *model, tr_torrent_t *tor, GtkTreeIter *iter) {
  gpointer ptr;

  if(gtk_tree_model_get_iter_first(model, iter)) {
    do {
      gtk_tree_model_get(model, iter, MC_TORRENT, &ptr, -1);
      if(tor == ptr)
        return;
    } while(gtk_tree_model_iter_next(model, iter));
  }

  assert(!"torrent not found");
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
  tr_torrent_t *tor;

  if(gtk_tree_model_get_iter(data->model, &iter, path)) {
    gtk_tree_model_get(data->model, &iter, MC_TORRENT, &tor, -1);
    makeinfowind(data->wind, tor);
  }
}

gboolean
addtorrent(void *vdata, const char *torrent, const char *dir, gboolean paused,
           GList **errs) {
  const struct { const int err; const char *msg; } errstrs[] = {
    {TR_EINVALID,       N_("not a valid torrent file")},
    {TR_EDUPLICATE,     N_("torrent is already open")},
  };
  struct cbdata *data = vdata;
  tr_torrent_t *new;
  char *wd;
  int err;
  unsigned int ii;
  GtkTreeIter iter;

  if(NULL == dir && NULL != (dir = cf_getpref(PREF_DIR))) {
    if(!mkdir_p(dir, 0777)) {
      errmsg(data->wind, _("Failed to create the directory %s:\n%s"),
             dir, strerror(errno));
      return FALSE;
    }
  }

  if(NULL == (new = tr_torrentInit(data->tr, torrent, &err))) {
    for(ii = 0; ii < ALEN(errstrs); ii++)
      if(err == errstrs[ii].err)
        break;
    if(NULL == errs) {
      if(ii == ALEN(errstrs))
        errmsg(data->wind, _("Failed to load the torrent file %s"), torrent);
      else
        errmsg(data->wind, _("Failed to load the torrent file %s: %s"),
               torrent, gettext(errstrs[ii].msg));
    } else {
      if(ii == ALEN(errstrs))
        *errs = g_list_append(*errs, g_strdup(torrent));
      else
        *errs = g_list_append(*errs, g_strdup_printf(_("%s (%s)"),
                              torrent, gettext(errstrs[ii].msg)));
    }
    return FALSE;
  }

  gtk_list_store_append(GTK_LIST_STORE(data->model), &iter);
  gtk_list_store_set(GTK_LIST_STORE(data->model), &iter, MC_TORRENT, new, -1);

  if(NULL != dir)
    tr_torrentSetFolder(new, dir);
  else {
    wd = g_new(char, MAXPATHLEN + 1);
    if(NULL == getcwd(wd, MAXPATHLEN + 1))
      tr_torrentSetFolder(new, ".");
    else {
      tr_torrentSetFolder(new, wd);
      free(wd);
    }
  }

  if(!paused)
    tr_torrentStart(new);

  return TRUE;
}

void
addedtorrents(void *vdata) {
  struct cbdata *data = vdata;

  updatemodel(data);
  savetorrents(data->tr, data->wind);
}

gboolean
savetorrents(tr_handle_t *tr, GtkWindow *wind) {
  GList *torrents;
  char *errstr;
  gboolean ret;

  torrents = NULL;
  tr_torrentIterate(tr, maketorrentlist, &torrents);

  if(!(ret = cf_savestate(torrents, &errstr))) {
    errmsg(wind, "%s", errstr);
    g_free(errstr);
  }

  g_list_free(torrents);

  return ret;
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

void
makeidlist(GtkTreeModel *model, GtkTreePath *path SHUTUP, GtkTreeIter *iter,
           gpointer gdata) {
  GList **ids = gdata;
  gpointer ptr;

  gtk_tree_model_get(model, iter, MC_TORRENT, &ptr, -1);
  *ids = g_list_append(*ids, ptr);
}

void
maketorrentlist(tr_torrent_t *tor, void *data) {
  GList **list = data;

  *list = g_list_append(*list, tor);
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

  global_lastsig = sig;

  if(SIGCOUNT_MAX <= ++global_sigcount) {
    bzero(&sa, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, NULL);
    raise(sig);
  }
}
