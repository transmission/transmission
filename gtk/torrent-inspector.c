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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>

#include "actions.h"
#include "tr_torrent.h"
#include "hig.h"
#include "torrent-inspector.h"
#include "util.h"

#define UPDATE_INTERVAL_MSEC 2000

/****
*****  PIECES VIEW
****/

static int
getGridSize (int pieceCount, int * n_rows, int * n_cols)
{
  const int MAX_ACROSS = 16;
  if (pieceCount >= (MAX_ACROSS * MAX_ACROSS)) {
    *n_rows = *n_cols = MAX_ACROSS;
    return MAX_ACROSS * MAX_ACROSS;
  }
  else {
    int i;
    for (i=0; (i*i) < pieceCount; ++i);
    *n_rows = *n_cols = i;
    return pieceCount;
  }
}

#define TO16(a) ((guint16)((a<<8)|(a)))
#define RGB_2_GDK(R,G,B) { 0, TO16(R), TO16(G), TO16(B) }

enum { DRAW_AVAIL, DRAW_PROG };

static void
release_gobject_array (gpointer data)
{
  int i;
  GObject **objects = (GObject**) data;
  for (i=0; objects[i]!=NULL; ++i)
    g_object_unref (G_OBJECT(objects[i]));
  g_free (objects);
}

static gboolean
refresh_pieces (GtkWidget * da, GdkEventExpose * event UNUSED, gpointer gtor)
{
  tr_torrent * tor = tr_torrent_handle( TR_TORRENT(gtor) );
  const tr_info * info = tr_torrent_info( TR_TORRENT(gtor) );
  int mode = GPOINTER_TO_INT (g_object_get_data (G_OBJECT(da), "draw-mode"));

  GdkColormap * colormap = gtk_widget_get_colormap (da);
  const int widget_w = da->allocation.width;
  const int widget_h = da->allocation.height;
  int n_rows, n_cols;
  const int n_cells = getGridSize (info->pieceCount,  &n_rows, &n_cols);
  const GdkRectangle grid_bounds = { 0, 0, widget_w, widget_h };
  const double piece_w = grid_bounds.width / (double)n_cols;
  const double piece_h = grid_bounds.height / (double)n_rows;
  const int piece_w_int = (int) (piece_w + 1); /* pad for roundoff */
  const int piece_h_int = (int) (piece_h + 1); /* pad for roundoff */
  guint8 * prev_color = NULL;
  gboolean first_time = FALSE;

  int i, x, y;
  int8_t * pieces = NULL;
  float * completeness = NULL;

  /**
  ***  Get the Graphics Contexts...
  **/

  enum { ALL, LOTS, SOME, FEW, NONE,
         BLACK, GRAY, BLINK,
         N_COLORS };
  GdkGC **gcs = (GdkGC**) g_object_get_data (G_OBJECT(da), "graphics-contexts");
  if (gcs == NULL)
  {
    const GdkColor colors [N_COLORS] = {
      RGB_2_GDK ( 114, 159, 207 ), /* all */
      RGB_2_GDK (  52, 101, 164 ), /* lots */
      RGB_2_GDK (  32,  74, 135 ), /* some */
      RGB_2_GDK (  85,  87,  83 ), /* few */
      RGB_2_GDK ( 238, 238, 236 ), /* none - tango aluminum highlight */
      RGB_2_GDK (  46,  52,  54 ), /* black - tango slate shadow */
      RGB_2_GDK ( 186, 189, 182 ), /* gray - tango aluminum shadow */
      RGB_2_GDK ( 252, 233,  79 ), /* blink - tango butter highlight */
    };

    gcs = g_new (GdkGC*, N_COLORS+1);

    for (i=0; i<N_COLORS; ++i) {
      gcs[i] = gdk_gc_new (da->window);
      gdk_gc_set_colormap (gcs[i], colormap);
      gdk_gc_set_rgb_fg_color (gcs[i], &colors[i]);
      gdk_gc_set_rgb_bg_color (gcs[i], &colors[i]);
    };

    gcs[N_COLORS] = NULL; /* a sentinel in the release function */
    g_object_set_data_full (G_OBJECT(da), "graphics-contexts",
                            gcs, release_gobject_array);
  }

  /**
  ***  Get the cells' previous colors...
  ***  (this is used for blinking when the color changes)
  **/

  prev_color = (guint8*) g_object_get_data (G_OBJECT(da), "prev-color");
  if (prev_color == NULL)
  {
    first_time = TRUE;
    prev_color = g_new0 (guint8, n_cells);
    g_object_set_data_full (G_OBJECT(da), "prev-color", prev_color, g_free);
  }

  /**
  ***  Get the piece data values...
  **/

  switch (mode) {
    case DRAW_AVAIL:
      pieces = g_new (int8_t, n_cells);
      tr_torrentAvailability ( tor, pieces, n_cells );
      break;
    case DRAW_PROG:
      completeness = g_new (float, n_cells);
      tr_torrentAmountFinished ( tor, completeness, n_cells );
      break;
    default: g_error("no mode defined!");
  }

  /**
  ***  Draw...
  **/

  i = 0; 
  for (y=0; y<n_rows; ++y) {
    for (x=0; x<n_cols; ++x) {
      const int draw_x = grid_bounds.x + (int)(x * piece_w);
      const int draw_y = grid_bounds.y + (int)(y * piece_h);
      int color = BLACK;
      int border = BLACK;

      if (i < n_cells)
      {
        border = GRAY;

        if (mode == DRAW_AVAIL) {
          const int8_t val = pieces[i];
               if (val <  0) color = ALL;
          else if (val == 0) color = NONE;
          else if (val <= 4) color = FEW;
          else if (val <= 8) color = SOME;
          else               color = LOTS;
        } else { /* completeness */
          const float val = completeness[i];
               if (val >= 1.00) color = ALL;
          else if (val >= 0.66) color = LOTS;
          else if (val >= 0.33) color = SOME;
          else if (val >= 0.01) color = FEW;
          else                  color = NONE;
        }

        /* draw a "blink" for one interval when a piece changes */
        if (first_time)
          prev_color[i] = color;
        else if (color != prev_color[i]) {
          prev_color[i] = color;
          color = border = BLINK;
        }
      }

      gdk_draw_rectangle (da->window, gcs[color], TRUE,
                          draw_x, draw_y,
                          piece_w_int, piece_h_int);

      if (i < n_cells)
        gdk_draw_rectangle (da->window, gcs[border], FALSE,
                            draw_x, draw_y,
                            piece_w_int, piece_h_int);

      ++i;
    }
  }

  gdk_draw_rectangle (da->window, gcs[GRAY], FALSE,
                      grid_bounds.x, grid_bounds.y, 
                      grid_bounds.width-1, grid_bounds.height-1);

  g_free (pieces);
  g_free (completeness);
  return FALSE;
}

/****
*****  PEERS TAB
****/

enum
{
  PEER_COL_ADDRESS,
  PEER_COL_CLIENT,
  PEER_COL_PROGRESS,
  PEER_COL_IS_ENCRYPTED,
  PEER_COL_DOWNLOAD_RATE,
  PEER_COL_UPLOAD_RATE,
  PEER_COL_STATUS,
  N_PEER_COLS
};

