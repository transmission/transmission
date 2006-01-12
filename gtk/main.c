/*
  Copyright (c) 2005 Joshua Elsasser. All rights reserved.
   
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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "conf.h"
#include "gtkcellrenderertorrent.h"
#include "prefs.h"
#include "transmission.h"
#include "util.h"

#define TRACKER_EXIT_TIMEOUT    5

struct cbdata {
  tr_handle_t *tr;
  GtkWindow *wind;
  GtkListStore *model;
  GtkTreeView *view;
  GtkStatusbar *bar;
  GtkWidget **buttons;
  guint timer;
};

struct exitdata {
  struct cbdata *cbdata;
  time_t started;
  guint timer;
};

struct pieces {
  char p[120];
};

void
maketypes(void);
gpointer
tr_pieces_copy(gpointer);
void
tr_pieces_free(gpointer);

void
makewind(GtkWidget *wind, tr_handle_t *tr, GList *saved);
gboolean
winclose(GtkWidget *widget, GdkEvent *event, gpointer gdata);
gboolean
exitcheck(gpointer gdata);
void
stoptransmission(void *tr);
GtkWidget *
makewind_toolbar(struct cbdata *data);
GtkWidget *
makewind_list(struct cbdata *data);
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
killmenu(GtkWidget *menu, gpointer *gdata SHUTUP);
void
actionclick(GtkWidget *widget, gpointer gdata);
gint
intrevcmp(gconstpointer a, gconstpointer b);
void
doubleclick(GtkWidget *widget, GtkTreePath *path, GtkTreeViewColumn *col,
            gpointer gdata);

gboolean
addtorrent(tr_handle_t *tr, GtkWindow *parentwind, const char *torrent,
           const char *dir, gboolean paused);
void
addedtorrents(void *vdata);
gboolean
savetorrents(tr_handle_t *tr, GtkWindow *wind, int count, tr_stat_t *stat);
void
orstatus(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
         gpointer gdata);
void
makeidlist(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
           gpointer gdata);

#define TR_TYPE_PIECES_NAME     "tr-type-pieces"
#define TR_TYPE_PIECES          ((const GType)tr_type_pieces)
#define TR_PIECES(ptr)          ((struct pieces*)ptr)
GType tr_type_pieces;

#define LIST_ACTION           "torrent-list-action"
enum listact { ACT_OPEN, ACT_START, ACT_STOP, ACT_DELETE, ACT_INFO, ACT_PREF };
#define LIST_ACTION_FROM      "torrent-list-action-from"
enum listfrom { FROM_BUTTON, FROM_POPUP };

#define LIST_INDEX            "torrent-list-indexes"
#define LIST_MENU_WIDGET      "torrent-list-popup-menu-widget"

struct { const gchar *name; const gchar *id; enum listact act; gboolean nomenu;
  int avail; const char *ttext; const char *tpriv; }
actionitems[] = {
  {"Add",         GTK_STOCK_ADD,          ACT_OPEN,   FALSE,  0,
   "Add a new torrent file", "XXX"},
  {"Resume",      GTK_STOCK_MEDIA_PLAY,   ACT_START,  FALSE,
   (TR_STATUS_STOPPING | TR_STATUS_PAUSE),
   "Resume a torrent that has been paused", "XXX"},
  {"Pause",       GTK_STOCK_MEDIA_PAUSE,  ACT_STOP,   FALSE,
   ~(TR_STATUS_STOPPING | TR_STATUS_PAUSE),
   "Pause a torrent", "XXX"},
  {"Remove",      GTK_STOCK_REMOVE,       ACT_DELETE, FALSE, ~0,
   "Remove a torrent from the list", "XXX"},
  {"Properties",  GTK_STOCK_PROPERTIES,   ACT_INFO,   FALSE, ~0,
   "Get additional information for a torrent", "XXX"},
  {"Preferences", GTK_STOCK_PREFERENCES,  ACT_PREF,   TRUE,   0,
   "Open preferences dialog", "XXX"},
};

#define CBDATA_PTR              "callback-data-pointer"
int
main(int argc, char **argv) {
  GtkWidget *mainwind, *preferr, *stateerr;
  char *err;
  tr_handle_t *tr;
  GList *saved;
  const char *pref;
  long intval;

  gtk_init(&argc, &argv);

  tr = tr_init();

  setuphandlers(stoptransmission, tr);

  if(cf_init(tr_getPrefsDirectory(), &err)) {
    if(cf_lock(&err)) {
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
      makewind(mainwind, tr, saved);

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

  gtk_main();

  return 0;
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
makewind(GtkWidget *wind, tr_handle_t *tr, GList *saved) {
  GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  GtkWidget *status = gtk_statusbar_new();
  struct cbdata *data = g_new0(struct cbdata, 1);
  GtkWidget *list;
  GtkRequisition req;
  GList *ii;
  struct cf_torrentstate *ts;
  gint height;

  data->tr = tr;
  data->wind = GTK_WINDOW(wind);
  data->timer = -1;
  /* filled in by makewind_list */
  data->model = NULL;
  data->view = NULL;
  data->bar = GTK_STATUSBAR(status);
  data->buttons = NULL;

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start(GTK_BOX(vbox), makewind_toolbar(data), FALSE, FALSE, 0);

  list = makewind_list(data);
  gtk_container_add(GTK_CONTAINER(scroll), list);
  gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

  gtk_statusbar_push(GTK_STATUSBAR(status), 0, "");
  gtk_box_pack_start(GTK_BOX(vbox), status, FALSE, FALSE, 0);

  gtk_container_add(GTK_CONTAINER(wind), vbox);
  g_signal_connect(G_OBJECT(wind), "delete_event", G_CALLBACK(winclose), data);

  for(ii = g_list_first(saved); NULL != ii; ii = ii->next) {
    ts = ii->data;
    addtorrent(tr, GTK_WINDOW(wind),
               ts->ts_torrent, ts->ts_directory, ts->ts_paused);
    cf_freestate(ts);
  }
  g_list_free(saved);

  data->timer = g_timeout_add(500, updatemodel, data);
  updatemodel(data);

  gtk_widget_show_all(vbox);
  gtk_widget_realize(wind);

  gtk_widget_size_request(wind, &req);
  height = req.height;
  gtk_widget_size_request(scroll, &req);
  height -= req.height;
  gtk_widget_size_request(list, &req);
  height += req.height;
  gtk_window_set_default_size(GTK_WINDOW(wind), -1, MAX(height, 100));

  gtk_widget_show(wind);
}

