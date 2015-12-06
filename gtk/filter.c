/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <stdlib.h> /* qsort () */

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include "favicon.h" /* gtr_get_favicon () */
#include "filter.h"
#include "hig.h" /* GUI_PAD */
#include "tr-core.h" /* MC_TORRENT */
#include "util.h" /* gtr_get_host_from_url () */

static GQuark DIRTY_KEY = 0;
static GQuark SESSION_KEY = 0;
static GQuark TEXT_KEY = 0;
static GQuark TORRENT_MODEL_KEY = 0;

/***
****
****  TRACKERS
****
***/

enum
{
  TRACKER_FILTER_TYPE_ALL,
  TRACKER_FILTER_TYPE_HOST,
  TRACKER_FILTER_TYPE_SEPARATOR,
};

enum
{
  TRACKER_FILTER_COL_NAME, /* human-readable name; ie, Legaltorrents */
  TRACKER_FILTER_COL_COUNT, /* how many matches there are */
  TRACKER_FILTER_COL_TYPE,
  TRACKER_FILTER_COL_HOST, /* pattern-matching text; ie, legaltorrents.com */
  TRACKER_FILTER_COL_PIXBUF,
  TRACKER_FILTER_N_COLS
};

static int
pstrcmp (const void * a, const void * b)
{
  return g_strcmp0 (* (const char* const *)a, * (const char* const *)b);
}

/* human-readable name; ie, Legaltorrents */
static char*
get_name_from_host (const char * host)
{
  char * name;
  const char * dot = strrchr (host, '.');

  if (tr_addressIsIP (host))
    name = g_strdup (host);
  else if (dot)
    name = g_strndup (host, dot - host);
  else
    name = g_strdup (host);

  *name = g_ascii_toupper (*name);

  return name;
}

static void
tracker_model_update_count (GtkTreeStore * store, GtkTreeIter * iter, int n)
{
  int count;
  GtkTreeModel * model = GTK_TREE_MODEL (store);
  gtk_tree_model_get (model, iter, TRACKER_FILTER_COL_COUNT, &count, -1);
  if (n != count)
    gtk_tree_store_set (store, iter, TRACKER_FILTER_COL_COUNT, n, -1);
}

static void
favicon_ready_cb (gpointer pixbuf, gpointer vreference)
{
  GtkTreeIter iter;
  GtkTreeRowReference * reference = vreference;

  if (pixbuf != NULL)
    {
      GtkTreePath * path = gtk_tree_row_reference_get_path (reference);
      GtkTreeModel * model = gtk_tree_row_reference_get_model (reference);

      if (gtk_tree_model_get_iter (model, &iter, path))
        gtk_tree_store_set (GTK_TREE_STORE (model), &iter,
                            TRACKER_FILTER_COL_PIXBUF, pixbuf,
                            -1);

      gtk_tree_path_free (path);

      g_object_unref (pixbuf);
    }

  gtk_tree_row_reference_free (reference);
}

