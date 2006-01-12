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
#include "prefs.h"
#include "transmission.h"
#include "util.h"

#define TRACKER_EXIT_TIMEOUT    30

struct cbdata {
  tr_handle_t *tr;
  GtkWindow *wind;
  GtkListStore *model;
  GtkTreeView *view;
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
GtkWidget *
makewind_toolbar(struct cbdata *data);
GtkWidget *
makewind_list(struct cbdata *data);

gboolean
updatemodel(gpointer gdata);
gboolean
listclick(GtkWidget *widget, GdkEventButton *event, gpointer gdata);
gboolean
listpopup(GtkWidget *widget, gpointer userdata);
void
dopopupmenu(GtkWidget *widget, GdkEventButton *event, struct cbdata *data);
void
actionclick(GtkWidget *widget, gpointer gdata);

void
makeaddwind(struct cbdata *data);
gboolean
addtorrent(tr_handle_t *tr, GtkWindow *parentwind, const char *torrent,
           const char *dir, gboolean paused);
void
fileclick(GtkWidget *widget, gpointer gdata);
const char *
statusstr(int status);
void
makeinfowind(struct cbdata *data, int index);
gboolean
savetorrents(tr_handle_t *tr, GtkWindow *wind, int count, tr_stat_t *stat);

#define TR_TYPE_PIECES_NAME     "tr-type-pieces"
#define TR_TYPE_PIECES          ((const GType)tr_type_pieces)
#define TR_PIECES(ptr)          ((struct pieces*)ptr)
GType tr_type_pieces;

#define LIST_ACTION           "torrent-list-action"
enum listact { ACT_OPEN, ACT_START, ACT_STOP, ACT_DELETE, ACT_INFO, ACT_PREF };
#define LIST_ACTION_FROM      "torrent-list-action-from"
enum listfrom { FROM_BUTTON, FROM_POPUP };

#define LIST_INDEX            "torrent-list-index"

struct { gint pos; const gchar *name; const gchar *id; enum listact act;
  const char *ttext; const char *tpriv; }
actionitems[] = {
  {0,  "Add",         GTK_STOCK_ADD,          ACT_OPEN,
   "Add a new torrent file", "XXX"},
  {1,  "Resume",      GTK_STOCK_MEDIA_PLAY,   ACT_START,
   "Resume a torrent that has been paused", "XXX"},
  {2,  "Pause",       GTK_STOCK_MEDIA_PAUSE,  ACT_STOP,
   "Pause a torrent", "XXX"},
  {3,  "Remove",      GTK_STOCK_REMOVE,       ACT_DELETE,
   "Remove a torrent from the list", "XXX"},
  {4,  "Properties",  GTK_STOCK_PROPERTIES,   ACT_INFO,
   "Get additional information for a torrent", "XXX"},
  {5,  "Preferences", GTK_STOCK_PREFERENCES,  ACT_PREF,
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

  if(cf_init(tr_getPrefsDirectory(tr), &err)) {
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
      gtk_widget_show(errmsg_full(NULL, (errfunc_t)gtk_main_quit,
                                  NULL, "%s", err));
      g_free(err);
    }
  } else {
    gtk_widget_show(errmsg_full(NULL, (errfunc_t)gtk_main_quit,
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
  struct cbdata *data = g_new0(struct cbdata, 1);
  GtkWidget *list;
  GtkRequisition req;
  GList *ii;
  struct cf_torrentstate *ts;

  data->tr = tr;
  data->wind = GTK_WINDOW(wind);
  data->timer = -1;
  /* filled in by makewind_list */
  data->model = NULL;
  data->view = NULL;

  gtk_box_pack_start(GTK_BOX(vbox), makewind_toolbar(data), FALSE, FALSE, 0);

  list = makewind_list(data);
  gtk_widget_size_request(list, &req);
  gtk_container_add(GTK_CONTAINER(scroll), list);
  gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 5);

  gtk_container_add(GTK_CONTAINER(wind), vbox);
  gtk_window_set_default_size(GTK_WINDOW(wind), req.width, req.height);
  g_signal_connect(G_OBJECT(wind), "delete_event", G_CALLBACK(winclose), data);
  gtk_widget_show_all(wind);

  for(ii = g_list_first(saved); NULL != ii; ii = ii->next) {
    ts = ii->data;
    addtorrent(tr, GTK_WINDOW(wind),
               ts->ts_torrent, ts->ts_directory, ts->ts_paused);
    cf_freestate(ts);
  }
  g_list_free(saved);

  data->timer = g_timeout_add(500, updatemodel, data);
  updatemodel(data);
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

  for(ii = tr_torrentStat(data->tr, &st); 0 < ii; ii--) {
    if(TR_TORRENT_NEEDS_STOP(st[ii-1].status)) {
      fprintf(stderr, "quit: stopping %i %s\n", ii, st[ii-1].info.name);
      tr_torrentStop(data->tr, ii - 1);
    } else {
      fprintf(stderr, "quit: closing %i %s\n", ii, st[ii-1].info.name);
      tr_torrentClose(data->tr, ii - 1);
    }
  }
  free(st);

  /* XXX should disable widgets or something */

  /* try to wait until torrents stop before exiting */
  edata = g_new0(struct exitdata, 1);
  edata->cbdata = data;
  edata->started = time(NULL);
  edata->timer = g_timeout_add(500, exitcheck, edata);

  fprintf(stderr, "quit: starting timeout at %i\n", edata->started);

  /* returning FALSE means to destroy the window */
  return TRUE;
}

gboolean
exitcheck(gpointer gdata) {
  struct exitdata *data = gdata;
  tr_stat_t *st;
  int ii;

  for(ii = tr_torrentStat(data->cbdata->tr, &st); 0 < ii; ii--) {
    if(TR_STATUS_PAUSE == st[ii-1].status) {
      fprintf(stderr, "quit: closing %i %s\n", ii, st[ii-1].info.name);
      tr_torrentClose(data->cbdata->tr, ii - 1);
    }
  }
  free(st);

  fprintf(stderr, "quit: %i torrents left at %i\n",
          tr_torrentCount(data->cbdata->tr), time(NULL));
  /* keep going if we still have torrents and haven't hit the exit timeout */
  if(0 < tr_torrentCount(data->cbdata->tr) &&
     time(NULL) - data->started < TRACKER_EXIT_TIMEOUT) {
    updatemodel(data->cbdata);
    return TRUE;
  }

  fprintf(stderr, "quit: giving up on %i torrents\n",
          tr_torrentCount(data->cbdata->tr));

  for(ii = tr_torrentCount(data->cbdata->tr); 0 < ii; ii--)
    tr_torrentClose(data->cbdata->tr, ii - 1);

  /* exit otherwise */

  if(0 >= data->timer)
    g_source_remove(data->timer);
  data->timer = -1;

  gtk_widget_destroy(GTK_WIDGET(data->cbdata->wind));
  tr_close(data->cbdata->tr);
  g_free(data->cbdata);
  g_free(data);
  gtk_main_quit();

  return FALSE;
}

GtkWidget *
makewind_toolbar(struct cbdata *data) {
  GtkWidget *bar = gtk_toolbar_new();
  GtkToolItem *item;
  unsigned int ii;

  gtk_toolbar_set_tooltips(GTK_TOOLBAR(bar), TRUE);
  gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_BOTH);

  for(ii = 0; ii < ALEN(actionitems); ii++) {
    item = gtk_tool_button_new_from_stock(actionitems[ii].id);
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(item), actionitems[ii].name);
    gtk_tool_item_set_tooltip(GTK_TOOL_ITEM(item), GTK_TOOLBAR(bar)->tooltips,
                              actionitems[ii].ttext, actionitems[ii].tpriv);
    g_object_set_data(G_OBJECT(item), LIST_ACTION,
                      GINT_TO_POINTER(actionitems[ii].act));
    g_object_set_data(G_OBJECT(item), LIST_ACTION_FROM,
                      GINT_TO_POINTER(FROM_BUTTON));
    g_signal_connect(G_OBJECT(item), "clicked", G_CALLBACK(actionclick), data);
    gtk_toolbar_insert(GTK_TOOLBAR(bar), GTK_TOOL_ITEM(item), actionitems[ii].pos);
  }

  return bar;
}

enum {MC_NAME, MC_SIZE, MC_STAT, MC_ERR, MC_PROG, MC_DRATE, MC_URATE,
      MC_ETA, MC_PEERS, MC_UPEERS, MC_DPEERS, MC_PIECES, MC_DOWN, MC_UP,
      MC_ROW_INDEX, MC_ROW_COUNT};

GtkWidget *
makewind_list(struct cbdata *data) {
  GType types[] = {
    /* info->name, info->totalSize, status,     error,         progress */
    G_TYPE_STRING, G_TYPE_UINT64,   G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT,
    /* rateDownload, rateUpload,   eta,        peersTotal, peersUploading */
    G_TYPE_FLOAT,    G_TYPE_FLOAT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
    /* peersDownloading, pieces,         downloaded,    uploaded */
    G_TYPE_INT,          TR_TYPE_PIECES, G_TYPE_UINT64, G_TYPE_UINT64,
    /* index into the torrent array */
    G_TYPE_INT};
  GtkListStore *model;
  GtkWidget *view;
  /*GtkTreeViewColumn *col;*/
  GtkTreeSelection *sel;
  GtkCellRenderer *rend;
  GtkCellRenderer *rendprog;

  assert(MC_ROW_COUNT == ALEN(types));

  model = gtk_list_store_newv(MC_ROW_COUNT, types);
  view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
  /* XXX do I need to worry about reference counts anywhere else? */
  g_object_unref(G_OBJECT(model));
  data->model = model;
  data->view = GTK_TREE_VIEW(view);

  rend = gtk_cell_renderer_text_new();
  rendprog = gtk_cell_renderer_progress_new();
  g_object_set(rendprog, "text", "", NULL);

  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Name", rend,
                                             "text", MC_NAME, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Size", rend,
                                             "text", MC_SIZE, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Status", rend,
                                             "text", MC_STAT, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Error", rend,
                                             "text", MC_ERR, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Progress", rendprog,
                                             "value", MC_PROG, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Download Rate", rend,
                                             "text", MC_DRATE, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Upload Rate", rend,
                                             "text", MC_URATE, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("ETA", rend,
                                             "text", MC_ETA, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Peers", rend,
                                             "text", MC_PEERS, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Seeders", rend,
                                             "text", MC_UPEERS, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Leechers", rend,
                                             "text", MC_DPEERS, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Downloaded", rend,
                                             "text", MC_DOWN, NULL));
  gtk_tree_view_append_column(GTK_TREE_VIEW(view),
    gtk_tree_view_column_new_with_attributes("Uploaded", rend,
                                             "text", MC_UP, NULL));

  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);
  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_set_mode(GTK_TREE_SELECTION(sel), GTK_SELECTION_SINGLE);
  g_signal_connect(G_OBJECT(view), "button-press-event",
                   G_CALLBACK(listclick), data);
  g_signal_connect(G_OBJECT(view), "popup-menu", G_CALLBACK(listpopup), data);
  gtk_widget_show_all(view);

  return view;
}

