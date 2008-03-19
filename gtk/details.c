/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>

#include "actions.h"
#include "details.h"
#include "file-list.h"
#include "tr-torrent.h"
#include "hig.h"
#include "util.h"

#define UPDATE_INTERVAL_MSEC 2000

/****
*****  PIECES VIEW
****/

/* define SHOW_PIECES */

#ifdef SHOW_PIECES
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
refresh_pieces( GtkWidget * da, GdkEventExpose * event UNUSED, gpointer gtor )
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
  const gboolean rtl = gtk_widget_get_direction( da ) == GTK_TEXT_DIR_RTL;

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
      int draw_x = grid_bounds.x + (int)(x * piece_w);
      int draw_y = grid_bounds.y + (int)(y * piece_h);
      int color = BLACK;
      int border = BLACK;

      if( rtl )
          draw_x = grid_bounds.x + grid_bounds.width - (int)((x+1) * piece_w);
      else
          draw_x = grid_bounds.x + (int)(x * piece_w);
      draw_y = grid_bounds.y + (int)(y * piece_h);

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
#endif

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
  /* 'percent done' column header. terse to keep the column narrow. */
  N_("%"),
  " ",
  /* 'download speed' column header. terse to keep the column narrow. */
  N_("Down"),
  /* 'upload speed' column header.  terse to keep the column narrow. */
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

#ifdef SHOW_PIECES
  if (GDK_IS_DRAWABLE (p->completeness->window))
    refresh_pieces (p->completeness, NULL, p->gtor);
#endif

  fmtpeercount (p->seeders_lb, stat->seeders);
  fmtpeercount (p->leechers_lb, stat->leechers);
  fmtpeercount (p->completed_lb, stat->completedFromTracker );

  free( peers );
}

static GtkWidget* peer_page_new ( TrTorrent * gtor )
{
  guint i;
  GtkTreeModel *m;
  GtkWidget *h, *v, *w, *ret, *sw, *l, *vbox, *hbox;
  tr_torrent * tor = tr_torrent_handle (gtor);
  PeerData * p = g_new (PeerData, 1);

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
  gtk_widget_set_size_request( v, 550, 0 );
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
        c = gtk_tree_view_column_new_with_attributes (t, r, "text", col, NULL);
        break;

      case PEER_COL_CLIENT:
        r = gtk_cell_renderer_text_new ();
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

#ifdef SHOW_PIECES
    l = gtk_label_new (NULL);
    gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
    gtk_label_set_markup (GTK_LABEL(l), _( "<b>Availability</b>" ) );
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

    l = gtk_label_new (NULL);
    gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
    gtk_label_set_markup (GTK_LABEL(l), _( "<b>Connected Peers</b>" ) );
    gtk_box_pack_start (GTK_BOX(vbox), l, FALSE, FALSE, 0);
#endif

    h = gtk_hbox_new (FALSE, GUI_PAD);
    gtk_box_pack_start_defaults (GTK_BOX(h), sw);
    gtk_box_pack_start_defaults (GTK_BOX(vbox), h);

    hbox = gtk_hbox_new (FALSE, GUI_PAD);
        l = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL(l), _( "<b>Seeders:</b>" ) );
        gtk_box_pack_start (GTK_BOX(hbox), l, FALSE, FALSE, 0);
        l = p->seeders_lb = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX(hbox), l, FALSE, FALSE, 0);
    gtk_box_pack_start_defaults (GTK_BOX(hbox),
                                 gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f));
        l = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL(l), _( "<b>Leechers:</b>" ) );
        gtk_box_pack_start (GTK_BOX(hbox), l, FALSE, FALSE, 0);
        l = p->leechers_lb = gtk_label_new (NULL);
        gtk_box_pack_start (GTK_BOX(hbox), l, FALSE, FALSE, 0);
    gtk_box_pack_start_defaults (GTK_BOX(hbox),
                                 gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f));
        l = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL(l), _( "<b>Completed:</b>" ) );
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