static gboolean
tracker_filter_model_update (gpointer gstore)
{
  int i, n;
  int all = 0;
  int store_pos;
  GtkTreeIter iter;
  GObject * o = G_OBJECT (gstore);
  GtkTreeStore * store = GTK_TREE_STORE (gstore);
  GtkTreeModel * model = GTK_TREE_MODEL (gstore);
  GPtrArray * hosts = g_ptr_array_new ();
  GStringChunk * strings = g_string_chunk_new (4096);
  GHashTable * hosts_hash = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
  GtkTreeModel * tmodel = GTK_TREE_MODEL (g_object_get_qdata (o, TORRENT_MODEL_KEY));
  const int first_tracker_pos = 2; /* offset past the "All" and the separator */

  g_object_steal_qdata (o, DIRTY_KEY);

  /* Walk through all the torrents, tallying how many matches there are
   * for the various categories. Also make a sorted list of all tracker
   * hosts s.t. we can merge it with the existing list */
  if (gtk_tree_model_iter_nth_child (tmodel, &iter, NULL, 0)) do
    {
      tr_torrent * tor;
      const tr_info * inf;
      int keyCount;
      char ** keys;

      gtk_tree_model_get (tmodel, &iter, MC_TORRENT, &tor, -1);
      inf = tr_torrentInfo (tor);
      keyCount = 0;
      keys = g_new (char*, inf->trackerCount);

      for (i=0, n=inf->trackerCount; i<n; ++i)
        {
          int k;
          int * count;
          char buf[1024];
          char * key;

          gtr_get_host_from_url (buf, sizeof (buf), inf->trackers[i].announce);
          key = g_string_chunk_insert_const (strings, buf);

          count = g_hash_table_lookup (hosts_hash, key);
          if (count == NULL)
            {
              count = tr_new0 (int, 1);
              g_hash_table_insert (hosts_hash, key, count);
              g_ptr_array_add (hosts, key);
            }

          for (k=0; k<keyCount; ++k)
            if (!g_strcmp0 (keys[k], key))
              break;

          if (k==keyCount)
            keys[keyCount++] = key;
        }

      for (i=0; i<keyCount; ++i)
        {
          int * incrementme = g_hash_table_lookup (hosts_hash, keys[i]);
          ++*incrementme;
        }

      g_free (keys);

      ++all;
    }
  while (gtk_tree_model_iter_next (tmodel, &iter));

  qsort (hosts->pdata, hosts->len, sizeof (char*), pstrcmp);

  /* update the "all" count */
  if (gtk_tree_model_iter_children (model, &iter, NULL))
    tracker_model_update_count (store, &iter, all);

  store_pos = first_tracker_pos;
  for (i=0, n=hosts->len ; ;)
    {
      const gboolean new_hosts_done = i >= n;
      const gboolean old_hosts_done = !gtk_tree_model_iter_nth_child (model, &iter, NULL, store_pos);
      gboolean remove_row = FALSE;
      gboolean insert_row = FALSE;

      /* are we done yet? */
      if (new_hosts_done && old_hosts_done)
        break;

      /* decide what to do */
      if (new_hosts_done)
        {
          remove_row = TRUE;
        }
      else if (old_hosts_done)
        {
          insert_row = TRUE;
        }
      else
        {
          int cmp;
          char * host;
          gtk_tree_model_get (model, &iter, TRACKER_FILTER_COL_HOST, &host,  -1);
          cmp = g_strcmp0 (host, hosts->pdata[i]);

          if (cmp < 0)
            remove_row = TRUE;
          else if (cmp > 0)
            insert_row = TRUE;

          g_free (host);
        }

      /* do something */
      if (remove_row)
        {
          /* g_message ("removing row and incrementing i"); */
          gtk_tree_store_remove (store, &iter);
        }
      else if (insert_row)
        {
          GtkTreeIter add;
          GtkTreePath * path;
          GtkTreeRowReference * reference;
          tr_session * session = g_object_get_qdata (G_OBJECT (store), SESSION_KEY);
          const char * host = hosts->pdata[i];
          char * name = get_name_from_host (host);
          const int count = * (int*)g_hash_table_lookup (hosts_hash, host);
          gtk_tree_store_insert_with_values (store, &add, NULL, store_pos,
                                             TRACKER_FILTER_COL_HOST, host,
                                             TRACKER_FILTER_COL_NAME, name,
                                             TRACKER_FILTER_COL_COUNT, count,
                                             TRACKER_FILTER_COL_TYPE, TRACKER_FILTER_TYPE_HOST,
                                             -1);
          path = gtk_tree_model_get_path (model, &add);
          reference = gtk_tree_row_reference_new (model, path);
          gtr_get_favicon (session, host, favicon_ready_cb, reference);
          gtk_tree_path_free (path);
          g_free (name);
          ++store_pos;
          ++i;
        }
      else /* update row */
        {
          const char * host = hosts->pdata[i];
          const int count = * (int*)g_hash_table_lookup (hosts_hash, host);
          tracker_model_update_count (store, &iter, count);
          ++store_pos;
          ++i;
        }
    }

  /* cleanup */
  g_ptr_array_free (hosts, TRUE);
  g_hash_table_unref (hosts_hash);
  g_string_chunk_free (strings);
  return G_SOURCE_REMOVE;
}

static GtkTreeModel *
tracker_filter_model_new (GtkTreeModel * tmodel)
{
  GtkTreeStore * store = gtk_tree_store_new (TRACKER_FILTER_N_COLS,
                                             G_TYPE_STRING,
                                             G_TYPE_INT,
                                             G_TYPE_INT,
                                             G_TYPE_STRING,
                                             GDK_TYPE_PIXBUF);

  gtk_tree_store_insert_with_values (store, NULL, NULL, -1,
                                     TRACKER_FILTER_COL_NAME, _("All"),
                                     TRACKER_FILTER_COL_TYPE, TRACKER_FILTER_TYPE_ALL,
                                     -1);
  gtk_tree_store_insert_with_values (store, NULL, NULL, -1,
                                     TRACKER_FILTER_COL_TYPE, TRACKER_FILTER_TYPE_SEPARATOR,
                                     -1);

  g_object_set_qdata (G_OBJECT (store), TORRENT_MODEL_KEY, tmodel);
  tracker_filter_model_update (store);
  return GTK_TREE_MODEL (store);
}