static const char* peer_column_names[N_PEER_COLS] =
{
  N_("Address"),
  N_("Client"),
  N_("%"),
  " ",
  N_("Down"),
  N_("Up"),
  N_("Status")
};

static int compare_peers (const void * a, const void * b)
{
  const tr_peer_stat * pa = a;
  const tr_peer_stat * pb = b;
  return strcmp (pa->addr, pb->addr);
}
static int compare_addr_to_peer (const void * a, const void * b)
{
  const char * addr = (const char *) a;
  const tr_peer_stat * peer = b;
  return strcmp (addr, peer->addr);
}

static void
peer_row_set (GtkTreeStore        * store,
              GtkTreeIter         * iter,
              const tr_peer_stat  * peer)
{
  const char * client = peer->client;

  if (!client || !strcmp(client,"Unknown Client"))
    client = " ";

  gtk_tree_store_set (store, iter,
                      PEER_COL_ADDRESS, peer->addr,
                      PEER_COL_CLIENT, client,
                      PEER_COL_IS_ENCRYPTED, peer->isEncrypted,
                      PEER_COL_PROGRESS, (int)(100.0*peer->progress),
                      PEER_COL_DOWNLOAD_RATE, peer->downloadFromRate,
                      PEER_COL_UPLOAD_RATE, peer->uploadToRate,
                      PEER_COL_STATUS, peer->flagStr,
                      -1);
}

static void
append_peers_to_model (GtkTreeStore        * store,
                       const tr_peer_stat  * peers,
                       int                   n_peers)
{
  int i;
  for (i=0; i<n_peers; ++i) {
    GtkTreeIter iter;
    gtk_tree_store_append (store, &iter, NULL);
    peer_row_set (store, &iter, &peers[i]);
  }
}

static GtkTreeModel*
peer_model_new (tr_torrent * tor)
{
  GtkTreeStore * m = gtk_tree_store_new (N_PEER_COLS,
                                         G_TYPE_STRING,   /* addr */
                                         G_TYPE_STRING,   /* client */
                                         G_TYPE_INT,      /* progress [0..100] */
                                         G_TYPE_BOOLEAN,  /* isEncrypted */
                                         G_TYPE_FLOAT,    /* downloadFromRate */
                                         G_TYPE_FLOAT,    /* uploadToRate */
                                         G_TYPE_STRING ); /* flagString */

  int n_peers = 0;
  tr_peer_stat * peers = tr_torrentPeers (tor, &n_peers);
  qsort (peers, n_peers, sizeof(tr_peer_stat), compare_peers);
  append_peers_to_model (m, peers, n_peers);
  tr_torrentPeersFree( peers, 0 );
  return GTK_TREE_MODEL (m);
}

static void
render_encrypted (GtkTreeViewColumn  * column UNUSED,
                  GtkCellRenderer    * renderer,
                  GtkTreeModel       * tree_model,
                  GtkTreeIter        * iter,
                  gpointer             data UNUSED)
{
  gboolean is_encrypted = FALSE;
  gtk_tree_model_get (tree_model, iter, PEER_COL_IS_ENCRYPTED, &is_encrypted,
                                        -1);
  g_object_set (renderer, "xalign", (gfloat)0.0,
                          "yalign", (gfloat)0.5,
                          "stock-id", (is_encrypted ? "transmission-lock" : NULL),
                          NULL);
}

static void
render_ul_rate (GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * tree_model,
                GtkTreeIter        * iter,
                gpointer             data UNUSED)
{
  float rate = 0.0;
  gtk_tree_model_get (tree_model, iter, PEER_COL_UPLOAD_RATE, &rate, -1);
  if( rate < 0.01 )
    g_object_set (renderer, "text", "", NULL);
  else {
    char speedStr[64];
    tr_strlspeed( speedStr, rate, sizeof(speedStr) );
    g_object_set( renderer, "text", speedStr, NULL );
  }
}

static void
render_dl_rate (GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * tree_model,
                GtkTreeIter        * iter,
                gpointer             data UNUSED)
{
  float rate = 0.0;
  gtk_tree_model_get (tree_model, iter, PEER_COL_DOWNLOAD_RATE, &rate, -1);
  if( rate < 0.01 )
    g_object_set (renderer, "text", "", NULL);
  else {
    char speedStr[64];
    tr_strlspeed( speedStr, rate, sizeof(speedStr) );
    g_object_set( renderer, "text", speedStr, NULL );
  }
}

static void
render_client (GtkTreeViewColumn   * column UNUSED,
               GtkCellRenderer     * renderer,
               GtkTreeModel        * tree_model,
               GtkTreeIter         * iter,
               gpointer              data UNUSED)
{
  char * client = NULL;
  gtk_tree_model_get (tree_model, iter, PEER_COL_CLIENT, &client,
                                        -1);
  g_object_set (renderer, "text", (client ? client : ""), NULL);
  g_free (client);
}

typedef struct
{
  TrTorrent * gtor;
  GtkTreeModel * model; /* same object as store, but recast */
  GtkTreeStore * store; /* same object as model, but recast */
  GtkWidget * completeness;
  GtkWidget * seeders_lb;
  GtkWidget * leechers_lb;
  GtkWidget * completed_lb;
}
PeerData;

static void
fmtpeercount (GtkWidget * l, int count)
{
  if( 0 > count ) {
    gtk_label_set_text( GTK_LABEL(l), "?" );
  } else {
    char str[16];
    g_snprintf( str, sizeof str, "%i", count );
    gtk_label_set_text( GTK_LABEL(l), str );
  }
}

static void
refresh_peers (GtkWidget * top)
{
  int i;
  int n_peers;
  GtkTreeIter iter;
  PeerData * p = (PeerData*) g_object_get_data (G_OBJECT(top), "peer-data");
  tr_torrent * tor = tr_torrent_handle ( p->gtor );
  GtkTreeModel * model = p->model;
  GtkTreeStore * store = p->store;
  tr_peer_stat * peers;
  const tr_stat * stat = tr_torrent_stat( p->gtor );

  /**
  ***  merge the peer diffs into the tree model.
  ***
  ***  this is more complicated than creating a new model,
  ***  but is also (a) more efficient and (b) doesn't undo
  ***  the view's visible area and sorting on every refresh.
  **/

  n_peers = 0;
  peers = tr_torrentPeers (tor, &n_peers);
  qsort (peers, n_peers, sizeof(tr_peer_stat), compare_peers);

  i = 0;
  if (gtk_tree_model_get_iter_first (model, &iter)) do
  {
    char * addr = NULL;
    tr_peer_stat * peer = NULL;
    gtk_tree_model_get (model, &iter, PEER_COL_ADDRESS, &addr, -1);
    peer = bsearch (addr, peers, n_peers, sizeof(tr_peer_stat),
                    compare_addr_to_peer);
    g_free (addr);

    if (peer) /* update a pre-existing row */
    {
      const int pos = peer - peers;
      const int n_rhs = n_peers - (pos+1);
      g_assert (n_rhs >= 0);

      peer_row_set (store, &iter, peer);

      /* remove it from the tr_peer_stat list */
      g_memmove (peer, peer+1, sizeof(tr_peer_stat)*n_rhs);
      --n_peers;
    }
    else if (!gtk_tree_store_remove (store, &iter))
      break; /* we removed the model's last item */
  }
  while (gtk_tree_model_iter_next (model, &iter));

  append_peers_to_model (store, peers, n_peers);  /* all these are new */

  if (GDK_IS_DRAWABLE (p->completeness->window))
    refresh_pieces (p->completeness, NULL, p->gtor);

  fmtpeercount (p->seeders_lb, stat->seeders);
  fmtpeercount (p->leechers_lb, stat->leechers);
  fmtpeercount (p->completed_lb, stat->completedFromTracker );

  free( peers );
}