static GtkWidget*
info_page_new (tr_torrent * tor)
{
  int row = 0;
  GtkWidget *t = hig_workarea_create ();
  GtkWidget *l, *w, *fr;
  char *pch;
  char sizeStr[128];
  char buf[256];
  GtkTextBuffer * b;
  const tr_info * info = tr_torrentInfo(tor);

  hig_workarea_add_section_title (t, &row, _("Torrent Information"));

    tr_strlsize( sizeStr, info->pieceSize, sizeof(sizeStr) );
    g_snprintf( buf, sizeof( buf ),
                /* %1$s is number of pieces; %2$s is how big each piece is */
                ngettext( "%1$d Piece @ %2$s",
                          "%1$d Pieces @ %2$s",
                          info->pieceCount ),
                info->pieceCount, sizeStr );
    l = gtk_label_new (buf);
    hig_workarea_add_row (t, &row, _("Pieces:"), l, NULL);

    l = gtk_label_new (info->hashString);
    gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
    hig_workarea_add_row (t, &row, _("Hash:"), l, NULL);

    pch = (info->isPrivate )
      ? _("Private Torrent: PEX disabled")
      : _("Public Torrent");
    l = gtk_label_new (pch);
    hig_workarea_add_row (t, &row, _("Privacy:"), l, NULL);

    b = gtk_text_buffer_new (NULL);
    if( info->comment )
        gtk_text_buffer_set_text (b, info->comment, -1);
    w = gtk_text_view_new_with_buffer (b);
    gtk_widget_set_size_request (w, 0u, 100u);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(w), GTK_WRAP_WORD); 
    gtk_text_view_set_editable (GTK_TEXT_VIEW(w), FALSE);
    fr = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME(fr), GTK_SHADOW_IN);
    gtk_container_add (GTK_CONTAINER(fr), w);
    w = hig_workarea_add_row (t, &row, _("Comment:"), fr, NULL);
    gtk_misc_set_alignment (GTK_MISC(w), 0.0f, 0.0f);

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Origins"));
  
    l = gtk_label_new (*info->creator ? info->creator : _("Unknown"));
    hig_workarea_add_row (t, &row, _("Creator:"), l, NULL);

    pch = rfc822date ((guint64)info->dateCreated * 1000u);
    l = gtk_label_new (pch);
    hig_workarea_add_row (t, &row, _("Date:"), l, NULL); 
    g_free (pch);

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Location"));

    l = gtk_label_new (tr_torrentGetFolder (tor));
    gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
    hig_workarea_add_row (t, &row, _( "Destination folder:" ), l, NULL); 

    l = gtk_label_new ( info->torrent );
    gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
    hig_workarea_add_row (t, &row, _( "Torrent file:" ), l, NULL); 

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

  /* %1$.1f is percent of how much of what we want's been downloaded,
     %2$.1f is percent of how much of the whole torrent we've downloaded */
  pch = g_strdup_printf( _( "%1$.1f%% (%2$.1f%% selected)" ), stat->percentComplete*100.0, stat->percentDone*100.0 );
  gtk_label_set_text (GTK_LABEL(a->progress_lb), pch);
  g_free (pch);

  tr_strlsize( sizeStr,  stat->haveValid + stat->haveUnchecked, sizeof(sizeStr) );
  tr_strlsize( sizeStr2, stat->haveValid,                       sizeof(sizeStr2) );
  /* %1$s is total size of what we've saved to disk
     %2$s is how much of it's passed the checksum test */
  g_snprintf( buf, sizeof(buf), _("%1$s (%2$s verified)"), sizeStr, sizeStr2 );
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

  pch = stat->startDate ? rfc822date( stat->startDate )
                        : g_strdup_printf( _( "Unknown" ) );
  gtk_label_set_text (GTK_LABEL(a->date_added_lb), pch);
  g_free (pch);

  pch = stat->activityDate ? rfc822date( stat->activityDate )
                           : g_strdup_printf( _( "Unknown" ) );
  gtk_label_set_text (GTK_LABEL(a->last_activity_lb), pch);
  g_free (pch);

#ifdef SHOW_PIECES
  if (GDK_IS_DRAWABLE (a->availability_da->window))
    refresh_pieces (a->availability_da, NULL, a->gtor);
#endif
}
  

static GtkWidget*
activity_page_new (TrTorrent * gtor)
{
  Activity * a = g_new (Activity, 1);
  int row = 0;
  GtkWidget *t = hig_workarea_create ();
  GtkWidget *l;

  a->gtor = gtor;

  hig_workarea_add_section_title (t, &row, _("Transfer"));

    l = a->state_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("State:"), l, NULL);

    l = a->progress_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Progress:"), l, NULL);

    l = a->have_lb = gtk_label_new (NULL);
    /* "Have" refers to how much of the torrent we have */
    hig_workarea_add_row (t, &row, _("Have:"), l, NULL);

    l = a->dl_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Downloaded:"), l, NULL);

    l = a->ul_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Uploaded:"), l, NULL);

    /* how much downloaded data was corrupt */
    l = a->failed_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Failed DL:"), l, NULL);

    l = a->ratio_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Ratio:"), l, NULL);

    l = a->swarm_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Swarm rate:"), l, NULL);

    l = a->err_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Error:"), l, NULL);