static gboolean
is_it_a_separator (GtkTreeModel * m, GtkTreeIter * iter, gpointer data UNUSED)
{
  int type;
  gtk_tree_model_get (m, iter, TRACKER_FILTER_COL_TYPE, &type, -1);
  return type == TRACKER_FILTER_TYPE_SEPARATOR;
}

static void
tracker_model_update_idle (gpointer tracker_model)
{
  GObject * o = G_OBJECT (tracker_model);
  const gboolean pending = g_object_get_qdata (o, DIRTY_KEY) != NULL;
  if (!pending)
    {
      GSourceFunc func = tracker_filter_model_update;
      g_object_set_qdata (o, DIRTY_KEY, GINT_TO_POINTER (1));
      gdk_threads_add_idle (func, tracker_model);
    }
}

static void
torrent_model_row_changed (GtkTreeModel  * tmodel UNUSED,
                           GtkTreePath   * path UNUSED,
                           GtkTreeIter   * iter UNUSED,
                           gpointer        tracker_model)
{
  tracker_model_update_idle (tracker_model);
}

static void
torrent_model_row_deleted_cb (GtkTreeModel * tmodel UNUSED,
                              GtkTreePath  * path UNUSED,
                              gpointer       tracker_model)
{
  tracker_model_update_idle (tracker_model);
}

static void
render_pixbuf_func (GtkCellLayout    * cell_layout UNUSED,
                    GtkCellRenderer  * cell_renderer,
                    GtkTreeModel     * tree_model,
                    GtkTreeIter      * iter,
                    gpointer           data UNUSED)
{
  int type;
  int width;

  gtk_tree_model_get (tree_model, iter, TRACKER_FILTER_COL_TYPE, &type, -1);
  width = (type == TRACKER_FILTER_TYPE_HOST) ? 20 : 0;
  g_object_set (cell_renderer, "width", width, NULL);
}

static void
render_number_func (GtkCellLayout    * cell_layout UNUSED,
                    GtkCellRenderer  * cell_renderer,
                    GtkTreeModel     * tree_model,
                    GtkTreeIter      * iter,
                    gpointer           data UNUSED)
{
  int count;
  char buf[32];

  gtk_tree_model_get (tree_model, iter, TRACKER_FILTER_COL_COUNT, &count, -1);

  if (count >= 0)
    g_snprintf (buf, sizeof (buf), "%'d", count);
  else
    *buf = '\0';

  g_object_set (cell_renderer, "text", buf,
                               NULL);
}

static GtkCellRenderer *
number_renderer_new (void)
{
  GtkCellRenderer * r = gtk_cell_renderer_text_new ();

  g_object_set (G_OBJECT (r), "alignment", PANGO_ALIGN_RIGHT,
                              "weight", PANGO_WEIGHT_ULTRALIGHT,
                              "xalign", 1.0,
                              "xpad", GUI_PAD,
                              NULL);

  return r;
}

static void
disconnect_cat_model_callbacks (gpointer tmodel, GObject * cat_model)
{
  g_signal_handlers_disconnect_by_func (tmodel, torrent_model_row_changed, cat_model);
  g_signal_handlers_disconnect_by_func (tmodel, torrent_model_row_deleted_cb, cat_model);
}

