/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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

#ifndef TR_CELL_RENDERER_PROGRESS_H
#define TR_CELL_RENDERER_PROGRESS_H

#include <glib-object.h>
#include <gtk/gtk.h>

#define TR_CELL_RENDERER_PROGRESS_TYPE (tr_cell_renderer_progress_get_type())
#define TR_CELL_RENDERER_PROGRESS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TR_CELL_RENDERER_PROGRESS_TYPE, \
  TrCellRendererProgress))
#define TR_CELL_RENDERER_PROGRESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TR_CELL_RENDERER_PROGRESS_TYPE,\
                           TrCellRendererProgressClass))
#define TR_IS_CELL_RENDERER_PROGRESS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TR_CELL_RENDERER_PROGRESS_TYPE))
#define TR_IS_CELL_RENDERER_PROGRESS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TR_CELL_RENDERER_PROGRESS_TYPE))
#define TR_CELL_RENDERER_PROGRESS_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), TR_CELL_RENDERER_PROGRESS_TYPE, \
                             TrCellRendererProgressClass))

typedef struct _TrCellRendererProgress TrCellRendererProgress;
typedef struct _TrCellRendererProgressClass TrCellRendererProgressClass;

/* treat the contents of this structure as private */
struct _TrCellRendererProgress {
  GtkCellRenderer parent;
  GtkCellRenderer *rend;
  GtkStyle *style;
  char *text;
  char *spacer;
  int barwidth;
  int barheight;
  gfloat progress;
  gboolean disposed;
};

struct _TrCellRendererProgressClass {
  GtkCellRendererClass parent;
};

GType
tr_cell_renderer_progress_get_type(void);

GtkCellRenderer *
tr_cell_renderer_progress_new(void);

void
tr_cell_renderer_progress_reset_style(TrCellRendererProgress *self);

#endif