#ifdef SHOW_PIECES
  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Completion"));

    w = a->availability_da = gtk_drawing_area_new ();
    gtk_widget_set_size_request (w, 0u, 100u);
    g_object_set_data (G_OBJECT(w), "draw-mode", GINT_TO_POINTER(DRAW_PROG));
    g_signal_connect (w, "expose-event", G_CALLBACK(refresh_pieces), gtor);
    hig_workarea_add_wide_control( t, &row, w );
#endif

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_add_section_title (t, &row, _("Dates"));

    l = a->date_added_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Started at:"), l, NULL);

    l = a->last_activity_lb = gtk_label_new (NULL);
    hig_workarea_add_row (t, &row, _("Last activity at:"), l, NULL);

  hig_workarea_add_section_divider (t, &row);
  hig_workarea_finish (t, &row);
  g_object_set_data_full (G_OBJECT(t), "activity-data", a, g_free);
  return t;
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
  int kb_sec = gtk_spin_button_get_value_as_int (spin);
  tr_torrentSetSpeedLimit( tor, up_or_down, kb_sec );
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
  hig_workarea_add_section_title (t, &row, _("Bandwidth") );

    tb = gtk_check_button_new_with_mnemonic (_("Limit _download speed (KB/s):"));
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

    tb = gtk_check_button_new_with_mnemonic (_("Limit _upload speed (KB/s):"));
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

    maxConnectedPeers = tr_torrentGetMaxConnectedPeers( tor );
    w = gtk_spin_button_new_with_range( 1, 3000, 5 );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON( w ), maxConnectedPeers );
    hig_workarea_add_row( t, &row, _( "_Maximum peers:" ), w, w );
    g_signal_connect( w, "value-changed", G_CALLBACK( max_peers_spun_cb ), gtor );

#if 0
    tb = gtk_check_button_new_with_mnemonic (_("_Stop seeding at ratio:"));
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
*****  TRACKER
****/

#define TRACKER_PAGE "tracker-page"

struct tracker_page
{
    TrTorrent * gtor;

    GtkWidget * last_scrape_time_lb;
    GtkWidget * last_scrape_response_lb;
    GtkWidget * next_scrape_countdown_lb;

    GtkWidget * last_announce_time_lb;
    GtkWidget * last_announce_response_lb;
    GtkWidget * next_announce_countdown_lb;
    GtkWidget * manual_announce_countdown_lb;
};

GtkWidget*
tracker_page_new( TrTorrent * gtor )
{
    GtkWidget * t;
    GtkWidget * l;
    int row = 0;
    const char * s;
    char * tmp;
    struct tracker_page * page = g_new0( struct tracker_page, 1 );
    const tr_tracker_info * track;
    const tr_info * info = tr_torrent_info (gtor);

    page->gtor = gtor;

    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Scrape" ) );

        s = _( "Last scrape at:" );
        l = gtk_label_new( NULL );
        page->last_scrape_time_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

        s = _( "Tracker responded:");
        l = gtk_label_new( NULL );
        page->last_scrape_response_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

        s = _( "Next scrape in:" );
        l = gtk_label_new( NULL );
        page->next_scrape_countdown_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Announce" ) );

        track = info->trackerList->list;
        tmp = track->port==80
          ? g_strdup_printf( "http://%s%s", track->address, track->announce )
          : g_strdup_printf( "http://%s:%d%s", track->address, track->port, track->announce );
        l = gtk_label_new( tmp );
        gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
        hig_workarea_add_row (t, &row, _( "Tracker:" ), l, NULL);
        g_free( tmp );

        s = _( "Last announce at:" );
        l = gtk_label_new( NULL );
        page->last_announce_time_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

        s = _( "Tracker responded:");
        l = gtk_label_new( NULL );
        page->last_announce_response_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

        s = _( "Next announce in:" );
        l = gtk_label_new( NULL );
        page->next_announce_countdown_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

        /* how long until the tracker will honor user
         * pressing the "ask for more peers" button */
        s = _( "Manual announce allowed in:" );
        l = gtk_label_new( NULL );
        page->manual_announce_countdown_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

    g_object_set_data_full( G_OBJECT( t ), TRACKER_PAGE, page, g_free );
    hig_workarea_finish( t, &row );
    return t;
}

static void
refresh_countdown_lb( GtkWidget * l, time_t t )
{
    const time_t now = time( NULL );

    if( !t || ( t < now ) )
        gtk_label_set_text( GTK_LABEL( l ), _( "Never" ) );
    else {
        char buf[1024];
        const int seconds = t - now;
        tr_strltime( buf, seconds, sizeof( buf ) );
        gtk_label_set_text( GTK_LABEL( l ), buf );
    }
}