/* XXX is this the right thing to do? */
#define TR_TORRENT_NEEDS_STOP(t) \
  ((t) & TR_STATUS_CHECK || (t) & TR_STATUS_DOWNLOAD || (t) & TR_STATUS_SEED)

gboolean
winclose(GtkWidget *widget SHUTUP, GdkEvent *event SHUTUP, gpointer gdata) {
  struct cbdata *data = gdata;
  struct exitdata *edata;
  tr_stat_t *st;
  int ii;

  if(0 >= data->timer)
    g_source_remove(data->timer);
  data->timer = -1;

  blocksigs();

  for(ii = tr_torrentStat(data->tr, &st); 0 < ii; ii--) {
    if(TR_TORRENT_NEEDS_STOP(st[ii-1].status)) {
      /*fprintf(stderr, "quit: stopping %i %s\n", ii, st[ii-1].info.name);*/
      tr_torrentStop(data->tr, ii - 1);
    } else {
      /*fprintf(stderr, "quit: closing %i %s\n", ii, st[ii-1].info.name);*/
      tr_torrentClose(data->tr, ii - 1);
    }
  }
  free(st);

  unblocksigs();

  /* XXX should disable widgets or something */

  /* try to wait until torrents stop before exiting */
  edata = g_new0(struct exitdata, 1);
  edata->cbdata = data;
  edata->started = time(NULL);
  edata->timer = g_timeout_add(500, exitcheck, edata);

  /*fprintf(stderr, "quit: starting timeout at %i\n", edata->started);*/

  /* returning FALSE means to destroy the window */
  return TRUE;
}

