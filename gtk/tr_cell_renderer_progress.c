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

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "tr_cell_renderer_progress.h"
#include "transmission.h"
#include "util.h"

enum {
  P_TEXT = 1,
  P_MARKUP,
  P_SPACER,
  P_PROG,
};

static void
class_init(gpointer gclass, gpointer gdata);
static void
init(GTypeInstance *instance, gpointer gclass);
static void
set_property(GObject *obj, guint id, const GValue *val, GParamSpec *pspec);
static void
get_property(GObject *obj, guint id, GValue *val, GParamSpec *pspec);
static void
dispose(GObject *obj);
static void
finalize(GObject *obj);
static void
get_size(GtkCellRenderer *rend, GtkWidget *widget, GdkRectangle *cell,
         gint *xoff, gint *yoff, gint *width, gint *height);
static void
render(GtkCellRenderer *rend, GdkWindow *win, GtkWidget *widget,
       GdkRectangle *bg, GdkRectangle *cell, GdkRectangle *expose,
       GtkCellRendererState flags);
static GtkStyle *
getstyle(TrCellRendererProgress *self, GtkWidget *wid, GdkWindow *win);

GType
tr_cell_renderer_progress_get_type(void) {
  static GType type = 0;

  if(0 == type) {
    static const GTypeInfo info = {
      sizeof (TrCellRendererProgressClass),
      NULL,   /* base_init */
      NULL,   /* base_finalize */
      class_init,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      sizeof (TrCellRendererProgress),
      0,      /* n_preallocs */
      init, /* instance_init */
      NULL,
    };
    type = g_type_register_static(GTK_TYPE_CELL_RENDERER,
                                  "TrCellRendererProgressType", &info, 0);
  }
  return type;
}

static void
class_init(gpointer gclass, gpointer gdata SHUTUP) {
  GObjectClass *gobjclass = G_OBJECT_CLASS(gclass);
  GtkCellRendererClass *rendclass = GTK_CELL_RENDERER_CLASS(gclass);
  GParamSpec *pspec;

  gobjclass->set_property = set_property;
  gobjclass->get_property = get_property;
  gobjclass->dispose      = dispose;
  gobjclass->finalize     = finalize;
  rendclass->get_size     = get_size;
  rendclass->render       = render;

  pspec = g_param_spec_string("markup", "Markup", "Marked up text to render",
                              NULL, G_PARAM_READWRITE);
  g_object_class_install_property(gobjclass, P_MARKUP, pspec);

  pspec = g_param_spec_string("bar-sizing", "Bar sizing",
                              "Text to determine size of progress bar",
                              NULL, G_PARAM_READWRITE);
  g_object_class_install_property(gobjclass, P_SPACER, pspec);

  pspec = g_param_spec_float("progress", "Progress", "Progress",
                             0.0, 1.0, 0.0, G_PARAM_READWRITE);
  g_object_class_install_property(gobjclass, P_PROG, pspec);
}

static void
init(GTypeInstance *instance, gpointer gclass SHUTUP) {
  TrCellRendererProgress *self = (TrCellRendererProgress *)instance;

  self->rend      = gtk_cell_renderer_text_new();
  self->style     = NULL;
  self->text      = NULL;
  self->spacer    = NULL;
  self->barwidth  = -1;
  self->barheight = -1;
  self->progress  = 0.0;
  self->disposed  = FALSE;

  g_object_ref(self->rend);
  gtk_object_sink(GTK_OBJECT(self->rend));
}

static void
set_property(GObject *obj, guint id, const GValue *val,
                                   GParamSpec *pspec) {
  TrCellRendererProgress *self = (TrCellRendererProgress*)obj;

  if(self->disposed)
    return;

  switch(id) {
    case P_MARKUP:
      g_free(self->text);
      self->text = g_value_dup_string(val);
      g_object_set_property(G_OBJECT(self->rend), "markup", val);
      break;
    case P_SPACER:
      g_free(self->spacer);
      self->spacer    = g_value_dup_string(val);
      self->barwidth  = -1;
      self->barheight = -1;
      break;
    case P_PROG:
      self->progress = g_value_get_float(val);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
      break;
  }
}

static void
get_property(GObject *obj, guint id, GValue *val,
                                   GParamSpec *pspec) {
  TrCellRendererProgress *self = (TrCellRendererProgress*)obj;

  if(self->disposed)
    return;

  switch(id) {
    case P_MARKUP:
      g_value_set_string(val, self->text);
      break;
    case P_SPACER:
      g_value_set_string(val, self->spacer);
      break;
    case P_PROG:
      g_value_set_float(val, self->progress);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
      break;
  }
}

static void
dispose(GObject *obj) {
  GObjectClass *parent =
    g_type_class_peek(g_type_parent(TR_CELL_RENDERER_PROGRESS_TYPE));
  TrCellRendererProgress *self = (TrCellRendererProgress*)obj;

  if(self->disposed)
    return;
  self->disposed = TRUE;

  g_object_unref(self->rend);
  tr_cell_renderer_progress_reset_style(self);

  /* Chain up to the parent class */
  parent->dispose(obj);
}