static void
refresh_time_lb( GtkWidget * l, time_t t )
{
    const char * never = _( "Never" );
    if( !t )
        gtk_label_set_text( GTK_LABEL( l ), never );
    else {
        char * str = rfc822date( t * 1000 );
        gtk_label_set_text( GTK_LABEL( l ), str );
        g_free( str );
    }
}

static void
refresh_tracker( GtkWidget * w )
{
    GtkWidget * l;
    time_t t;
    struct tracker_page * page = g_object_get_data( G_OBJECT( w ), TRACKER_PAGE );
    const tr_stat * torStat = tr_torrent_stat( page->gtor );

    l = page->last_scrape_time_lb;
    t = torStat->tracker_stat.lastScrapeTime;
    refresh_time_lb( l, t );

    l = page->last_scrape_response_lb;
    gtk_label_set_text( GTK_LABEL( l ), torStat->tracker_stat.scrapeResponse );

    l = page->next_scrape_countdown_lb;
    t = torStat->tracker_stat.nextScrapeTime;
    refresh_countdown_lb( l, t );

    l = page->last_announce_time_lb;
    t = torStat->tracker_stat.lastAnnounceTime;
    refresh_time_lb( l, t );

    l = page->last_announce_response_lb;
    gtk_label_set_text( GTK_LABEL( l ), torStat->tracker_stat.announceResponse );

    l = page->next_announce_countdown_lb;
    t = torStat->tracker_stat.nextAnnounceTime;
    refresh_countdown_lb( l, t );

    l = page->manual_announce_countdown_lb;
    t = torStat->tracker_stat.nextManualAnnounceTime;
    refresh_countdown_lb( l, t );
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
  refresh_tracker   (g_object_get_data (G_OBJECT(data), "tracker-top"));
  refresh_peers     (g_object_get_data (G_OBJECT(data), "peers-top"));
  refresh_activity  (g_object_get_data (G_OBJECT(data), "activity-top"));
  refresh_options   (g_object_get_data (G_OBJECT(data), "options-top"));
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
  /* %1$s is torrent name; %2$s its file size */
  g_snprintf( title, sizeof(title), _( "Details for %1$s (%2$s)" ), info->name, sizeStr );
  d = gtk_dialog_new_with_buttons (title, parent, 0,
                                   GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                   NULL);
  gtk_window_set_role (GTK_WINDOW(d), "tr-info" );
  g_signal_connect (d, "response", G_CALLBACK (response_cb), gtor);
  gtk_dialog_set_has_separator( GTK_DIALOG( d ), FALSE );
  gtk_container_set_border_width (GTK_CONTAINER (d), 5);
  gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (d)->vbox), 2);
  g_object_weak_ref (G_OBJECT(gtor), torrent_destroyed, d);
  

  /* add the notebook */
  n = gtk_notebook_new ();
  gtk_container_set_border_width ( GTK_CONTAINER ( n ), 5 );

  w = activity_page_new (gtor);
  g_object_set_data (G_OBJECT(d), "activity-top", w);
  gtk_notebook_append_page (GTK_NOTEBOOK(n), w, 
                            gtk_label_new (_("Activity")));

  w = peer_page_new (gtor);
  g_object_set_data (G_OBJECT(d), "peers-top", w);
  gtk_notebook_append_page (GTK_NOTEBOOK(n),  w,
                            gtk_label_new (_("Peers")));

  gtk_notebook_append_page (GTK_NOTEBOOK(n),
                            info_page_new (tor),
                            gtk_label_new (_("Information")));

  w = file_list_new( gtor );
  gtk_container_set_border_width( GTK_CONTAINER( w ), GUI_PAD_BIG );
  g_object_set_data (G_OBJECT(d), "files-top", w);
  gtk_notebook_append_page (GTK_NOTEBOOK(n), w,
                            gtk_label_new (_("Files")));

  w = tracker_page_new( gtor );
  g_object_set_data( G_OBJECT( d ), "tracker-top", w );
  gtk_notebook_append_page( GTK_NOTEBOOK( n ), w,
                            gtk_label_new( _( "Tracker" ) ) );

  w = options_page_new (gtor);
  g_object_set_data (G_OBJECT(d), "options-top", w);
  gtk_notebook_append_page (GTK_NOTEBOOK(n), w,
                            gtk_label_new (_("Options")));

  gtk_box_pack_start_defaults (GTK_BOX(GTK_DIALOG(d)->vbox), n);

  tag = g_timeout_add (UPDATE_INTERVAL_MSEC, periodic_refresh, d);
  g_object_set_data_full (G_OBJECT(d), "tag",
                          GUINT_TO_POINTER(tag), remove_tag);

  /* return the results */
  periodic_refresh (d);
  gtk_widget_show_all (GTK_DIALOG(d)->vbox);
  return d;
}