static GtkWidget* peer_page_new ( TrTorrent * gtor )
{
  guint i;
  GtkTreeModel *m;
  GtkWidget *h, *v, *w, *ret, *da, *sw, *l, *vbox, *hbox;
  tr_torrent * tor = tr_torrent_handle (gtor);
  PeerData * p = g_new (PeerData, 1);
  char name[64];

  /* TODO: make this configurable? */
  int view_columns[] = { PEER_COL_IS_ENCRYPTED,
                         PEER_COL_UPLOAD_RATE,
                         PEER_COL_DOWNLOAD_RATE,
                         PEER_COL_PROGRESS,
                         PEER_COL_STATUS,
                         PEER_COL_ADDRESS,
                         PEER_COL_CLIENT };

  m  = peer_model_new (tor);
  v = gtk_tree_view_new_with_model (m);
  gtk_tree_view_set_rules_hint (GTK_TREE_VIEW(v), TRUE);
  g_object_unref (G_OBJECT(m));

  for (i=0; i<G_N_ELEMENTS(view_columns); ++i)
  {
    const int col = view_columns[i];
    const char * t = _(peer_column_names[col]);
    gboolean resizable = TRUE;
    GtkTreeViewColumn * c;
    GtkCellRenderer * r;

    switch (col)
    {
      case PEER_COL_ADDRESS:
        r = gtk_cell_renderer_text_new ();
        g_object_set( G_OBJECT( r ), "ellipsize", PANGO_ELLIPSIZE_END, NULL );
        c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
        break;

      case PEER_COL_CLIENT:
        r = gtk_cell_renderer_text_new ();
        g_object_set( G_OBJECT( r ), "ellipsize", PANGO_ELLIPSIZE_END, NULL );
        c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
        gtk_tree_view_column_set_cell_data_func (c, r, render_client,
                                                 NULL, NULL);
        break;

      case PEER_COL_PROGRESS:
        r = gtk_cell_renderer_progress_new ();
        c = gtk_tree_view_column_new_with_attributes (t, r, "value", PEER_COL_PROGRESS, NULL);
        break;

      case PEER_COL_IS_ENCRYPTED:
        resizable = FALSE;
        r = gtk_cell_renderer_pixbuf_new ();
        c = gtk_tree_view_column_new_with_attributes (t, r, NULL);
        gtk_tree_view_column_set_sizing (c, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_column_set_fixed_width (c, 20);
        gtk_tree_view_column_set_cell_data_func (c, r, render_encrypted,
                                                 NULL, NULL);
        break;

      case PEER_COL_DOWNLOAD_RATE:
        r = gtk_cell_renderer_text_new ();
        c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
        gtk_tree_view_column_set_cell_data_func (c, r, render_dl_rate,
                                                 NULL, NULL);
        break;

      case PEER_COL_UPLOAD_RATE:
        r = gtk_cell_renderer_text_new ();
        c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
        gtk_tree_view_column_set_cell_data_func (c, r, render_ul_rate,
                                                 NULL, NULL);
        break;

      case PEER_COL_STATUS:
        r = gtk_cell_renderer_text_new( );
        c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
        break;

      default:
        abort ();
    }

    gtk_tree_view_column_set_resizable (c, resizable);
    gtk_tree_view_column_set_sort_column_id (c, col);
    gtk_tree_view_append_column (GTK_TREE_VIEW(v), c);
  }

  /* the 'expander' column has a 10-pixel margin on the left
     that doesn't look quite correct in any of these columns...
     so create a non-visible column and assign it as the
     'expander column. */
  {
    GtkTreeViewColumn *c = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_visible (c, FALSE);
    gtk_tree_view_append_column (GTK_TREE_VIEW(v), c);
    gtk_tree_view_set_expander_column (GTK_TREE_VIEW(v), c);
  }

  w = sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(w),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(w),
                                       GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(w), v);


  vbox = gtk_vbox_new (FALSE, GUI_PAD);
  gtk_container_set_border_width (GTK_CONTAINER(vbox), GUI_PAD_BIG);

    g_snprintf (name, sizeof(name), "<b>%s</b>", _("Piece Availability"));
    l = gtk_label_new (NULL);
    gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
    gtk_label_set_markup (GTK_LABEL(l), name);
    gtk_box_pack_start (GTK_BOX(vbox), l, FALSE, FALSE, 0);

    w = da = p->completeness = gtk_drawing_area_new ();
    gtk_widget_set_size_request (w, 0u, 100u);
    g_object_set_data (G_OBJECT(w), "draw-mode", GINT_TO_POINTER(DRAW_AVAIL));
    g_signal_connect (w, "expose-event", G_CALLBACK(refresh_pieces), gtor);

    h = gtk_hbox_new (FALSE, GUI_PAD);
    w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
    gtk_widget_set_size_request (w, GUI_PAD_BIG, 0);
    gtk_box_pack_start (GTK_BOX(h), w, FALSE, FALSE, 0);
    gtk_box_pack_start_defaults (GTK_BOX(h), da);
    gtk_box_pack_start (GTK_BOX(vbox), h, FALSE, FALSE, 0);

    /* a small vertical spacer */
    w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
    gtk_widget_set_size_request (w, 0u, GUI_PAD);
    gtk_box_pack_start (GTK_BOX(vbox), w, FALSE, FALSE, 0);

    g_snprintf (name, sizeof(name), "<b>%s</b>", _("Connected Peers"));
    l = gtk_label_new (NULL);
    gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
    gtk_label_set_markup (GTK_LABEL(l), name);
    gtk_box_pack_start (GTK_BOX(vbox), l, FALSE, FALSE, 0);

    h = gtk_hbox_new (FALSE, GUI_PAD);
    w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
    gtk_widget_set_size_request (w, GUI_PAD_BIG, 0);
    gtk_box_pack_start (GTK_BOX(h), w, FALSE, FALSE, 0);
    gtk_box_pack_start_defaults (GTK_BOX(h), sw);
    gtk_box_pack_start_defaults (GTK_BOX(vbox), h);

    hbox = gtk_hbox_new (FALSE, GUI_PAD);
    w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
    gtk_widget_set_size_request (w, GUI_PAD_BIG, 0);
    gtk_box_pack_start (GTK_BOX(hbox), w, FALSE, FALSE, 0);
        g_snprintf (name, sizeof(name), "<b>%s:</b>", _("Seeders"));
        l = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL(l), name);
        gtk_box_pack_start (GTK_BOX(hbox), l, FALSE, FALSE, 0);
        l = p->seeders_lb = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX(hbox), l, FALSE, FALSE, 0);
    gtk_box_pack_start_defaults (GTK_BOX(hbox),
                                 gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f));
        g_snprintf (name, sizeof(name), "<b>%s:</b>", _("Leechers"));
        l = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL(l), name);
        gtk_box_pack_start (GTK_BOX(hbox), l, FALSE, FALSE, 0);
        l = p->leechers_lb = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX(hbox), l, FALSE, FALSE, 0);
    gtk_box_pack_start_defaults (GTK_BOX(hbox),
                                 gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f));
        g_snprintf (name, sizeof(name), "<b>%s:</b>", _("Completed"));
        l = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL(l), name);
        gtk_box_pack_start (GTK_BOX(hbox), l, FALSE, FALSE, 0);
        l = p->completed_lb = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX(hbox), l, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  ret = vbox;
  p->gtor = gtor;
  p->model = m;
  p->store = GTK_TREE_STORE(m);
  g_object_set_data_full (G_OBJECT(ret), "peer-data", p, g_free);
  return ret;
}