gboolean
updatemodel(gpointer gdata) {
  struct cbdata *data = gdata;
  tr_stat_t *st;
  int ii, max, prog;
  GtkTreeIter iter;

  max = tr_torrentStat(data->tr, &st);
  for(ii = 0; ii < max; ii++) {
    if(!(ii ? gtk_tree_model_iter_next(GTK_TREE_MODEL(data->model), &iter) :
         gtk_tree_model_get_iter_first(GTK_TREE_MODEL(data->model), &iter)))
      gtk_list_store_append(data->model, &iter);
    if(0.0 > (prog = st[ii].progress * 100.0))
      prog = 0;
    else if(100 < prog)
      prog = 100;
    /* XXX find out if setting the same data emits changed signal */
    gtk_list_store_set(data->model, &iter, MC_ROW_INDEX, ii,
      MC_NAME, st[ii].info.name, MC_SIZE, st[ii].info.totalSize, MC_STAT, st[ii].status,
      MC_ERR, st[ii].error, MC_PROG, prog, MC_DRATE, st[ii].rateDownload,
      MC_URATE, st[ii].rateUpload, MC_ETA, st[ii].eta, MC_PEERS, st[ii].peersTotal,
      MC_UPEERS, st[ii].peersUploading, MC_DPEERS, st[ii].peersDownloading,
      MC_DOWN, st[ii].downloaded, MC_UP, st[ii].uploaded, -1);
  }
  free(st);

  return TRUE;
}