static GtkWidget *
tracker_combo_box_new (GtkTreeModel * tmodel)
{
  GtkWidget * c;
  GtkCellRenderer * r;
  GtkTreeModel * cat_model;
  GtkCellLayout * c_cell_layout;
  GtkComboBox * c_combo_box;

  /* create the tracker combobox */
  cat_model = tracker_filter_model_new (tmodel);
  c = gtk_combo_box_new_with_model (cat_model);
  c_combo_box = GTK_COMBO_BOX (c);
  c_cell_layout = GTK_CELL_LAYOUT (c);
  gtk_combo_box_set_row_separator_func (c_combo_box,
                                        is_it_a_separator, NULL, NULL);
  gtk_combo_box_set_active (c_combo_box, 0);

  r = gtk_cell_renderer_pixbuf_new ();
  gtk_cell_layout_pack_start (c_cell_layout, r, FALSE);
  gtk_cell_layout_set_cell_data_func (c_cell_layout, r,
                                      render_pixbuf_func, NULL, NULL);
  gtk_cell_layout_set_attributes (c_cell_layout, r,
                                  "pixbuf", TRACKER_FILTER_COL_PIXBUF,
                                  NULL);

  r = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (c_cell_layout, r, FALSE);
  gtk_cell_layout_set_attributes (c_cell_layout, r,
                                  "text", TRACKER_FILTER_COL_NAME,
                                  NULL);

  r = number_renderer_new ();
  gtk_cell_layout_pack_end (c_cell_layout, r, TRUE);
  gtk_cell_layout_set_cell_data_func (c_cell_layout, r,
                                      render_number_func, NULL, NULL);

  g_object_weak_ref (G_OBJECT (cat_model), disconnect_cat_model_callbacks, tmodel);
  g_signal_connect (tmodel, "row-changed", G_CALLBACK (torrent_model_row_changed), cat_model);
  g_signal_connect (tmodel, "row-inserted", G_CALLBACK (torrent_model_row_changed), cat_model);
  g_signal_connect (tmodel, "row-deleted", G_CALLBACK (torrent_model_row_deleted_cb), cat_model);

  return c;
}

static gboolean
test_tracker (tr_torrent * tor, int active_tracker_type, const char * host)
{
  gboolean matches = TRUE;

  if (active_tracker_type == TRACKER_FILTER_TYPE_HOST)
    {
      unsigned int i;
      char tmp[1024];
      const tr_info * const inf = tr_torrentInfo (tor);

      for (i=0; i<inf->trackerCount; ++i)
        {
          gtr_get_host_from_url (tmp, sizeof (tmp), inf->trackers[i].announce);
          if (!g_strcmp0 (tmp, host))
            break;
        }

      matches = i < inf->trackerCount;
    }

  return matches;
}

/***
****
****  ACTIVITY
****
***/

enum
{
  ACTIVITY_FILTER_ALL,
  ACTIVITY_FILTER_DOWNLOADING,
  ACTIVITY_FILTER_SEEDING,
  ACTIVITY_FILTER_ACTIVE,
  ACTIVITY_FILTER_PAUSED,
  ACTIVITY_FILTER_FINISHED,
  ACTIVITY_FILTER_VERIFYING,
  ACTIVITY_FILTER_ERROR,
  ACTIVITY_FILTER_SEPARATOR
};

enum
{
  ACTIVITY_FILTER_COL_NAME,
  ACTIVITY_FILTER_COL_COUNT,
  ACTIVITY_FILTER_COL_TYPE,
  ACTIVITY_FILTER_COL_STOCK_ID,
  ACTIVITY_FILTER_N_COLS
};

static gboolean
activity_is_it_a_separator (GtkTreeModel * m, GtkTreeIter * i, gpointer d UNUSED)
{
  int type;
  gtk_tree_model_get (m, i, ACTIVITY_FILTER_COL_TYPE, &type, -1);
  return type == ACTIVITY_FILTER_SEPARATOR;
}

static gboolean
test_torrent_activity (tr_torrent * tor, int type)
{
  const tr_stat * st = tr_torrentStatCached (tor);

  switch (type)
    {
      case ACTIVITY_FILTER_DOWNLOADING:
        return (st->activity == TR_STATUS_DOWNLOAD)
            || (st->activity == TR_STATUS_DOWNLOAD_WAIT);

      case ACTIVITY_FILTER_SEEDING:
        return (st->activity == TR_STATUS_SEED)
            || (st->activity == TR_STATUS_SEED_WAIT);

      case ACTIVITY_FILTER_ACTIVE:
        return (st->peersSendingToUs > 0)
            || (st->peersGettingFromUs > 0)
            || (st->webseedsSendingToUs > 0)
            || (st->activity == TR_STATUS_CHECK);

      case ACTIVITY_FILTER_PAUSED:
        return st->activity == TR_STATUS_STOPPED;

      case ACTIVITY_FILTER_FINISHED:
        return st->finished == TRUE;

      case ACTIVITY_FILTER_VERIFYING:
        return (st->activity == TR_STATUS_CHECK)
            || (st->activity == TR_STATUS_CHECK_WAIT);

      case ACTIVITY_FILTER_ERROR:
        return st->error != 0;

      default: /* ACTIVITY_FILTER_ALL */
        return TRUE;
    }
}