/****
*****  INFO TAB
****/

static GtkWidget* info_page_new (tr_torrent * tor)
{
  int row = 0;
  GtkWidget *t = hig_workarea_create ();
  GtkWidget *l, *w, *fr;
  char *pch;
  char *dname, *bname;
  char sizeStr[128];
  char buf[256];
  char name[128];
  const char * namefmt = "%s:";
  GtkTextBuffer * b;
  tr_tracker_info * track;
  const tr_info * info = tr_torrentInfo(tor);

  hig_workarea_add_section_title (t, &row, _("Torrent Information"));
  hig_workarea_add_section_spacer (t, row, 5);

    g_snprintf (name, sizeof(name), namefmt, _("Tracker"));
    track = info->trackerList->list;
    pch = track->port==80
      ? g_strdup_printf( "http://%s%s", track->address, track->announce )
      : g_strdup_printf( "http://%s:%d%s", track->address, track->port, track->announce );
    l = gtk_label_new( pch );
    gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );

    hig_workarea_add_row (t, &row, name, l, NULL);
    g_free (pch);

    g_snprintf (name, sizeof(name), namefmt, _("Pieces"));
    tr_strlsize( sizeStr, info->pieceSize, sizeof(sizeStr) );
    g_snprintf( buf, sizeof(buf), "%d (%s)", info->pieceCount, sizeStr );
    l = gtk_label_new (buf);
    hig_workarea_add_row (t, &row, name, l, NULL);

    g_snprintf (name, sizeof(name), namefmt, _("Hash"));
    l = gtk_label_new (info->hashString);
    gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
    hig_workarea_add_row (t, &row, name, l, NULL);

    g_snprintf (name, sizeof(name), namefmt, _("Secure"));
    pch = (info->isPrivate )
      ? _("Private Torrent, PEX disabled")
      : _("Public Torrent");
    l = gtk_label_new (pch);
    hig_workarea_add_row (t, &row, name, l, NULL);

    g_snprintf (name, sizeof(name), namefmt, _("Comment"));
    b = gtk_text_buffer_new (NULL);
    gtk_text_buffer_set_text (b, info->comment, -1);
    w = gtk_text_view_new_with_buffer (b);
    gtk_widget_set_size_request (w, 0u, 100u);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(w), GTK_WRAP_WORD); 
    gtk_text_view_set_editable (GTK_TEXT_VIEW(w), FALSE);
    fr = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME(fr), GTK_SHADOW_IN);
    gtk_container_add (GTK_CONTAINER(fr), w);
    hig_workarea_add_row (t, &row, name, fr, NULL);

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Created By"));
  hig_workarea_add_section_spacer (t, row, 2);
  
    g_snprintf (name, sizeof(name), namefmt, _("Creator"));
    l = gtk_label_new (*info->creator ? info->creator : _("N/A"));
    hig_workarea_add_row (t, &row, name, l, NULL);

    g_snprintf (name, sizeof(name), namefmt, _("Date"));
    pch = rfc822date ((guint64)info->dateCreated * 1000u);
    l = gtk_label_new (pch);
    hig_workarea_add_row (t, &row, name, l, NULL); 
    g_free (pch);

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Location"));
  hig_workarea_add_section_spacer (t, row, 3);

    g_snprintf (name, sizeof(name), namefmt, _("Downloaded Data"));
    l = gtk_label_new (tr_torrentGetFolder (tor));
    gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
    hig_workarea_add_row (t, &row, name, l, NULL); 

    g_snprintf (name, sizeof(name), namefmt, _("Torrent File Path"));
    dname = g_path_get_dirname (info->torrent);
    l = gtk_label_new ( dname );
    gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
    hig_workarea_add_row (t, &row, name, l, NULL); 
    g_free (dname);

    g_snprintf (name, sizeof(name), namefmt, _("Torrent File Name"));
    bname = g_path_get_basename (info->torrent);
    l = gtk_label_new (bname);
    gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
    hig_workarea_add_row (t, &row, name, l, NULL); 
    g_free (bname);

  hig_workarea_finish (t, &row);
  return t;
}

/****
*****  ACTIVITY TAB
****/

typedef struct
{
  GtkWidget * state_lb;
  GtkWidget * progress_lb;
  GtkWidget * have_lb;
  GtkWidget * dl_lb;
  GtkWidget * ul_lb;
  GtkWidget * failed_lb;
  GtkWidget * ratio_lb;
  GtkWidget * err_lb;
  GtkWidget * swarm_lb;
  GtkWidget * date_added_lb;
  GtkWidget * last_activity_lb;
  GtkWidget * availability_da;
  TrTorrent * gtor;
}
Activity;

static void
refresh_activity (GtkWidget * top)
{
  Activity * a = (Activity*) g_object_get_data (G_OBJECT(top), "activity-data");
  const tr_stat * stat = tr_torrent_stat( a->gtor );
  char *pch;
  char sizeStr[64];
  char sizeStr2[64];
  char buf[128];

  pch = tr_torrent_status_str( a->gtor );
  gtk_label_set_text (GTK_LABEL(a->state_lb), pch);
  g_free (pch);

  pch = g_strdup_printf( _( "%.1f%% (%.1f%% selected)" ), stat->percentComplete*100.0, stat->percentDone*100.0 );
  gtk_label_set_text (GTK_LABEL(a->progress_lb), pch);
  g_free (pch);

  tr_strlsize( sizeStr,  stat->haveValid + stat->haveUnchecked, sizeof(sizeStr) );
  tr_strlsize( sizeStr2, stat->haveValid,                       sizeof(sizeStr2) );
  g_snprintf( buf, sizeof(buf), _("%s (%s verified)"), sizeStr, sizeStr2 );
  gtk_label_set_text( GTK_LABEL( a->have_lb ), buf );

  tr_strlsize( sizeStr, stat->downloadedEver, sizeof(sizeStr) );
  gtk_label_set_text( GTK_LABEL(a->dl_lb), sizeStr );

  tr_strlsize( sizeStr, stat->uploadedEver, sizeof(sizeStr) );
  gtk_label_set_text( GTK_LABEL(a->ul_lb), sizeStr );

  tr_strlsize( sizeStr, stat->corruptEver, sizeof( sizeStr ) );
  gtk_label_set_text( GTK_LABEL( a->failed_lb ), sizeStr );

  tr_strlratio( buf, stat->ratio, sizeof( buf ) );
  gtk_label_set_text( GTK_LABEL( a->ratio_lb ), buf );

  tr_strlspeed( buf, stat->swarmspeed, sizeof(buf) );
  gtk_label_set_text (GTK_LABEL(a->swarm_lb), buf );

  gtk_label_set_text (GTK_LABEL(a->err_lb),
                      *stat->errorString ? stat->errorString : _("None"));

  pch = stat->startDate ? rfc822date (stat->startDate)
                        : g_strdup_printf ("?");
  gtk_label_set_text (GTK_LABEL(a->date_added_lb), pch);
  g_free (pch);

  pch = stat->activityDate ? rfc822date (stat->activityDate)
                           : g_strdup_printf ("?");
  gtk_label_set_text (GTK_LABEL(a->last_activity_lb), pch);
  g_free (pch);

  if (GDK_IS_DRAWABLE (a->availability_da->window))
    refresh_pieces (a->availability_da, NULL, a->gtor);
}
  