gboolean
exitcheck(gpointer gdata) {
  struct exitdata *data = gdata;
  tr_stat_t *st;
  int ii;

  blocksigs();

  for(ii = tr_torrentStat(data->cbdata->tr, &st); 0 < ii; ii--) {
    if(TR_STATUS_PAUSE & st[ii-1].status) {
      /*fprintf(stderr, "quit: closing %i %s\n", ii, st[ii-1].info.name);*/
      tr_torrentClose(data->cbdata->tr, ii - 1);
    }
  }
  free(st);

  /*fprintf(stderr, "quit: %i torrents left at %i\n",
    tr_torrentCount(data->cbdata->tr), time(NULL));*/
  /* keep going if we still have torrents and haven't hit the exit timeout */
  if(0 < tr_torrentCount(data->cbdata->tr) &&
     time(NULL) - data->started < TRACKER_EXIT_TIMEOUT) {
    updatemodel(data->cbdata);
    unblocksigs();
    return TRUE;
  }

  /* exit otherwise */

  if(0 >= data->timer)
    g_source_remove(data->timer);
  data->timer = -1;

  /*fprintf(stderr, "quit: giving up on %i torrents\n",
    tr_torrentCount(data->cbdata->tr));*/
  stoptransmission(data->cbdata->tr);
  clearhandlers();
  unblocksigs();

  gtk_widget_destroy(GTK_WIDGET(data->cbdata->wind));
  g_free(data->cbdata);
  g_free(data);
  gtk_main_quit();

  return FALSE;
}

void
stoptransmission(void *tr) {
  while(0 < tr_torrentCount(tr))
    tr_torrentClose(tr, 0);
  tr_close(tr);
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
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(item), actionitems[ii].name);
    gtk_tool_item_set_tooltip(GTK_TOOL_ITEM(item), GTK_TOOLBAR(bar)->tooltips,
                              actionitems[ii].ttext, actionitems[ii].tpriv);
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
enum {MC_NAME, MC_SIZE, MC_STAT, MC_ERR, MC_PROG, MC_DRATE, MC_URATE,
      MC_ETA, MC_PEERS, MC_UPEERS, MC_DPEERS, MC_PIECES, MC_DOWN, MC_UP,
      MC_ROW_INDEX, MC_ROW_COUNT};

