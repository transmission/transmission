/* gtkcellrenderertorrent.c
 * Copyright (C) 2002 Naba Kumar <kh_naba@users.sourceforge.net>
 * heavily modified by Jörgen Scheibengruber <mfcn@gmx.de>
 * heavily modified by Marco Pesenti Gritti <marco@gnome.org>
 * heavily modified by Josh Elsasser <josh@elsasser.org> for transmission
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

#include <gtk/gtk.h>

#include "gtkcellrenderertorrent.h"
#include "util.h"

enum { PROP_0, PROP_VALUE, PROP_TEXT, PROP_LABEL }; 

struct _GtkCellRendererTorrentPrivate {
  gfloat value;
  gchar *text;
  PangoAttrList *text_attrs;
  gchar *label;
  PangoAttrList *label_attrs;
  GtkStyle *style;
};

static void
finalize(GObject *object);
static void
get_property(GObject *obj, guint id, GValue *value, GParamSpec *spec);
static void
set_property(GObject *obj, guint id, const GValue *value, GParamSpec *spec);
static void
get_size(GtkCellRenderer *cell, GtkWidget *widget, GdkRectangle *area,
         gint *xoff, gint *yoff, gint *width, gint *height);
static void
render(GtkCellRenderer *cell, GdkWindow *window, GtkWidget *widget,
       GdkRectangle *bg, GdkRectangle *area, GdkRectangle *exp, guint flags);

     
G_DEFINE_TYPE(GtkCellRendererTorrent, gtk_cell_renderer_torrent, GTK_TYPE_CELL_RENDERER);

static void
gtk_cell_renderer_torrent_class_init (GtkCellRendererTorrentClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS (klass);
  
  object_class->finalize = finalize;
  object_class->get_property = get_property;
  object_class->set_property = set_property;
  
  cell_class->get_size = get_size;
  cell_class->render = render;
  
  g_object_class_install_property(
    object_class, PROP_VALUE,
    g_param_spec_float("value", "Value", "Value of the torrent bar",
                       0.0, 1.0, 0.0, G_PARAM_READWRITE));

  g_object_class_install_property(
    object_class, PROP_TEXT,
    g_param_spec_string ("text", "Text", "Text under the torrent bar",
  /* XXX should I have NULL or "" here, and is initial strdup needed? */
                         NULL, G_PARAM_READWRITE));

  g_object_class_install_property(
    object_class, PROP_LABEL,
    g_param_spec_string ("label", "Label", "Text on the torrent bar",
                         NULL, G_PARAM_READWRITE));

  g_type_class_add_private (object_class, 
			    sizeof (GtkCellRendererTorrentPrivate));
}

static void
gtk_cell_renderer_torrent_init(GtkCellRendererTorrent *tcell) {
  tcell->priv = G_TYPE_INSTANCE_GET_PRIVATE(
    tcell, GTK_TYPE_CELL_RENDERER_TORRENT, GtkCellRendererTorrentPrivate);
  tcell->priv->value = 0.0;
  tcell->priv->text = g_strdup("");
  tcell->priv->text_attrs = NULL;
  tcell->priv->label = g_strdup("");
  tcell->priv->text_attrs = NULL;
  tcell->priv->style = NULL;
}

GtkCellRenderer*
gtk_cell_renderer_torrent_new(void) {
  return g_object_new (GTK_TYPE_CELL_RENDERER_TORRENT, NULL);
}

/* XXX need to do this better somehow */
void
gtk_cell_renderer_torrent_reset_style(GtkCellRendererTorrent *tor) {
  if(NULL != tor->priv->style) {
    gtk_style_detach(tor->priv->style);
    gtk_style_unref(tor->priv->style);
    tor->priv->style = NULL;
  }
}

static void
finalize(GObject *object) {
  GtkCellRendererTorrent *tcell = GTK_CELL_RENDERER_TORRENT(object);

  g_free(tcell->priv->text);
  g_free(tcell->priv->label);

  if(NULL != tcell->priv->text_attrs)
    pango_attr_list_unref(tcell->priv->text_attrs);
  if(NULL != tcell->priv->label_attrs)
    pango_attr_list_unref(tcell->priv->label_attrs);
  if(NULL != tcell->priv->style) {
    gtk_style_detach(tcell->priv->style);
    gtk_style_unref(tcell->priv->style);
  }

  G_OBJECT_CLASS (gtk_cell_renderer_torrent_parent_class)->finalize(object);
}

static void
get_property(GObject *object, guint id, GValue *value, GParamSpec *pspec) {
  GtkCellRendererTorrent *tcell = GTK_CELL_RENDERER_TORRENT (object);
  
  switch (id) {
    case PROP_VALUE:
      g_value_set_float (value, tcell->priv->value);
      break;
    case PROP_TEXT:
      g_value_set_string (value, tcell->priv->text);
      break;
    case PROP_LABEL:
      g_value_set_string (value, tcell->priv->label);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
  }
}