static void
status_model_update_count (GtkListStore * store, GtkTreeIter * iter, int n)
{
  int count;
  GtkTreeModel * model = GTK_TREE_MODEL (store);
  gtk_tree_model_get (model, iter, ACTIVITY_FILTER_COL_COUNT, &count, -1);
  if (n != count)
    gtk_list_store_set (store, iter, ACTIVITY_FILTER_COL_COUNT, n, -1);
}

static gboolean
activity_filter_model_update (gpointer gstore)
{
  GtkTreeIter iter;
  GObject * o = G_OBJECT (gstore);
  GtkListStore * store = GTK_LIST_STORE (gstore);
  GtkTreeModel * model = GTK_TREE_MODEL (store);
  GtkTreeModel * tmodel = GTK_TREE_MODEL (g_object_get_qdata (o, TORRENT_MODEL_KEY));

  g_object_steal_qdata (o, DIRTY_KEY);

  if (gtk_tree_model_iter_nth_child (model, &iter, NULL, 0)) do
    {
      int hits;
      int type;
      GtkTreeIter torrent_iter;

      gtk_tree_model_get (model, &iter, ACTIVITY_FILTER_COL_TYPE, &type, -1);

      hits = 0;
      if (gtk_tree_model_iter_nth_child (tmodel, &torrent_iter, NULL, 0)) do
        {
          tr_torrent * tor;
          gtk_tree_model_get (tmodel, &torrent_iter, MC_TORRENT, &tor, -1);
          if (test_torrent_activity (tor, type))
            ++hits;
        }
      while (gtk_tree_model_iter_next (tmodel, &torrent_iter));

      status_model_update_count (store, &iter, hits);
    }
  while (gtk_tree_model_iter_next (model, &iter));

  return G_SOURCE_REMOVE;
}

static GtkTreeModel *
activity_filter_model_new (GtkTreeModel * tmodel)
{
  int i, n;
  struct {
    int type;
    const char * context;
    const char * name;
    const char * stock_id;
  } types[] = {
    { ACTIVITY_FILTER_ALL, NULL, N_("All"), NULL },
    { ACTIVITY_FILTER_SEPARATOR, NULL, NULL, NULL },
    { ACTIVITY_FILTER_ACTIVE, NULL, N_("Active"), GTK_STOCK_EXECUTE },
    { ACTIVITY_FILTER_DOWNLOADING, "Verb", NC_("Verb", "Downloading"), GTK_STOCK_GO_DOWN },
    { ACTIVITY_FILTER_SEEDING, "Verb", NC_("Verb", "Seeding"), GTK_STOCK_GO_UP },
    { ACTIVITY_FILTER_PAUSED, NULL, N_("Paused"), GTK_STOCK_MEDIA_PAUSE },
    { ACTIVITY_FILTER_FINISHED, NULL, N_("Finished"), NULL },
    { ACTIVITY_FILTER_VERIFYING, "Verb", NC_("Verb", "Verifying"), GTK_STOCK_REFRESH },
    { ACTIVITY_FILTER_ERROR, NULL, N_("Error"), GTK_STOCK_DIALOG_ERROR }
  };

  GtkListStore * store = gtk_list_store_new (ACTIVITY_FILTER_N_COLS,
                                             G_TYPE_STRING,
                                             G_TYPE_INT,
                                             G_TYPE_INT,
                                             G_TYPE_STRING);
  for (i=0, n=G_N_ELEMENTS (types); i<n; ++i)
    {
      const char * name = types[i].context ? g_dpgettext2 (NULL, types[i].context, types[i].name)
                                           : _ (types[i].name);
      gtk_list_store_insert_with_values (store, NULL, -1,
                                         ACTIVITY_FILTER_COL_NAME, name,
                                         ACTIVITY_FILTER_COL_TYPE, types[i].type,
                                         ACTIVITY_FILTER_COL_STOCK_ID, types[i].stock_id,
                                         -1);
    }

  g_object_set_qdata (G_OBJECT (store), TORRENT_MODEL_KEY, tmodel);
  activity_filter_model_update (store);
  return GTK_TREE_MODEL (store);
}

