/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id:$
 */

#ifndef TORRENT_CELL_RENDERER_H
#define TORRENT_CELL_RENDERER_H

#include <glib-object.h>
#include <gtk/gtkcellrenderer.h>

#define TORRENT_CELL_RENDERER_TYPE (torrent_cell_renderer_get_type())

#define TORRENT_CELL_RENDERER(o) ( G_TYPE_CHECK_INSTANCE_CAST((o), \
                                   TORRENT_CELL_RENDERER_TYPE, \
                                   TorrentCellRenderer ) )

typedef struct TorrentCellRenderer TorrentCellRenderer;

typedef struct TorrentCellRendererClass TorrentCellRendererClass;

struct TorrentCellRenderer
{
    GtkCellRenderer parent;
    struct TorrentCellRendererPrivate * priv;
};

struct TorrentCellRendererClass
{
    GtkCellRendererClass parent;
};

GType torrent_cell_renderer_get_type( void );

GtkCellRenderer * torrent_cell_renderer_new( void );

#endif