static void
finalize(GObject *obj) {
  GObjectClass *parent =
    g_type_class_peek(g_type_parent(TR_CELL_RENDERER_PROGRESS_TYPE));
  TrCellRendererProgress *self = (TrCellRendererProgress*)obj;

  g_free(self->text);
  g_free(self->spacer);

  /* Chain up to the parent class */
  parent->finalize(obj);
}

GtkCellRenderer *
tr_cell_renderer_progress_new(void) {
  return g_object_new(TR_CELL_RENDERER_PROGRESS_TYPE, NULL);
}

static void
get_size(GtkCellRenderer *rend, GtkWidget *wid, GdkRectangle *cell,
         gint *xoff, gint *yoff, gint *width, gint *height) {
  TrCellRendererProgress *self;
  GdkRectangle rect;
  int xpad, ypad;

  TR_IS_CELL_RENDERER_PROGRESS(rend);
  self = TR_CELL_RENDERER_PROGRESS(rend);

  /* calculate and cache minimum bar width and height */
  if(0 > self->barwidth || 0 > self->barheight) {
    xpad = self->rend->xpad;
    ypad = self->rend->ypad;
    g_object_set(self->rend, "markup", self->spacer, "xpad", 0, "ypad", 0,
                 NULL);
    gtk_cell_renderer_get_size(self->rend, wid, NULL, NULL, NULL,
                               &self->barwidth, &self->barheight);
    g_object_set(self->rend, "markup", self->text, "xpad", xpad, "ypad", ypad,
                 NULL);
  }

  /* get the text size */
  if(NULL != cell) {
    rect = *cell;
    rect.height -= self->barheight;
    cell = &rect;
  }
  gtk_cell_renderer_get_size(self->rend, wid, cell, xoff, yoff, width, height);

  if(NULL != width && self->barwidth > *width)
    *width = self->barwidth;
  if(NULL != height)
    *height += self->barheight + (NULL == yoff ? 0 : *yoff);
}

static void
render(GtkCellRenderer *rend, GdkWindow *win, GtkWidget *wid, GdkRectangle *bg,
       GdkRectangle *cell, GdkRectangle *expose, GtkCellRendererState flags) {
  TrCellRendererProgress *self;
  GdkRectangle rect, full, empty;
  GtkStyle *style;

  TR_IS_CELL_RENDERER_PROGRESS(rend);
  self = TR_CELL_RENDERER_PROGRESS(rend);

  style = getstyle(self, wid, win);

  /* make sure we have a cached bar width */
  if(0 > self->barwidth || 0 > self->barheight)
    get_size(rend, wid, NULL, NULL, NULL, NULL, NULL);
  g_assert(0 < self->barwidth && 0 < self->barheight);

  /* set up the dimensions for the bar and text */
  rect         = *cell;
  rect.x      += rend->xpad + style->xthickness;
  rect.y      += rend->ypad + style->ythickness;
  rect.width  -= (rend->xpad + style->xthickness) * 2;
  rect.height -= (rend->ypad + style->ythickness) * 2;
  empty        = rect;
  empty.height = self->barheight;
  full         = empty;
  full.x      += style->xthickness;
  full.y      += style->ythickness;
  full.width  -= style->xthickness * 2;
  full.height -= style->ythickness * 2;
  rect.y      += self->barheight;
  rect.height -= self->barheight;

  /* draw the bar background */
  gtk_paint_box(style, win, GTK_STATE_NORMAL, GTK_SHADOW_IN, NULL, wid,
                "trough", empty.x, empty.y, empty.width, empty.height);

  /* figure the width of the complete portion of the bar */
  if(PANGO_DIRECTION_RTL ==
     pango_context_get_base_dir(gtk_widget_get_pango_context(wid)))
    full.x += full.width - (full.width * self->progress);
  full.width *= self->progress;

  /* draw the complete portion of the bar */
  if(0 < full.width)
    gtk_paint_box(style, win, GTK_STATE_PRELIGHT, GTK_SHADOW_OUT, NULL,
                  wid, "bar", full.x, full.y, full.width, full.height);

  /* draw the text */
  if(0 < rect.height)
    gtk_cell_renderer_render(self->rend, win, wid, bg, &rect, expose, flags);
}

/* ugly hack to get the style for GtkProgressBar */
static GtkStyle *
getstyle(TrCellRendererProgress *self, GtkWidget *wid, GdkWindow *win) {
  if(NULL == self->style) {
    self->style = gtk_rc_get_style_by_paths(gtk_widget_get_settings(wid), NULL,
                                            NULL, GTK_TYPE_PROGRESS_BAR);
    if(NULL != self->style)
      self->style = gtk_style_attach(gtk_style_ref(self->style), win);
  }

  return (NULL == self->style ? wid->style : self->style);
}

/* hack to make the GtkProgressBar style hack work when the theme changes */
void
tr_cell_renderer_progress_reset_style(TrCellRendererProgress *self) {
  TR_IS_CELL_RENDERER_PROGRESS(self);

  if(NULL != self->style) {
    gtk_style_detach(self->style);
    gtk_style_unref(self->style);
    self->style = NULL;
  }
}