GtkWidget *
makewind_list(struct cbdata *data) {
  GType types[] = {
    /* info->name, info->totalSize, status,     error,         progress */
    G_TYPE_STRING, G_TYPE_UINT64,   G_TYPE_INT, G_TYPE_STRING, G_TYPE_FLOAT,
    /* rateDownload, rateUpload,   eta,        peersTotal, peersUploading */
    G_TYPE_FLOAT,    G_TYPE_FLOAT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
    /* peersDownloading, pieces,         downloaded,    uploaded */
    G_TYPE_INT,          TR_TYPE_PIECES, G_TYPE_UINT64, G_TYPE_UINT64,
    /* index into the torrent array */
    G_TYPE_INT};
  GtkListStore *model;
  GtkWidget *view;
  GtkTreeViewColumn *col;
  GtkTreeSelection *sel;
  GtkCellRenderer *namerend, *progrend;

  assert(MC_ROW_COUNT == ALEN(types));

  model = gtk_list_store_newv(MC_ROW_COUNT, types);
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
  /* XXX do I need to worry about reference counts anywhere else? */
  g_object_unref(G_OBJECT(model));
  data->model = model;
  data->view = GTK_TREE_VIEW(view);

  namerend = gtk_cell_renderer_text_new();
  col = gtk_tree_view_column_new_with_attributes("Name", namerend, NULL);
  gtk_tree_view_column_set_cell_data_func(col, namerend, dfname, NULL, NULL);
  gtk_tree_view_column_set_expand(col, TRUE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

  progrend = gtk_cell_renderer_torrent_new();
  g_object_set(progrend, "label", "<big>  fnord    fnord  </big>", NULL);
  col = gtk_tree_view_column_new_with_attributes("Progress", progrend, NULL);
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

/* kludge to have the progress bars notice theme changes */
static void
stylekludge(GObject *obj, GParamSpec *spec, gpointer gdata) {
  if(0 == strcmp("style", spec->name)) {
    gtk_cell_renderer_torrent_reset_style(GTK_CELL_RENDERER_TORRENT(gdata));
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
  char *name, *mb, *err, *str, *top, *bottom;
  guint64 size;
  gfloat prog;
  int status, eta, tpeers, upeers, dpeers;

  /* XXX should I worry about gtk_tree_model_get failing? */
  gtk_tree_model_get(model, iter, MC_NAME, &name, MC_STAT, &status,
    MC_SIZE, &size, MC_PROG, &prog, MC_ETA, &eta, MC_PEERS, &tpeers,
    MC_UPEERS, &upeers, MC_DPEERS, &dpeers, -1);

  if(0 > eta)
    eta = 0;
  if(0 > tpeers)
    tpeers = 0;
  if(0 > upeers)
    upeers = 0;
  if(0 > dpeers)
    dpeers = 0;
  mb = readablesize(size, 1);
  prog *= 100;

  if(status & TR_STATUS_CHECK)
    top = g_strdup_printf("Checking existing files (%.1f%%)", prog);
  else if(status & TR_STATUS_DOWNLOAD)
    top = g_strdup_printf("Finishing in %02i:%02i:%02i (%.1f%%)",
                           eta / 60 / 60, eta / 60 % 60, eta % 60, prog);
  else if(status & TR_STATUS_SEED)
    top = g_strdup_printf("Seeding, uploading to %d of %d peer%s",
                           dpeers, tpeers, (1 == tpeers ? "" : "s"));
  else if(status & TR_STATUS_STOPPING)
    top = g_strdup("Stopping...");
  else if(status & TR_STATUS_PAUSE)
    top = g_strdup_printf("Paused (%.1f%%)", prog);
  else {
    top = g_strdup("");
    assert("XXX unknown status");
  }

  if(status & TR_TRACKER_ERROR) {
    gtk_tree_model_get(model, iter, MC_ERR, &err, -1);
    bottom = g_strconcat("Error: ", err, NULL);
    g_free(err);
  }
  else if(status & TR_STATUS_DOWNLOAD)
    bottom = g_strdup_printf("Downloading from %i of %i peer%s",
                             upeers, tpeers, (1 == tpeers ? "" : "s"));
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
  char *dlstr, *ulstr, *str;
  gfloat prog, dl, ul;

  /* XXX should I worry about gtk_tree_model_get failing? */
  gtk_tree_model_get(model, iter, MC_PROG, &prog,
                     MC_DRATE, &dl, MC_URATE, &ul, -1);
  if(0.0 > prog)
    prog = 0.0;
  else if(1.0 < prog)
    prog = 1.0;

  dlstr = readablesize(dl * 1024.0, 2);
  ulstr = readablesize(ul * 1024.0, 2);
  str = g_strdup_printf("<small>DL: %s/s\nUL: %s/s</small>", dlstr, ulstr);
  g_object_set(rend, "text", str, "value", prog, NULL);
  g_free(dlstr);
  g_free(ulstr);
  g_free(str);
}

gboolean
updatemodel(gpointer gdata) {
  struct cbdata *data = gdata;
  tr_stat_t *st;
  int ii, max;
  GtkTreeIter iter;
  float up, down;
  char *upstr, *downstr, *str;

  blocksigs();

  max = tr_torrentStat(data->tr, &st);
  for(ii = 0; ii < max; ii++) {
    if(!(ii ? gtk_tree_model_iter_next(GTK_TREE_MODEL(data->model), &iter) :
         gtk_tree_model_get_iter_first(GTK_TREE_MODEL(data->model), &iter)))
      gtk_list_store_append(data->model, &iter);
    /* XXX find out if setting the same data emits changed signal */
    gtk_list_store_set(
      data->model, &iter, MC_ROW_INDEX, ii,
      MC_NAME, st[ii].info.name, MC_SIZE, st[ii].info.totalSize,
      MC_STAT, st[ii].status, MC_ERR, st[ii].error, MC_PROG, st[ii].progress,
      MC_DRATE, st[ii].rateDownload, MC_URATE, st[ii].rateUpload,
      MC_ETA, st[ii].eta, MC_PEERS, st[ii].peersTotal,
      MC_UPEERS, st[ii].peersUploading, MC_DPEERS, st[ii].peersDownloading,
      MC_DOWN, st[ii].downloaded, MC_UP, st[ii].uploaded, -1);
  }
  free(st);

  /* remove any excess rows */
  if(ii ? gtk_tree_model_iter_next(GTK_TREE_MODEL(data->model), &iter) :
     gtk_tree_model_get_iter_first(GTK_TREE_MODEL(data->model), &iter))
    while(gtk_list_store_remove(data->model, &iter))
      ;

  /* update the status bar */
  tr_torrentRates(data->tr, &up, &down);
  downstr = readablesize(down * 1024.0, 2);
  upstr = readablesize(up * 1024.0, 2);
  str = g_strdup_printf("     Total DL: %s/s     Total UL: %s/s", upstr, downstr);
  gtk_statusbar_pop(data->bar, 0);
  gtk_statusbar_push(data->bar, 0, str);
  g_free(str);
  g_free(upstr);
  g_free(downstr);

  /* the status of the selected item may have changed, so update the buttons */
  fixbuttons(NULL, data);

  unblocksigs();

  return TRUE;
}

/* show a popup menu for a right-click on the list */
gboolean
listclick(GtkWidget *widget SHUTUP, GdkEventButton *event, gpointer gdata) {
  struct cbdata *data = gdata;
  GtkTreeSelection *sel = gtk_tree_view_get_selection(data->view);
  GtkTreePath *path;
  GtkTreeIter iter;
  int index, status;
  GList *ids;

  if(GDK_BUTTON_PRESS == event->type && 3 == event->button) {
    /* find what row, if any, the user clicked on */
    if(!gtk_tree_view_get_path_at_pos(data->view, event->x, event->y, &path,
                                      NULL, NULL, NULL))
      /* no row was clicked, do the popup with no torrent IDs or status */
      dopopupmenu(event, data, NULL, 0);
    else {
      if(gtk_tree_model_get_iter(GTK_TREE_MODEL(data->model), &iter, path)) {
        /* get ID and status for the right-clicked row */
        gtk_tree_model_get(GTK_TREE_MODEL(data->model), &iter,
                           MC_ROW_INDEX, &index, MC_STAT, &status, -1);
        /* get a list of selected IDs */
        ids = NULL;
        gtk_tree_selection_selected_foreach(sel, makeidlist, &ids);
        /* is the clicked row selected? */
        if(NULL == g_list_find(ids, GINT_TO_POINTER(index))) {
          /* no, do the popup for just the clicked row */
          g_list_free(ids);
          dopopupmenu(event, data, g_list_append(NULL, GINT_TO_POINTER(index)),
                      status);
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
  GtkTreeModel *model;
  GList *ids;
  int status;

  if(0 >= gtk_tree_selection_count_selected_rows(sel))
    dopopupmenu(NULL, data, NULL, 0);
  else {
    assert(model == GTK_TREE_MODEL(data->model));
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
    item = gtk_menu_item_new_with_label(actionitems[ii].name);
    /* set the action for the menu item */
    g_object_set_data(G_OBJECT(item), LIST_ACTION,
                      GINT_TO_POINTER(actionitems[ii].act));
    /* show that this action came from a popup menu */
    g_object_set_data(G_OBJECT(item), LIST_ACTION_FROM,
                      GINT_TO_POINTER(FROM_POPUP));
    /* set a glist of selected torrent's IDs */
    g_object_set_data(G_OBJECT(item), LIST_INDEX, ids);
    /* set the menu widget, so the activate handler can destroy it */
    g_object_set_data(G_OBJECT(item), LIST_MENU_WIDGET, menu);
    g_signal_connect(G_OBJECT(item), "activate",
                     G_CALLBACK(actionclick), data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  }

  /* set up the glist to be freed when the menu is destroyed */
  g_object_set_data_full(G_OBJECT(menu), LIST_INDEX, ids,
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
  int index, count;
  unsigned int actindex;
  tr_stat_t *sb;
  GList *ids, *ii;
  gboolean updatesave;

  /* destroy the popup menu, if any */
  if(FROM_POPUP == from)
    gtk_widget_destroy(g_object_get_data(G_OBJECT(widget), LIST_MENU_WIDGET));

  switch(act) {
    case ACT_OPEN:
      makeaddwind(addtorrent, data->wind, data->tr, addedtorrents, data);
      return;
    case ACT_PREF:
      makeprefwindow(data->wind, data->tr);
      return;
    default:
      break;
  }

  switch(from) {
    case FROM_BUTTON:
      ids = NULL;
      gtk_tree_selection_selected_foreach(sel, makeidlist, &ids);
      /* XXX should I assert(0 <= index) to insure a row was selected? */
      break;
    case FROM_POPUP:
      ids = g_object_get_data(G_OBJECT(widget), LIST_INDEX);
      break;
    default:
      assert(!"unknown action source");
      break;
  }

  for(actindex = 0; actindex < ALEN(actionitems); actindex++)
    if(actionitems[actindex].act == act)
      break;
  assert(actindex < ALEN(actionitems));

  blocksigs();
  updatesave = FALSE;
  count = tr_torrentStat(data->tr, &sb);

  for(ii = g_list_sort(ids, intrevcmp); NULL != ii; ii = ii->next) {
    index = GPOINTER_TO_INT(ii->data);
    if(index >= count) {
      assert(!"illegal torrent id");
      continue;
    }
    /* check if this action is valid for this torrent */
    if(actionitems[actindex].nomenu ||
       (actionitems[actindex].avail &&
        !(actionitems[actindex].avail & sb[index].status)))
      continue;

    switch(act) {
      case ACT_START:
        tr_torrentStart(data->tr, index);
        updatesave = TRUE;
        break;
      case ACT_STOP:
        tr_torrentStop(data->tr, index);
        updatesave = TRUE;
        break;
      case ACT_DELETE:
        if(TR_TORRENT_NEEDS_STOP(sb[index].status))
          tr_torrentStop(data->tr, index);
        tr_torrentClose(data->tr, index);
        updatesave = TRUE;
        /* XXX should only unselect deleted rows */
        gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(data->view));
        break;
      case ACT_INFO:
        makeinfowind(data->wind, data->tr, index);
        break;
      default:
        assert(!"unknown type");
        break;
    }
  }
  free(sb);

  if(updatesave) {
    savetorrents(data->tr, data->wind, -1, NULL);
    updatemodel(data);
  }

  unblocksigs();

  if(FROM_BUTTON == from)
    g_list_free(ids);
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
  int index;

  if(gtk_tree_model_get_iter(GTK_TREE_MODEL(data->model), &iter, path)) {
    gtk_tree_model_get(GTK_TREE_MODEL(data->model), &iter,
                       MC_ROW_INDEX, &index, -1);
    makeinfowind(data->wind, data->tr, index);
  }
}

gboolean
addtorrent(tr_handle_t *tr, GtkWindow *parentwind, const char *torrent,
           const char *dir, gboolean paused) {
  char *wd;

  if(NULL == dir && NULL != (dir = cf_getpref(PREF_DIR))) {
    if(!mkdir_p(dir, 0777)) {
      errmsg(parentwind, "Failed to create download directory %s:\n%s",
             dir, strerror(errno));
      return FALSE;
    }
  }

  blocksigs();

  if(0 != tr_torrentInit(tr, torrent)) {
    unblocksigs();
    /* XXX would be nice to have errno strings, are they printed to stdout? */
    errmsg(parentwind, "Failed to open torrent file %s", torrent);
    return FALSE;
  }

  if(NULL != dir)
    tr_torrentSetFolder(tr, tr_torrentCount(tr) - 1, dir);
  else {
    wd = g_new(char, MAXPATHLEN + 1);
    if(NULL == getcwd(wd, MAXPATHLEN + 1))
      tr_torrentSetFolder(tr, tr_torrentCount(tr) - 1, ".");
    else {
      tr_torrentSetFolder(tr, tr_torrentCount(tr) - 1, wd);
      free(wd);
    }
  }

  if(!paused)
    tr_torrentStart(tr, tr_torrentCount(tr) - 1);

  unblocksigs();

  return TRUE;
}

void
addedtorrents(void *vdata) {
  struct cbdata *data = vdata;

  updatemodel(data);
  savetorrents(data->tr, data->wind, -1, NULL);
}

gboolean
savetorrents(tr_handle_t *tr, GtkWindow *wind, int count, tr_stat_t *stat) {
  char *errstr;
  tr_stat_t *st;
  gboolean ret;

  assert(NULL != tr || 0 <= count);

  if(0 <= count)
    ret = cf_savestate(count, stat, &errstr);
  else {
    blocksigs();
    count = tr_torrentStat(tr, &st);
    unblocksigs();
    ret = cf_savestate(count, st, &errstr);
    free(st);
  }

  if(!ret) {
    errmsg(wind, "%s", errstr);
    g_free(errstr);
  }

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
  int index;

  gtk_tree_model_get(model, iter, MC_ROW_INDEX, &index, -1);
  *ids = g_list_append(*ids, GINT_TO_POINTER(index));
}