static void
render_activity_pixbuf_func (GtkCellLayout    * cell_layout UNUSED,
                             GtkCellRenderer  * cell_renderer,
                             GtkTreeModel     * tree_model,
                             GtkTreeIter      * iter,
                             gpointer           data UNUSED)
{
  int type;
  int width;
  int ypad;

  gtk_tree_model_get (tree_model, iter, ACTIVITY_FILTER_COL_TYPE, &type, -1);
  width = type == ACTIVITY_FILTER_ALL ? 0 : 20;
  ypad = type == ACTIVITY_FILTER_ALL ? 0 : 2;

  g_object_set (cell_renderer, "width", width,
                               "ypad", ypad,
                               NULL);
}

static void
activity_model_update_idle (gpointer activity_model)
{
  GObject * o = G_OBJECT (activity_model);
  const gboolean pending = g_object_get_qdata (o, DIRTY_KEY) != NULL;
  if (!pending)
    {
      GSourceFunc func = activity_filter_model_update;
      g_object_set_qdata (o, DIRTY_KEY, GINT_TO_POINTER (1));
      gdk_threads_add_idle (func, activity_model);
    }
}

static void
activity_torrent_model_row_changed (GtkTreeModel  * tmodel UNUSED,
                                    GtkTreePath   * path UNUSED,
                                    GtkTreeIter   * iter UNUSED,
                                    gpointer        activity_model)
{
  activity_model_update_idle (activity_model);
}

static void
activity_torrent_model_row_deleted_cb (GtkTreeModel  * tmodel UNUSED,
                                       GtkTreePath   * path UNUSED,
                                       gpointer        activity_model)
{
  activity_model_update_idle (activity_model);
}

static void
disconnect_activity_model_callbacks (gpointer tmodel, GObject * cat_model)
{
  g_signal_handlers_disconnect_by_func (tmodel, activity_torrent_model_row_changed, cat_model);
  g_signal_handlers_disconnect_by_func (tmodel, activity_torrent_model_row_deleted_cb, cat_model);
}

static GtkWidget *
activity_combo_box_new (GtkTreeModel * tmodel)
{
  GtkWidget * c;
  GtkCellRenderer * r;
  GtkTreeModel * activity_model;
  GtkComboBox * c_combo_box;
  GtkCellLayout * c_cell_layout;

  activity_model = activity_filter_model_new (tmodel);
  c = gtk_combo_box_new_with_model (activity_model);
  c_combo_box = GTK_COMBO_BOX (c);
  c_cell_layout = GTK_CELL_LAYOUT (c);
  gtk_combo_box_set_row_separator_func (c_combo_box,
                                        activity_is_it_a_separator, NULL, NULL);
  gtk_combo_box_set_active (c_combo_box, 0);

  r = gtk_cell_renderer_pixbuf_new ();
  gtk_cell_layout_pack_start (c_cell_layout, r, FALSE);
  gtk_cell_layout_set_attributes (c_cell_layout, r,
                                  "stock-id", ACTIVITY_FILTER_COL_STOCK_ID,
                                  NULL);
  gtk_cell_layout_set_cell_data_func (c_cell_layout, r,
                                      render_activity_pixbuf_func, NULL, NULL);

  r = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (c_cell_layout, r, TRUE);
  gtk_cell_layout_set_attributes (c_cell_layout, r,
                                  "text", ACTIVITY_FILTER_COL_NAME,
                                  NULL);

  r = number_renderer_new ();
  gtk_cell_layout_pack_end (c_cell_layout, r, TRUE);
  gtk_cell_layout_set_cell_data_func (c_cell_layout, r,
                                      render_number_func, NULL, NULL);

  g_object_weak_ref (G_OBJECT (activity_model), disconnect_activity_model_callbacks, tmodel);
  g_signal_connect (tmodel, "row-changed", G_CALLBACK (activity_torrent_model_row_changed), activity_model);
  g_signal_connect (tmodel, "row-inserted", G_CALLBACK (activity_torrent_model_row_changed), activity_model);
  g_signal_connect (tmodel, "row-deleted", G_CALLBACK (activity_torrent_model_row_deleted_cb), activity_model);

  return c;
}

/****
*****
*****  ENTRY FIELD
*****
****/

static gboolean
testText (const tr_torrent * tor, const char * key)
{
  gboolean ret = FALSE;

  if (!key || !*key)
    {
      ret = TRUE;
    }
  else
    {
      tr_file_index_t i;
      const tr_info * inf = tr_torrentInfo (tor);

      /* test the torrent name... */
      {
        char * pch = g_utf8_casefold (tr_torrentName (tor), -1);
        ret = !key || strstr (pch, key) != NULL;
        g_free (pch);
      }

      /* test the files... */
      for (i=0; i<inf->fileCount && !ret; ++i)
        {
          char * pch = g_utf8_casefold (inf->files[i].name, -1);
          ret = !key || strstr (pch, key) != NULL;
          g_free (pch);
        }
    }

  return ret;
}