gboolean
listclick(GtkWidget *widget, GdkEventButton *event, gpointer gdata) {
  struct cbdata *data = gdata;

  if(GDK_BUTTON_PRESS == event->type && 3 == event->button) {
    dopopupmenu(widget, event, data);
    return TRUE;
  }

  return FALSE;
}

gboolean
listpopup(GtkWidget *widget, gpointer userdata) {
  dopopupmenu(widget, NULL, userdata);

  return TRUE;
}

void
dopopupmenu(GtkWidget *widget SHUTUP, GdkEventButton *event,
            struct cbdata *data) {
  GtkWidget *menu = gtk_menu_new();
  GtkWidget *item;
  GtkTreePath *path;
  GtkTreeIter iter;
  unsigned int ii;
  int index;

  index = -1;
  if(NULL != event && gtk_tree_view_get_path_at_pos(
       data->view, event->x, event->y, &path, NULL, NULL, NULL)) {
    if(gtk_tree_model_get_iter(GTK_TREE_MODEL(data->model), &iter, path))
      gtk_tree_model_get(GTK_TREE_MODEL(data->model), &iter, MC_ROW_INDEX, &index, -1);
    gtk_tree_path_free(path);
  }

  /* XXX am I leaking references here? */
  /* XXX can I cache this in cbdata? */
  for(ii = 0; ii < ALEN(actionitems); ii++) {
    item = gtk_menu_item_new_with_label(actionitems[ii].name);
    g_object_set_data(G_OBJECT(item), LIST_ACTION,
                      GINT_TO_POINTER(actionitems[ii].act));
    g_object_set_data(G_OBJECT(item), LIST_ACTION_FROM,
                      GINT_TO_POINTER(FROM_POPUP));
    g_object_set_data(G_OBJECT(item), LIST_INDEX, GINT_TO_POINTER(index));
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
actionclick(GtkWidget *widget, gpointer gdata) {
  struct cbdata *data = gdata;
  GtkTreeSelection *sel = gtk_tree_view_get_selection(data->view);
  GtkTreeModel *model;
  GtkTreeIter iter;
  enum listact act =
    GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), LIST_ACTION));
  enum listfrom from =
    GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), LIST_ACTION_FROM));
  int index;
  tr_stat_t *sb;

  switch(act) {
    case ACT_OPEN:
      makeaddwind(data);
      return;
    case ACT_PREF:
      makeprefwindow(data->wind, data->tr);
      return;
    default:
      break;
  }

  index = -1;
  switch(from) {
    case FROM_BUTTON:
      if(gtk_tree_selection_get_selected(sel, &model, &iter))
        gtk_tree_model_get(GTK_TREE_MODEL(model), &iter,
                           MC_ROW_INDEX, &index, -1);
      /* XXX should I assert(0 <= index) to insure a row was selected? */
      break;
    case FROM_POPUP:
      index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), LIST_INDEX));
      break;
    default:
      assert(!"unknown action source");
      break;
  }

  if(0 <= index) {
    switch(act) {
      case ACT_START:
        tr_torrentStart(data->tr, index);
        savetorrents(data->tr, data->wind, -1, NULL);
        break;
      case ACT_STOP:
        tr_torrentStop(data->tr, index);
        savetorrents(data->tr, data->wind, -1, NULL);
        break;
      case ACT_DELETE:
        /* XXX need to be able to stat just one torrent */
        if(index >= tr_torrentStat(data->tr, &sb)) {
          assert(!"XXX i'm tired");
        }
        if(TR_TORRENT_NEEDS_STOP(sb[index].status))
          tr_torrentStop(data->tr, index);
        free(sb);
        tr_torrentClose(data->tr, index);
        gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
        savetorrents(data->tr, data->wind, -1, NULL);
        break;
      case ACT_INFO:
        makeinfowind(data, index);
        break;
      default:
        assert(!"unknown type");
        break;
    }
  }
}