static GtkWidget*
activity_page_new (TrTorrent * gtor)
{
  Activity * a = g_new (Activity, 1);
  int row = 0;
  GtkWidget *t = hig_workarea_create ();
  GtkWidget *l, *w;

  a->gtor = gtor;

  hig_workarea_add_section_title (t, &row, _("Transfer"));
  hig_workarea_add_section_spacer (t, row, 8);

    l = a->state_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("State:"), l, NULL);

    l = a->progress_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Progress:"), l, NULL);

    l = a->have_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Have:"), l, NULL);

    l = a->dl_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Downloaded:"), l, NULL);

    l = a->ul_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Uploaded:"), l, NULL);

    l = a->failed_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Failed DL:"), l, NULL);

    l = a->ratio_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Ratio:"), l, NULL);

    l = a->swarm_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Swarm Rate:"), l, NULL);

    l = a->err_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Error:"), l, NULL);

    w = a->availability_da = gtk_drawing_area_new ();
    gtk_widget_set_size_request (w, 0u, 100u);
    g_object_set_data (G_OBJECT(w), "draw-mode", GINT_TO_POINTER(DRAW_PROG));
    g_signal_connect (w, "expose-event", G_CALLBACK(refresh_pieces), gtor);
    hig_workarea_add_row (t, &row, _("Completeness:"), w, NULL);

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Dates"));
  hig_workarea_add_section_spacer (t, row, 3);

    l = a->date_added_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Added:"), l, NULL);

    l = a->last_activity_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Last Activity"), l, NULL);

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_finish (t, &row);
  g_object_set_data_full (G_OBJECT(t), "activity-data", a, g_free);
  return t;
}

/****
*****  FILES TAB
****/

#define STRIPROOT( path )                                                     \
    ( g_path_is_absolute( (path) ) ? g_path_skip_root( (path) ) : (path) )

enum
{
  FC_STOCK,
  FC_LABEL,
  FC_PROG,
  FC_KEY,
  FC_INDEX,
  FC_SIZE,
  FC_PRIORITY,
  FC_ENABLED,
  N_FILE_COLS
};

typedef struct
{
  TrTorrent * gtor;
  GtkTreeModel * model; /* same object as store, but recast */
  GtkTreeStore * store; /* same object as model, but recast */
}
FileData;

static const char*
priorityToString( const int priority )
{
    switch( priority ) {
        case TR_PRI_HIGH:   return _("High");
        case TR_PRI_NORMAL: return _("Normal");
        case TR_PRI_LOW:    return _("Low");
        default:            return "BUG!";
    }
}

static tr_priority_t
stringToPriority( const char* str )
{
    if( !strcmp( str, _( "High" ) ) ) return TR_PRI_HIGH;
    if( !strcmp( str, _( "Low" ) ) ) return TR_PRI_LOW;
    return TR_PRI_NORMAL;
}

static void
parsepath( const tr_torrent  * tor,
           GtkTreeStore      * store,
           GtkTreeIter       * ret,
           const char        * path,
           int                 index,
           uint64_t            size )
{
    GtkTreeModel * model;
    GtkTreeIter  * parent, start, iter;
    char         * file, * lower, * mykey, *escaped=0;
    const char   * stock;
    int            priority = 0;
    gboolean       enabled = TRUE;

    model  = GTK_TREE_MODEL( store );
    parent = NULL;
    file   = g_path_get_basename( path );
    if( 0 != strcmp( file, path ) )
    {
        char * dir = g_path_get_dirname( path );
        parsepath( tor, store, &start, dir, index, size );
        parent = &start;
        g_free( dir );
    }

    lower = g_utf8_casefold( file, -1 );
    mykey = g_utf8_collate_key( lower, -1 );
    if( gtk_tree_model_iter_children( model, &iter, parent ) ) do
    {
        gboolean stop;
        char * modelkey;
        gtk_tree_model_get( model, &iter, FC_KEY, &modelkey, -1 );
        stop = (modelkey!=NULL) && !strcmp(mykey,modelkey);
        g_free (modelkey);
        if (stop) goto done;
    }
    while( gtk_tree_model_iter_next( model, &iter ) );

    gtk_tree_store_append( store, &iter, parent );
    if( NULL == ret )
    {
        stock = GTK_STOCK_FILE;
    }
    else
    {
        stock = GTK_STOCK_DIRECTORY;
        size  = 0;
        index = -1;
    }

    if (index != -1) {
        priority = tr_torrentGetFilePriority( tor, index );
        enabled  = tr_torrentGetFileDL( tor, index );
    }

    escaped = g_markup_escape_text (file, -1); 
    gtk_tree_store_set( store, &iter, FC_INDEX, index,
                                      FC_LABEL, escaped,
                                      FC_KEY, mykey,
                                      FC_STOCK, stock,
                                      FC_PRIORITY, priorityToString(priority),
                                      FC_ENABLED, enabled,
                                      FC_SIZE, size, -1 );
  done:
    g_free( escaped );
    g_free( mykey );
    g_free( lower );
    g_free( file );
    if( NULL != ret )
      *ret = iter;
}

static uint64_t
getdirtotals( GtkTreeStore * store, GtkTreeIter * parent )
{
    GtkTreeModel * model;
    GtkTreeIter    iter;
    uint64_t       mysize, subsize;
    char         * name, * label;

    model  = GTK_TREE_MODEL( store );
    mysize = 0;
    if( gtk_tree_model_iter_children( model, &iter, parent ) ) do
    {
         char sizeStr[64];
        if( gtk_tree_model_iter_has_child( model, &iter ) )
        {
            subsize = getdirtotals( store, &iter );
            gtk_tree_store_set( store, &iter, FC_SIZE, subsize, -1 );
        }
        else
        {
            gtk_tree_model_get( model, &iter, FC_SIZE, &subsize, -1 );
        }
        gtk_tree_model_get( model, &iter, FC_LABEL, &name, -1 );
        tr_strlsize( sizeStr, subsize, sizeof( sizeStr ) );
        label = g_markup_printf_escaped( "<small>%s (%s)</small>",
                                          name, sizeStr );
        g_free( name );
        gtk_tree_store_set( store, &iter, FC_LABEL, label, -1 );
        g_free( label );
        mysize += subsize;
    }
    while( gtk_tree_model_iter_next( model, &iter ) );

    return mysize;
}