static void
entry_clear (GtkEntry * e)
{
  gtk_entry_set_text (e, "");
}

static void
filter_entry_changed (GtkEditable * e, gpointer filter_model)
{
  char * pch;
  char * folded;

  pch = gtk_editable_get_chars (e, 0, -1);
  folded = g_utf8_casefold (pch, -1);
  g_strstrip (folded);
  g_object_set_qdata_full (filter_model, TEXT_KEY, folded, g_free);
  g_free (pch);

  gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (filter_model));
}

/*****
******
******
******
*****/

struct filter_data
{
  GtkWidget * activity;
  GtkWidget * tracker;
  GtkWidget * entry;
  GtkWidget * show_lb;
  GtkTreeModel * filter_model;
  int active_activity_type;
  int active_tracker_type;
  char * active_tracker_host;
};

static gboolean
is_row_visible (GtkTreeModel * model, GtkTreeIter * iter, gpointer vdata)
{
  const char * text;
  tr_torrent * tor;
  struct filter_data * data = vdata;
  GObject * o = G_OBJECT (data->filter_model);

  gtk_tree_model_get (model, iter, MC_TORRENT, &tor, -1);

  text = (const char*) g_object_get_qdata (o, TEXT_KEY);

  return (tor != NULL) && test_tracker (tor, data->active_tracker_type, data->active_tracker_host)
                       && test_torrent_activity (tor, data->active_activity_type)
                       && testText (tor, text);
}

static void
selection_changed_cb (GtkComboBox * combo, gpointer vdata)
{
  int type;
  char * host;
  GtkTreeIter iter;
  GtkTreeModel * model;
  struct filter_data * data = vdata;

  /* set data->active_activity_type from the activity combobox */
  combo = GTK_COMBO_BOX (data->activity);
  model = gtk_combo_box_get_model (combo);
  if (gtk_combo_box_get_active_iter (combo, &iter))
    gtk_tree_model_get (model, &iter, ACTIVITY_FILTER_COL_TYPE, &type, -1);
  else
    type = ACTIVITY_FILTER_ALL;
  data->active_activity_type = type;

  /* set the active tracker type & host from the tracker combobox */
  combo = GTK_COMBO_BOX (data->tracker);
  model = gtk_combo_box_get_model (combo);
  if (gtk_combo_box_get_active_iter (combo, &iter))
    {
      gtk_tree_model_get (model, &iter, TRACKER_FILTER_COL_TYPE, &type,
                                        TRACKER_FILTER_COL_HOST, &host,
                                        -1);
    }
  else
    {
      type = TRACKER_FILTER_TYPE_ALL;
      host = NULL;
    }
  g_free (data->active_tracker_host);
  data->active_tracker_host = host;
  data->active_tracker_type = type;

  /* refilter */
  gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (data->filter_model));
}

/***
****
***/

static gboolean
update_count_label (gpointer gdata)
{
  char buf[512];
  int visibleCount;
  int trackerCount;
  int activityCount;
  GtkTreeModel * model;
  GtkComboBox * combo;
  GtkTreeIter iter;
  struct filter_data * data = gdata;

  /* get the visible count */
  visibleCount = gtk_tree_model_iter_n_children (data->filter_model, NULL);

  /* get the tracker count */
  combo = GTK_COMBO_BOX (data->tracker);
  model = gtk_combo_box_get_model (combo);
  if (gtk_combo_box_get_active_iter (combo, &iter))
    gtk_tree_model_get (model, &iter, TRACKER_FILTER_COL_COUNT, &trackerCount, -1);
  else
    trackerCount = 0;

  /* get the activity count */
  combo = GTK_COMBO_BOX (data->activity);
  model = gtk_combo_box_get_model (combo);
  if (gtk_combo_box_get_active_iter (combo, &iter))
    gtk_tree_model_get (model, &iter, ACTIVITY_FILTER_COL_COUNT, &activityCount, -1);
  else
    activityCount = 0;

  /* set the text */
  if (visibleCount == MIN (activityCount, trackerCount))
    g_snprintf (buf, sizeof(buf), _("_Show:"));
  else
    g_snprintf (buf, sizeof(buf), _("_Show %'d of:"), visibleCount);
  gtk_label_set_markup_with_mnemonic (GTK_LABEL (data->show_lb), buf);

  g_object_steal_qdata (G_OBJECT(data->show_lb), DIRTY_KEY);
  return G_SOURCE_REMOVE;
}

