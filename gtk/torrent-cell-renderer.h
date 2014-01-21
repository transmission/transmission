/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef GTR_TORRENT_CELL_RENDERER_H
#define GTR_TORRENT_CELL_RENDERER_H

#include <gtk/gtk.h>

#define TORRENT_CELL_RENDERER_TYPE (torrent_cell_renderer_get_type ())

#define TORRENT_CELL_RENDERER(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), \
                                 TORRENT_CELL_RENDERER_TYPE, \
                                 TorrentCellRenderer))

typedef struct TorrentCellRenderer TorrentCellRenderer;

typedef struct TorrentCellRendererClass TorrentCellRendererClass;

struct TorrentCellRenderer
{
    GtkCellRenderer parent;

    /*< private >*/
    struct TorrentCellRendererPrivate * priv;
};

struct TorrentCellRendererClass
{
    GtkCellRendererClass parent;
};

GType torrent_cell_renderer_get_type (void) G_GNUC_CONST;

GtkCellRenderer * torrent_cell_renderer_new (void);

#endif /* GTR_TORRENT_CELL_RENDERER_H */