static void
updateprogress( GtkTreeModel   * model,
                GtkTreeStore   * store,
                GtkTreeIter    * parent,
                tr_file_stat   * fileStats,
                guint64        * setmeGotSize,
                guint64        * setmeTotalSize)
{
    GtkTreeIter iter;
    guint64 gotSize=0, totalSize=0;

    if( gtk_tree_model_iter_children( model, &iter, parent ) ) do
    {
        int oldProg, newProg;
        guint64 subGot, subTotal;

        if (gtk_tree_model_iter_has_child( model, &iter ) )
        {
            updateprogress( model, store, &iter, fileStats, &subGot, &subTotal);
        }
        else
        {
            int index, percent;
            gtk_tree_model_get( model, &iter, FC_SIZE, &subTotal,
                                              FC_INDEX, &index,
                                              -1 );
            g_assert( 0 <= index );
            percent = (int)(fileStats[index].progress * 100.0); /* [0...100] */
            subGot = (guint64)(subTotal * percent/100.0);
        }

        if (!subTotal) subTotal = 1; /* avoid div by zero */
        g_assert (subGot <= subTotal);

        /* why not just set it every time?
           because that causes the "priorities" combobox to pop down */
        gtk_tree_model_get (model, &iter, FC_PROG, &oldProg, -1);
        newProg = (int)(100.0*subGot/subTotal);
        if (oldProg != newProg)
          gtk_tree_store_set (store, &iter,
                              FC_PROG, (int)(100.0*subGot/subTotal), -1);

        gotSize += subGot;
        totalSize += subTotal;
    }
    while( gtk_tree_model_iter_next( model, &iter ) );

    *setmeGotSize = gotSize;
    *setmeTotalSize = totalSize;
}

static GtkTreeModel*
priority_model_new (void)
{
  GtkTreeIter iter;
  GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("High"), 1, TR_PRI_HIGH, -1);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("Normal"), 1, TR_PRI_NORMAL, -1);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("Low"), 1, TR_PRI_LOW, -1);
  return GTK_TREE_MODEL (store);
}

static void
subtree_walk_dnd( GtkTreeStore   * store,
                  GtkTreeIter    * iter,
                  tr_torrent     * tor,
                  gboolean         enabled,
                  GArray         * indices )
{
    int index;
    GtkTreeIter child;
 
    /* update this node */ 
    gtk_tree_model_get( GTK_TREE_MODEL(store), iter, FC_INDEX, &index, -1  );
    if (index >= 0)
      g_array_append_val( indices, index );
    gtk_tree_store_set( store, iter, FC_ENABLED, enabled, -1 );

    /* visit the children */
    if( gtk_tree_model_iter_children( GTK_TREE_MODEL(store), &child, iter ) ) do
      subtree_walk_dnd( store, &child, tor, enabled, indices );
    while( gtk_tree_model_iter_next( GTK_TREE_MODEL(store), &child ) );
}

static void
set_subtree_dnd( GtkTreeStore   * store,
                 GtkTreeIter    * iter,
                 tr_torrent     * tor,
                 gboolean         enabled )
{
    GArray * indices = g_array_new( FALSE, FALSE, sizeof(int) );
    subtree_walk_dnd( store, iter, tor, enabled, indices );
    tr_torrentSetFileDLs( tor, (int*)indices->data, (int)indices->len, enabled );
    g_array_free( indices, TRUE );
}

static void
subtree_walk_priority( GtkTreeStore   * store,
                       GtkTreeIter    * iter,
                       tr_torrent     * tor,
                       int              priority,
                       GArray         * indices )
{
    int index;
    GtkTreeIter child;

    /* update this node */ 
    gtk_tree_model_get( GTK_TREE_MODEL(store), iter, FC_INDEX, &index, -1  );
    if( index >= 0 )
        g_array_append_val( indices, index );
    gtk_tree_store_set( store, iter, FC_PRIORITY, priorityToString(priority), -1 );

    /* visit the children */
    if( gtk_tree_model_iter_children( GTK_TREE_MODEL(store), &child, iter ) ) do
        subtree_walk_priority( store, &child, tor, priority, indices );
    while( gtk_tree_model_iter_next( GTK_TREE_MODEL(store), &child ) );

}

static void
set_subtree_priority( GtkTreeStore * store,
                      GtkTreeIter * iter,
                      tr_torrent * tor,
                      int priority )
{
    GArray * indices = g_array_new( FALSE, FALSE, sizeof(int) );
    subtree_walk_priority( store, iter, tor, priority, indices );
    tr_torrentSetFilePriorities( tor, (int*)indices->data, (int)indices->len, priority );
    g_array_free( indices, TRUE );
}

static void
priority_changed_cb (GtkCellRendererText * cell UNUSED,
                     const gchar         * path,
		     const gchar         * value,
		     void                * file_data)
{
    GtkTreeIter iter;
    FileData * d = file_data;
    if (gtk_tree_model_get_iter_from_string (d->model, &iter, path))
    {
        tr_torrent  * tor = tr_torrent_handle( d->gtor );
        const tr_priority_t priority = stringToPriority( value );
        set_subtree_priority( d->store, &iter, tor, priority );
    }
}

static void
enabled_toggled (GtkCellRendererToggle  * cell UNUSED,
	         const gchar            * path_str,
	         gpointer                 data_gpointer)
{
  FileData * data = data_gpointer;
  GtkTreePath * path = gtk_tree_path_new_from_string( path_str );
  GtkTreeModel * model = data->model;
  GtkTreeIter iter;
  gboolean enabled;

  gtk_tree_model_get_iter( model, &iter, path );
  gtk_tree_model_get( model, &iter, FC_ENABLED, &enabled, -1 );
  enabled = !enabled;
  set_subtree_dnd( GTK_TREE_STORE(model),
                   &iter,
                   tr_torrent_handle( data->gtor ),
                   enabled );

  gtk_tree_path_free( path );
}