static void
update_count_label_idle (struct filter_data * data)
{
  GObject * o = G_OBJECT (data->show_lb);
  const gboolean pending = g_object_get_qdata (o, DIRTY_KEY) != NULL;
  if (!pending)
    {
      g_object_set_qdata (o, DIRTY_KEY, GINT_TO_POINTER (1));
      gdk_threads_add_idle (update_count_label, data);
    }
}

static void
on_filter_model_row_inserted (GtkTreeModel * tree_model UNUSED,
                              GtkTreePath  * path       UNUSED,
                              GtkTreeIter  * iter       UNUSED,
                              gpointer       data)
{
  update_count_label_idle (data);
}

static void
on_filter_model_row_deleted (GtkTreeModel * tree_model UNUSED,
                             GtkTreePath  * path       UNUSED,
                             gpointer       data)
{
  update_count_label_idle (data);
}

/***
****
***/

GtkWidget *
gtr_filter_bar_new (tr_session * session, GtkTreeModel * tmodel, GtkTreeModel ** filter_model)
{
  GtkWidget * l;
  GtkWidget * w;
  GtkWidget * h;
  GtkWidget * s;
  GtkWidget * activity;
  GtkWidget * tracker;
  GtkBox * h_box;
  struct filter_data * data;

  g_assert (DIRTY_KEY == 0);
  TEXT_KEY = g_quark_from_static_string ("tr-filter-text-key");
  DIRTY_KEY = g_quark_from_static_string ("tr-filter-dirty-key");
  SESSION_KEY = g_quark_from_static_string ("tr-session-key");
  TORRENT_MODEL_KEY = g_quark_from_static_string ("tr-filter-torrent-model-key");

  data = g_new0 (struct filter_data, 1);
  data->show_lb = gtk_label_new (NULL);
  data->activity = activity = activity_combo_box_new (tmodel);
  data->tracker = tracker = tracker_combo_box_new (tmodel);
  data->filter_model = gtk_tree_model_filter_new (tmodel, NULL);
  g_signal_connect (data->filter_model, "row-deleted", G_CALLBACK(on_filter_model_row_deleted), data);
  g_signal_connect (data->filter_model, "row-inserted", G_CALLBACK(on_filter_model_row_inserted), data);

  g_object_set (G_OBJECT (data->tracker), "width-request", 170, NULL);
  g_object_set_qdata (G_OBJECT (gtk_combo_box_get_model (GTK_COMBO_BOX (data->tracker))), SESSION_KEY, session);

  gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (data->filter_model),
                                          is_row_visible, data, g_free);

  g_signal_connect (data->tracker, "changed", G_CALLBACK (selection_changed_cb), data);
  g_signal_connect (data->activity, "changed", G_CALLBACK (selection_changed_cb), data);


  h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, GUI_PAD_SMALL);
  h_box = GTK_BOX (h);

  /* add the activity combobox */
  w = activity;
  l = data->show_lb;
  gtk_label_set_mnemonic_widget (GTK_LABEL (l), w);
  gtk_box_pack_start (h_box, l, FALSE, FALSE, 0);
  gtk_box_pack_start (h_box, w, TRUE, TRUE, 0);

  /* add a spacer */
  w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
  gtk_widget_set_size_request (w, 0u, GUI_PAD_BIG);
  gtk_box_pack_start (h_box, w, FALSE, FALSE, 0);

  /* add the tracker combobox */
  w = tracker;
  gtk_box_pack_start (h_box, w, TRUE, TRUE, 0);

  /* add a spacer */
  w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
  gtk_widget_set_size_request (w, 0u, GUI_PAD_BIG);
  gtk_box_pack_start (h_box, w, FALSE, FALSE, 0);

  /* add the entry field */
  s = gtk_entry_new ();
  gtk_entry_set_icon_from_stock (GTK_ENTRY (s), GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_CLEAR);
  g_signal_connect (s, "icon-release", G_CALLBACK (entry_clear), NULL);
  gtk_box_pack_start (h_box, s, TRUE, TRUE, 0);

  g_signal_connect (s, "changed", G_CALLBACK (filter_entry_changed), data->filter_model);
  selection_changed_cb (NULL, data);

  *filter_model = data->filter_model;
  update_count_label (data);
  return h;
}
