/*
  $Id$

  Copyright (c) 2006 Joshua Elsasser. All rights reserved.
   
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