static void
set_property(GObject *obj, guint id, const GValue *value, GParamSpec *spec) {
  GtkCellRendererTorrent *tcell = GTK_CELL_RENDERER_TORRENT(obj);
  gchar **prop = NULL; 
  PangoAttrList **attrs = NULL;
  /*GError *err = NULL;*/
  const gchar *markup;
  
  switch(id) {
    case PROP_VALUE:
      tcell->priv->value = g_value_get_float(value);
      break;

    case PROP_TEXT:
      prop = &tcell->priv->text;
      attrs = &tcell->priv->text_attrs;
      /* fallthrough */

    case PROP_LABEL:
      if(PROP_LABEL == id) {
        prop = &tcell->priv->label;
        attrs = &tcell->priv->label_attrs;
      }

      if(NULL == (markup = g_value_get_string(value)))
        markup = "";

      g_free(*prop);
      if(NULL != *attrs)
        pango_attr_list_unref(*attrs);

      *prop = g_strdup(markup);

      /*
      if(pango_parse_markup(markup, -1, 0, attrs, prop, NULL, &err))
        break;
        
      g_warning ("Failed to parse markup: %s", err->message);
      g_error_free(err);
      */
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
  }
}

static void
get_size(GtkCellRenderer *cell, GtkWidget *widget, GdkRectangle *area,
         gint *xoff, gint *yoff, gint *width, gint *height) {
  GtkCellRendererTorrent *tcell = GTK_CELL_RENDERER_TORRENT(cell);
  /* XXX do I have to unref the context? */
  PangoLayout *layout = pango_layout_new(gtk_widget_get_pango_context(widget));
  PangoRectangle rect;
  gint h = cell->ypad * 2;
  gint w1, w2;

  pango_layout_set_markup(layout, tcell->priv->label, -1);
  pango_layout_get_pixel_extents(layout, NULL, &rect);
  w1 = rect.width;
  h += rect.height;

  pango_layout_set_markup(layout, tcell->priv->text, -1);
  pango_layout_get_pixel_extents(layout, NULL, &rect);
  w2 = rect.width;
  h += rect.height;

  if(NULL != xoff)
    *xoff = 0;
  if(NULL != yoff)
    *yoff = (area->height - h) / 2;
  if(NULL != width)
    *width = MAX(w1, w2) + cell->xpad * 2;
  if(NULL != height)
    *height = h;

  g_object_unref(layout);
}

#define RECTARGS(rect)          (rect).x, (rect).y, (rect).width, (rect).height

static void
render(GtkCellRenderer *cell, GdkWindow *window, GtkWidget *widget,
       GdkRectangle *bg SHUTUP, GdkRectangle *area, GdkRectangle *exp SHUTUP,
       guint flags) {
  GtkCellRendererTorrent *tcell = GTK_CELL_RENDERER_TORRENT(cell);
  PangoContext *ctx = gtk_widget_get_pango_context(widget);
  PangoLayout *llayout, *tlayout;
  PangoRectangle lrect, trect;
  GdkRectangle bar, complete, text;
  gboolean rtl;
  GtkStyle *style;

  /* try to use the style for GtkProgressBar */
  if(NULL == tcell->priv->style)
    if(NULL != (tcell->priv->style = gtk_rc_get_style_by_paths(
                  gtk_widget_get_settings(widget), NULL, NULL,
                  gtk_progress_bar_get_type())))
      tcell->priv->style = gtk_style_attach(gtk_style_ref(tcell->priv->style),
                                            window);
  style = (NULL == tcell->priv->style ? widget->style : tcell->priv->style);

  rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;

  /* get the text layouts */
  llayout = pango_layout_new(ctx);
  /* XXX cache parsed markup? */
  pango_layout_set_markup(llayout, tcell->priv->label, -1);
  pango_layout_get_pixel_extents(llayout, NULL, &lrect);
  tlayout = pango_layout_new(ctx);
  pango_layout_set_markup(tlayout, tcell->priv->text, -1);
  pango_layout_get_pixel_extents (tlayout, NULL, &trect);

  /* set up the dimensions for the bar */
  bar.x = area->x + cell->xpad;
  bar.y = area->y + cell->ypad +
    (area->height - lrect.height - trect.height) / 2;
  bar.width = area->width - cell->xpad * 2;
  bar.height = lrect.height;

  /* set up the dimensions for the complete portion of the bar */
  complete.x = bar.x + style->xthickness;
  complete.y = bar.y + style->ythickness;
  complete.width = (bar.width - style->xthickness * 2) * tcell->priv->value;
  complete.height = bar.height - style->ythickness * 2;
  if(rtl)
    complete.x += bar.width - complete.width;

  /* set up the dimensions for the text under the bar */
  text.x = bar.x;
  text.y = bar.y + bar.height;
  text.width = bar.width;
  text.height = area->height - bar.height;

  /* draw the background of the bar */
  if(complete.width < bar.width)
    gtk_paint_box(style, window, GTK_STATE_NORMAL, GTK_SHADOW_IN, 
                  NULL, widget, "trough", RECTARGS(bar));

  /* draw the complete portion of the bar */
  if(0 < complete.width)
    gtk_paint_box(style, window, GTK_STATE_PRELIGHT, GTK_SHADOW_OUT,
                  NULL, widget, "bar", RECTARGS(complete));

  /* draw the text under the bar */
  gtk_paint_layout(style, window, (GTK_CELL_RENDERER_SELECTED & flags ?
                   GTK_STATE_SELECTED : GTK_STATE_NORMAL), FALSE, &text,
                   widget, "cellrenderertext", text.x, text.y, tlayout);

  /* free memory */
  g_object_unref(llayout);
  g_object_unref(tlayout);
}
