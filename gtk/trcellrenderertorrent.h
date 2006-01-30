/* trcellrenderertorrent.h
 * Copyright (C) 2002 Naba Kumar <kh_naba@users.sourceforge.net>
 * modified by Jörgen Scheibengruber <mfcn@gmx.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/*
 * Modified by the GTK+ Team and others 1997-2004.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 */

#ifndef __TR_CELL_RENDERER_TORRENT_H__
#define __TR_CELL_RENDERER_TORRENT_H__

#include <gtk/gtkcellrenderer.h>

G_BEGIN_DECLS

#define GTK_TYPE_CELL_RENDERER_TORRENT (tr_cell_renderer_torrent_get_type ())
#define TR_CELL_RENDERER_TORRENT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_CELL_RENDERER_TORRENT, TrCellRendererTorrent))
#define TR_CELL_RENDERER_TORRENT_CLASS(klass)	  (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_CELL_RENDERER_TORRENT, TrCellRendererTorrentClass))
#define GTK_IS_CELL_RENDERER_TORRENT(obj)	  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_CELL_RENDERER_TORRENT))
#define GTK_IS_CELL_RENDERER_TORRENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_CELL_RENDERER_TORRENT))
#define TR_CELL_RENDERER_TORRENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_CELL_RENDERER_TORRENT, TrCellRendererTorrentClass))

typedef struct _TrCellRendererTorrent         TrCellRendererTorrent;
typedef struct _TrCellRendererTorrentClass    TrCellRendererTorrentClass;
typedef struct _TrCellRendererTorrentPrivate  TrCellRendererTorrentPrivate;

struct _TrCellRendererTorrent
{
  GtkCellRenderer parent_instance;
  
  /*< private >*/
  TrCellRendererTorrentPrivate *priv;
};

struct _TrCellRendererTorrentClass
{
  GtkCellRendererClass parent_class;

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};

GType		 tr_cell_renderer_torrent_get_type (void) G_GNUC_CONST;
GtkCellRenderer* tr_cell_renderer_torrent_new      (void);
void tr_cell_renderer_torrent_reset_style(TrCellRendererTorrent *);

G_END_DECLS

#endif  /* __TR_CELL_RENDERER_TORRENT_H__ */