static GtkWidget *
file_page_new ( TrTorrent * gtor )
{
    GtkWidget           * ret;
    FileData            * data;
    const tr_info       * inf;
    tr_torrent          * tor;
    GtkTreeStore        * store;
    int                   ii;
    GtkWidget           * view, * scroll;
    GtkCellRenderer     * rend;
    GtkCellRenderer     * priority_rend;
    GtkCellRenderer     * enabled_rend;
    GtkTreeViewColumn   * col;
    GtkTreeSelection    * sel;
    GtkTreeModel        * model;

    store = gtk_tree_store_new ( N_FILE_COLS,
                                 G_TYPE_STRING,    /* stock */
                                 G_TYPE_STRING,    /* label */
                                 G_TYPE_INT,       /* prog [0..100] */
                                 G_TYPE_STRING,    /* key */
                                 G_TYPE_INT,       /* index */
                                 G_TYPE_UINT64,    /* size */
                                 G_TYPE_STRING,    /* priority */
                                 G_TYPE_BOOLEAN ); /* dl enabled */

    /* set up the model */
    tor = tr_torrent_handle( gtor );
    inf = tr_torrent_info( gtor );
    for( ii = 0; ii < inf->fileCount; ii++ )
    {
        parsepath( tor, store, NULL, STRIPROOT( inf->files[ii].name ),
                   ii, inf->files[ii].length );
    }
    getdirtotals( store, NULL );

    /* create the view */
    view = gtk_tree_view_new_with_model( GTK_TREE_MODEL( store ) );
    g_object_set_data (G_OBJECT(view), "torrent-handle", tor );

    /* add file column */
    
    col = GTK_TREE_VIEW_COLUMN (g_object_new (GTK_TYPE_TREE_VIEW_COLUMN,
        "expand", TRUE,
        "title", _("File"),
        NULL));
    rend = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start( col, rend, FALSE );
    gtk_tree_view_column_add_attribute( col, rend, "stock-id", FC_STOCK );
    /* add text renderer */
    rend = gtk_cell_renderer_text_new();
    g_object_set( rend, "ellipsize", PANGO_ELLIPSIZE_END, NULL );
    gtk_tree_view_column_pack_start( col, rend, TRUE );
    gtk_tree_view_column_add_attribute( col, rend, "markup", FC_LABEL );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );
    /* add progress column */
    rend = gtk_cell_renderer_progress_new();
    col = gtk_tree_view_column_new_with_attributes (
      _("Progress"), rend, "value", FC_PROG, NULL);
    gtk_tree_view_column_set_sort_column_id( col, FC_PROG );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );
    /* set up view */
    sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );
    gtk_tree_view_expand_all( GTK_TREE_VIEW( view ) );
    gtk_tree_view_set_search_column( GTK_TREE_VIEW( view ), FC_LABEL );

    /* add "download" checkbox column */
    col = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_sort_column_id( col, FC_ENABLED );
    rend = enabled_rend = gtk_cell_renderer_toggle_new  ();
    col = gtk_tree_view_column_new_with_attributes (_("Download"),
                                                    rend,
                                                    "active", FC_ENABLED,
                                                    NULL);
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );

    /* add priority column */
    model = priority_model_new ();
    col = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_sort_column_id( col, FC_PRIORITY );
    gtk_tree_view_column_set_title (col, _("Priority"));
    rend = priority_rend = gtk_cell_renderer_combo_new ();
    gtk_tree_view_column_pack_start (col, rend, TRUE);
    g_object_set (G_OBJECT(rend), "model", model,
                                  "editable", TRUE,
                                  "has-entry", FALSE,
                                  "text-column", 0,
                                  NULL);
    g_object_unref (G_OBJECT(model));
    gtk_tree_view_column_add_attribute (col, rend, "text", FC_PRIORITY);
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );

    /* create the scrolled window and stick the view in it */
    scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scroll ),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scroll),
                                         GTK_SHADOW_IN);
    gtk_container_add( GTK_CONTAINER( scroll ), view );
    gtk_widget_set_size_request (scroll, 0u, 200u);
    gtk_container_set_border_width (GTK_CONTAINER(scroll), GUI_PAD);

    ret = scroll;
    data = g_new (FileData, 1);
    data->gtor = gtor;
    data->model = GTK_TREE_MODEL(store);
    data->store = store;
    g_object_set_data_full (G_OBJECT(ret), "file-data", data, g_free);
    g_signal_connect (G_OBJECT(priority_rend), "edited", G_CALLBACK(priority_changed_cb), data);
    g_signal_connect(enabled_rend, "toggled", G_CALLBACK(enabled_toggled), data );
    return ret;
}

static void
refresh_files (GtkWidget * top)
{
    guint64 foo, bar;
    int fileCount = 0;
    FileData * data = g_object_get_data (G_OBJECT(top), "file-data");
    tr_torrent * tor = tr_torrent_handle( data->gtor );
    tr_file_stat * fileStats = tr_torrentFiles( tor, &fileCount );
    updateprogress (data->model, data->store, NULL, fileStats, &foo, &bar);
    tr_torrentFilesFree( fileStats, fileCount );
}


/****
*****  OPTIONS
****/

static void
speed_toggled_cb( GtkToggleButton * tb, gpointer gtor, int up_or_down )
{
  tr_torrent * tor = tr_torrent_handle (gtor);
  gboolean b = gtk_toggle_button_get_active(tb);
  tr_torrentSetSpeedMode( tor, up_or_down, b ? TR_SPEEDLIMIT_SINGLE
                                             : TR_SPEEDLIMIT_GLOBAL );
}
static void
ul_speed_toggled_cb (GtkToggleButton *tb, gpointer gtor)
{
  speed_toggled_cb( tb, gtor, TR_UP );
}
static void
dl_speed_toggled_cb (GtkToggleButton *tb, gpointer gtor)
{
  speed_toggled_cb( tb, gtor, TR_DOWN );
}


#if 0
static void
seeding_cap_toggled_cb (GtkToggleButton *tb, gpointer gtor)
{
  tr_torrent_set_seeding_cap_enabled (TR_TORRENT(gtor), 
                                      gtk_toggle_button_get_active(tb));
}
#endif

static void
sensitize_from_check_cb (GtkToggleButton *toggle, gpointer w)
{
  gtk_widget_set_sensitive (GTK_WIDGET(w),
                            gtk_toggle_button_get_active(toggle));
}

static void
setSpeedLimit( GtkSpinButton* spin, gpointer gtor, int up_or_down )
{
  tr_torrent * tor = tr_torrent_handle (gtor);
  int KiB_sec = gtk_spin_button_get_value_as_int (spin);
  tr_torrentSetSpeedLimit( tor, up_or_down, KiB_sec );
}
static void
ul_speed_spun_cb (GtkSpinButton *spin, gpointer gtor)
{
  setSpeedLimit( spin, gtor, TR_UP );
}
static void
dl_speed_spun_cb (GtkSpinButton *spin, gpointer gtor)
{
  setSpeedLimit( spin, gtor, TR_DOWN );
}

#if 0
static void
seeding_ratio_spun_cb (GtkSpinButton *spin, gpointer gtor)
{
  tr_torrent_set_seeding_cap_ratio (TR_TORRENT(gtor),
                                    gtk_spin_button_get_value(spin));
}
#endif

static void
max_peers_spun_cb( GtkSpinButton * spin, gpointer gtor )
{
  const uint16_t n = gtk_spin_button_get_value( spin );
  tr_torrentSetMaxConnectedPeers( tr_torrent_handle( gtor ), n );
}