void
makeaddwind(struct cbdata *data) {
  GtkWidget *wind = gtk_file_selection_new("Add a Torrent");

  g_object_set_data(G_OBJECT(GTK_FILE_SELECTION(wind)->ok_button),
                    CBDATA_PTR, data);
  g_signal_connect(GTK_FILE_SELECTION(wind)->ok_button, "clicked",
                   G_CALLBACK(fileclick), wind);
  g_signal_connect_swapped(GTK_FILE_SELECTION(wind)->cancel_button, "clicked",
                           G_CALLBACK(gtk_widget_destroy), wind); 
  gtk_window_set_transient_for(GTK_WINDOW(wind), data->wind);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(wind), TRUE);
  gtk_window_set_modal(GTK_WINDOW(wind), TRUE);
  gtk_widget_show_all(wind);
}

gboolean
addtorrent(tr_handle_t *tr, GtkWindow *parentwind, const char *torrent,
           const char *dir, gboolean paused) {
  char *wd;

  if(NULL == dir) {
    dir = cf_getpref(PREF_DIR);
    if(!mkdir_p(dir, 0777)) {
      errmsg(parentwind, "Failed to create download directory %s:\n%s",
             dir, strerror(errno));
      return FALSE;
    }
  }

  if(0 != tr_torrentInit(tr, torrent)) {
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
  return TRUE;
}

void
fileclick(GtkWidget *widget, gpointer gdata) {
  struct cbdata *data = g_object_get_data(G_OBJECT(widget), CBDATA_PTR);
  GtkWidget *wind = gdata;
  const char *file = gtk_file_selection_get_filename(GTK_FILE_SELECTION(wind));

  if(addtorrent(data->tr, data->wind, file, NULL, FALSE)) {
    updatemodel(data);
    savetorrents(data->tr, data->wind, -1, NULL);
  }

  gtk_widget_destroy(wind);
}

const char *
statusstr(int status) {
  switch(status) {
    case TR_STATUS_CHECK:       return "check";
    case TR_STATUS_DOWNLOAD:    return "download";
    case TR_STATUS_SEED:        return "seed";
    case TR_STATUS_STOPPING:    return "stopping";
    case TR_STATUS_STOPPED:     return "stopped";
    case TR_STATUS_PAUSE:       return "pause";
    case TR_TRACKER_ERROR:      return "error";
    default:
      assert(!"unknown status code");
      return NULL;
  }
}

void
makeinfowind(struct cbdata *data, int index) {
  tr_stat_t *sb;
  GtkWidget *wind, *table, *name, *value;
  char buf[32];

  if(index >= tr_torrentStat(data->tr, &sb)) {
    assert(!"XXX i'm tired");
  }
  wind = gtk_dialog_new_with_buttons(sb[index].info.name, data->wind,
    GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);

  table = gtk_table_new(21, 2, FALSE);

  name = gtk_label_new("Torrent File");
  value = gtk_label_new(sb[index].info.torrent);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 0, 1);
  name = gtk_label_new("Name");
  value = gtk_label_new(sb[index].info.name);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 1, 2);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 1, 2);
  name = gtk_label_new("Tracker Address");
  value = gtk_label_new(sb[index].info.trackerAddress);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 2, 3);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 2, 3);
  name = gtk_label_new("Tracker Port");
  snprintf(buf, sizeof buf, "%i", sb[index].info.trackerPort);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 3, 4);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 3, 4);
  name = gtk_label_new("Tracker Announce URL");
  value = gtk_label_new(sb[index].info.trackerAnnounce);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 4, 5);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 4, 5);
  name = gtk_label_new("Piece Size");
  snprintf(buf, sizeof buf, "%i", sb[index].info.pieceSize);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 5, 6);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 5, 6);
  name = gtk_label_new("Piece Count");
  snprintf(buf, sizeof buf, "%i", sb[index].info.pieceCount);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 6, 7);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 6, 7);
  name = gtk_label_new("Total Size");
  snprintf(buf, sizeof buf, "%llu", sb[index].info.totalSize);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 7, 8);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 7, 8);
  name = gtk_label_new("File Count");
  snprintf(buf, sizeof buf, "%i", sb[index].info.fileCount);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 8, 9);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 8, 9);
  name = gtk_label_new("Status");
  value = gtk_label_new(statusstr(sb[index].status));
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 9, 10);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 9, 10);
  name = gtk_label_new("Error");
  value = gtk_label_new(sb[index].error);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 10, 11);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 10, 11);
  name = gtk_label_new("Progress");
  snprintf(buf, sizeof buf, "%f", sb[index].progress);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 11, 12);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 11, 12);
  name = gtk_label_new("Download Rate");
  value = gtk_label_new(buf);
  snprintf(buf, sizeof buf, "%f", sb[index].rateDownload);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 12, 13);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 12, 13);
  name = gtk_label_new("Upload Rate");
  snprintf(buf, sizeof buf, "%f", sb[index].rateUpload);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 13, 14);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 13, 14);
  name = gtk_label_new("ETA");
  snprintf(buf, sizeof buf, "%i", sb[index].eta);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 14, 15);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 14, 15);
  name = gtk_label_new("Total Peers");
  snprintf(buf, sizeof buf, "%i", sb[index].peersTotal);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 15, 16);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 15, 16);
  name = gtk_label_new("Uploading Peers");
  snprintf(buf, sizeof buf, "%i", sb[index].peersUploading);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 16, 17);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 16, 17);
  name = gtk_label_new("Downloading Peers");
  snprintf(buf, sizeof buf, "%i", sb[index].peersDownloading);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 17, 18);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 17, 18);
  name = gtk_label_new("Downloaded");
  snprintf(buf, sizeof buf, "%llu", sb[index].downloaded);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 18, 19);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 18, 19);
  name = gtk_label_new("Uploaded");
  snprintf(buf, sizeof buf, "%llu", sb[index].uploaded);
  value = gtk_label_new(buf);
  gtk_table_attach_defaults(GTK_TABLE(table), name, 0, 1, 19, 20);
  gtk_table_attach_defaults(GTK_TABLE(table), value, 1, 2, 19, 20);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(wind)->vbox), table);
  g_signal_connect(G_OBJECT(wind), "response",
                   G_CALLBACK(gtk_widget_destroy), NULL);
  gtk_widget_show_all(wind);
  free(sb);
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
    count = tr_torrentStat(tr, &st);
    ret = cf_savestate(count, st, &errstr);
    free(st);
  }

  if(!ret) {
    errmsg(wind, "%s", errstr);
    g_free(errstr);
  }

  return ret;
}