static GtkWidget*
options_page_new ( TrTorrent * gtor )
{
  uint16_t maxConnectedPeers;
  int i, row;
  gboolean b;
  GtkAdjustment *a;
  GtkWidget *t, *w, *tb;
  tr_torrent * tor = tr_torrent_handle (gtor);

  row = 0;
  t = hig_workarea_create ();
  hig_workarea_add_section_title (t, &row, _("Speed Limits") );
  hig_workarea_add_section_spacer (t, row, 2);

    tb = gtk_check_button_new_with_mnemonic (_("Limit _Download Speed (KiB/s):"));
    b = tr_torrentGetSpeedMode(tor,TR_DOWN) == TR_SPEEDLIMIT_SINGLE;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(tb), b );
    g_signal_connect (tb, "toggled", G_CALLBACK(dl_speed_toggled_cb), gtor);

    i = tr_torrentGetSpeedLimit( tor, TR_DOWN );
    a = (GtkAdjustment*) gtk_adjustment_new (i, 0.0, G_MAXDOUBLE, 1, 1, 1);
    w = gtk_spin_button_new (a, 1, 0);
    g_signal_connect (w, "value-changed", G_CALLBACK(dl_speed_spun_cb), gtor);
    g_signal_connect (tb, "toggled", G_CALLBACK(sensitize_from_check_cb), w);
    sensitize_from_check_cb (GTK_TOGGLE_BUTTON(tb), w);
    hig_workarea_add_row_w (t, &row, tb, w, NULL);

    tb = gtk_check_button_new_with_mnemonic (_("Limit _Upload Speed (KiB/s):"));
    b = tr_torrentGetSpeedMode(tor,TR_UP) == TR_SPEEDLIMIT_SINGLE;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(tb), b );
    g_signal_connect (tb, "toggled", G_CALLBACK(ul_speed_toggled_cb), gtor);

    i = tr_torrentGetSpeedLimit( tor, TR_UP );
    a = (GtkAdjustment*) gtk_adjustment_new (i, 0.0, G_MAXDOUBLE, 1, 1, 1);
    w = gtk_spin_button_new (a, 1, 0);
    g_signal_connect (w, "value-changed", G_CALLBACK(ul_speed_spun_cb), gtor);
    g_signal_connect (tb, "toggled", G_CALLBACK(sensitize_from_check_cb), w);
    sensitize_from_check_cb (GTK_TOGGLE_BUTTON(tb), w);
    hig_workarea_add_row_w (t, &row, tb, w, NULL);

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Peer Connections"));
  hig_workarea_add_section_spacer (t, row, 1);

    maxConnectedPeers = tr_torrentGetMaxConnectedPeers( tor );
    w = gtk_spin_button_new_with_range( 1, 3000, 5 );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON( w ), maxConnectedPeers );
    hig_workarea_add_row( t, &row, _( "Maximum connected peers:" ), w, NULL );
    g_signal_connect( w, "value-changed", G_CALLBACK( max_peers_spun_cb ), gtor );

#if 0
    tb = gtk_check_button_new_with_mnemonic (_("_Stop Seeding at Ratio:"));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(tb), gtor->seeding_cap_enabled);
    g_signal_connect (tb, "toggled", G_CALLBACK(seeding_cap_toggled_cb), gtor);
    a = (GtkAdjustment*) gtk_adjustment_new (gtor->seeding_cap, 0.0, G_MAXDOUBLE, 1, 1, 1);
    w = gtk_spin_button_new (a, 1, 1);
    g_signal_connect (w, "value-changed", G_CALLBACK(seeding_ratio_spun_cb), gtor);
    g_signal_connect (tb, "toggled", G_CALLBACK(sensitize_from_check_cb), w);
    sensitize_from_check_cb (GTK_TOGGLE_BUTTON(tb), w);
    hig_workarea_add_row_w (t, &row, tb, w, NULL);
#endif

  hig_workarea_finish (t, &row);
  return t;
}

static void
refresh_options (GtkWidget * top UNUSED)
{
}

/****
*****  DIALOG
****/

static void
torrent_destroyed (gpointer dialog, GObject * dead_torrent UNUSED)
{
  gtk_widget_destroy (GTK_WIDGET(dialog));
}

static void
remove_tag (gpointer tag)
{
  g_source_remove (GPOINTER_TO_UINT(tag)); /* stop the periodic refresh */
}

static void
response_cb (GtkDialog *dialog, int response UNUSED, gpointer gtor)
{
  g_object_weak_unref (G_OBJECT(gtor), torrent_destroyed, dialog);
  gtk_widget_destroy (GTK_WIDGET(dialog));
}

static gboolean
periodic_refresh (gpointer data)
{
  refresh_peers    (g_object_get_data (G_OBJECT(data), "peers-top"));
  refresh_activity (g_object_get_data (G_OBJECT(data), "activity-top"));
  refresh_files    (g_object_get_data (G_OBJECT(data), "files-top"));
  refresh_options  (g_object_get_data (G_OBJECT(data), "options-top"));

  return TRUE;
}

GtkWidget*
torrent_inspector_new ( GtkWindow * parent, TrTorrent * gtor )
{
  guint tag;
  GtkWidget *d, *n, *w;
  tr_torrent * tor = tr_torrent_handle (gtor);
  char sizeStr[64];
  char title[512];
  const tr_info * info = tr_torrent_info (gtor);

  /* create the dialog */
  tr_strlsize( sizeStr, info->totalSize, sizeof(sizeStr) );
  g_snprintf( title, sizeof(title), _( "Details for %s (%s)" ), info->name, sizeStr );
  d = gtk_dialog_new_with_buttons (title, parent, 0,
                                   GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                   NULL);
  gtk_window_set_role (GTK_WINDOW(d), "tr-info" );
  g_signal_connect (d, "response", G_CALLBACK (response_cb), gtor);
  gtk_dialog_set_has_separator( GTK_DIALOG( d ), FALSE );
  g_object_weak_ref (G_OBJECT(gtor), torrent_destroyed, d);

  /* add the notebook */
  n = gtk_notebook_new ();

  w = activity_page_new (gtor);
  g_object_set_data (G_OBJECT(d), "activity-top", w);
  gtk_notebook_append_page (GTK_NOTEBOOK(n), w, 
                            gtk_label_new_with_mnemonic (_("_Activity")));

  w = peer_page_new (gtor);
  g_object_set_data (G_OBJECT(d), "peers-top", w);
  gtk_notebook_append_page (GTK_NOTEBOOK(n),  w,
                            gtk_label_new_with_mnemonic (_("_Peers")));

  gtk_notebook_append_page (GTK_NOTEBOOK(n),
                            info_page_new (tor),
                            gtk_label_new_with_mnemonic (_("_Info")));

  w = file_page_new (gtor);
  g_object_set_data (G_OBJECT(d), "files-top", w);
  gtk_notebook_append_page (GTK_NOTEBOOK(n), w,
                            gtk_label_new_with_mnemonic (_("_Files")));

  w = options_page_new (gtor);
  g_object_set_data (G_OBJECT(d), "options-top", w);
  gtk_notebook_append_page (GTK_NOTEBOOK(n), w,
                            gtk_label_new_with_mnemonic (_("_Options")));

  gtk_box_pack_start_defaults (GTK_BOX(GTK_DIALOG(d)->vbox), n);

  tag = g_timeout_add (UPDATE_INTERVAL_MSEC, periodic_refresh, d);
  g_object_set_data_full (G_OBJECT(d), "tag",
                          GUINT_TO_POINTER(tag), remove_tag);

  /* return the results */
  periodic_refresh (d);
  gtk_widget_show_all (GTK_DIALOG(d)->vbox);
  return d;
}
