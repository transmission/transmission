/*
 * @file libsexy/sexy-icon-entry.c Entry widget
 *
 * @Copyright (C) 2004-2006 Christian Hammond.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA  02111-1307, USA.
 */
#include <config.h>
#include <string.h>
#include <gtk/gtk.h>
#include "sexy-icon-entry.h"

#define ICON_MARGIN 2
#define MAX_ICONS 2

#define IS_VALID_ICON_ENTRY_POSITION(pos) \
	((pos) == SEXY_ICON_ENTRY_PRIMARY || \
	 (pos) == SEXY_ICON_ENTRY_SECONDARY)

typedef struct
{
	GtkImage *icon;
	gboolean highlight;
	gboolean hovered;
	GdkWindow *window;

} SexyIconInfo;

struct _SexyIconEntryPriv
{
	SexyIconInfo icons[MAX_ICONS];

	gulong icon_released_id;
};

enum
{
	ICON_PRESSED,
	ICON_RELEASED,
	LAST_SIGNAL
};

static void sexy_icon_entry_class_init(SexyIconEntryClass *klass);
static void sexy_icon_entry_editable_init(GtkEditableClass *iface);
static void sexy_icon_entry_init(SexyIconEntry *entry);
static void sexy_icon_entry_finalize(GObject *obj);
static void sexy_icon_entry_destroy(GtkObject *obj);
static void sexy_icon_entry_map(GtkWidget *widget);
static void sexy_icon_entry_unmap(GtkWidget *widget);
static void sexy_icon_entry_realize(GtkWidget *widget);
static void sexy_icon_entry_unrealize(GtkWidget *widget);
static void sexy_icon_entry_size_request(GtkWidget *widget,
										  GtkRequisition *requisition);
static void sexy_icon_entry_size_allocate(GtkWidget *widget,
										   GtkAllocation *allocation);
static gint sexy_icon_entry_expose(GtkWidget *widget, GdkEventExpose *event);
static gint sexy_icon_entry_enter_notify(GtkWidget *widget,
											   GdkEventCrossing *event);
static gint sexy_icon_entry_leave_notify(GtkWidget *widget,
											   GdkEventCrossing *event);
static gint sexy_icon_entry_button_press(GtkWidget *widget,
											   GdkEventButton *event);
static gint sexy_icon_entry_button_release(GtkWidget *widget,
												 GdkEventButton *event);

static GtkEntryClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE_EXTENDED(SexyIconEntry, sexy_icon_entry, GTK_TYPE_ENTRY,
					   0,
					   G_IMPLEMENT_INTERFACE(GTK_TYPE_EDITABLE,
											 sexy_icon_entry_editable_init));

static void
sexy_icon_entry_class_init(SexyIconEntryClass *klass)
{
	GObjectClass *gobject_class;
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkEntryClass *entry_class;

	parent_class = g_type_class_peek_parent(klass);

	gobject_class = G_OBJECT_CLASS(klass);
	object_class  = GTK_OBJECT_CLASS(klass);
	widget_class  = GTK_WIDGET_CLASS(klass);
	entry_class   = GTK_ENTRY_CLASS(klass);

	gobject_class->finalize = sexy_icon_entry_finalize;

	object_class->destroy = sexy_icon_entry_destroy;

	widget_class->map = sexy_icon_entry_map;
	widget_class->unmap = sexy_icon_entry_unmap;
	widget_class->realize = sexy_icon_entry_realize;
	widget_class->unrealize = sexy_icon_entry_unrealize;
	widget_class->size_request = sexy_icon_entry_size_request;
	widget_class->size_allocate = sexy_icon_entry_size_allocate;
	widget_class->expose_event = sexy_icon_entry_expose;
	widget_class->enter_notify_event = sexy_icon_entry_enter_notify;
	widget_class->leave_notify_event = sexy_icon_entry_leave_notify;
	widget_class->button_press_event = sexy_icon_entry_button_press;
	widget_class->button_release_event = sexy_icon_entry_button_release;

	/**
	 * SexyIconEntry::icon-pressed:
	 * @entry: The entry on which the signal is emitted.
	 * @icon_pos: The position of the clicked icon.
	 * @button: The mouse button clicked.
	 *
	 * The ::icon-pressed signal is emitted when an icon is clicked.
	 */
	signals[ICON_PRESSED] =
		g_signal_new("icon_pressed",
					 G_TYPE_FROM_CLASS(gobject_class),
					 G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
					 G_STRUCT_OFFSET(SexyIconEntryClass, icon_pressed),
					 NULL, NULL,
					 gtk_marshal_VOID__INT_INT,
					 G_TYPE_NONE, 2,
					 G_TYPE_INT,
					 G_TYPE_INT);

	/**
	 * SexyIconEntry::icon-released:
	 * @entry: The entry on which the signal is emitted.
	 * @icon_pos: The position of the clicked icon.
	 * @button: The mouse button clicked.
	 *
	 * The ::icon-released signal is emitted on the button release from a
	 * mouse click.
	 */
	signals[ICON_RELEASED] =
		g_signal_new("icon_released",
					 G_TYPE_FROM_CLASS(gobject_class),
					 G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
					 G_STRUCT_OFFSET(SexyIconEntryClass, icon_released),
					 NULL, NULL,
					 gtk_marshal_VOID__INT_INT,
					 G_TYPE_NONE, 2,
					 G_TYPE_INT,
					 G_TYPE_INT);
}

static void
sexy_icon_entry_editable_init(GtkEditableClass *iface)
{
};

static void
sexy_icon_entry_init(SexyIconEntry *entry)
{
	entry->priv = g_new0(SexyIconEntryPriv, 1);
}

static void
sexy_icon_entry_finalize(GObject *obj)
{
	SexyIconEntry *entry;

	g_return_if_fail(obj != NULL);
	g_return_if_fail(SEXY_IS_ICON_ENTRY(obj));

	entry = SEXY_ICON_ENTRY(obj);

	g_free(entry->priv);

	if (G_OBJECT_CLASS(parent_class)->finalize)
		G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void
sexy_icon_entry_destroy(GtkObject *obj)
{
	SexyIconEntry *entry;

	entry = SEXY_ICON_ENTRY(obj);

	sexy_icon_entry_set_icon(entry, SEXY_ICON_ENTRY_PRIMARY, NULL);
	sexy_icon_entry_set_icon(entry, SEXY_ICON_ENTRY_SECONDARY, NULL);

	if (GTK_OBJECT_CLASS(parent_class)->destroy)
		GTK_OBJECT_CLASS(parent_class)->destroy(obj);
}

static void
sexy_icon_entry_map(GtkWidget *widget)
{
	if (GTK_WIDGET_REALIZED(widget) && !GTK_WIDGET_MAPPED(widget))
	{
		SexyIconEntry *entry = SEXY_ICON_ENTRY(widget);
		int i;

		GTK_WIDGET_CLASS(parent_class)->map(widget);

		for (i = 0; i < MAX_ICONS; i++)
		{
			if (entry->priv->icons[i].icon != NULL)
				gdk_window_show(entry->priv->icons[i].window);
		}
	}
}

static void
sexy_icon_entry_unmap(GtkWidget *widget)
{
	if (GTK_WIDGET_MAPPED(widget))
	{
		SexyIconEntry *entry = SEXY_ICON_ENTRY(widget);
		int i;

		for (i = 0; i < MAX_ICONS; i++)
		{
			if (entry->priv->icons[i].icon != NULL)
				gdk_window_hide(entry->priv->icons[i].window);
		}

		GTK_WIDGET_CLASS(parent_class)->unmap(widget);
	}
}

static gint
get_icon_width(SexyIconEntry *entry, SexyIconEntryPosition icon_pos)
{
	GtkRequisition requisition;
	gint menu_icon_width;
	gint width;
	SexyIconInfo *icon_info = &entry->priv->icons[icon_pos];

	if (icon_info->icon == NULL)
		return 0;

	gtk_widget_size_request(GTK_WIDGET(icon_info->icon), &requisition);
	gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &menu_icon_width, NULL);

	width = MAX(requisition.width, menu_icon_width);

	return width;
}

static void
get_borders(SexyIconEntry *entry, gint *xborder, gint *yborder)
{
	GtkWidget *widget = GTK_WIDGET(entry);
	gint focus_width;
	gboolean interior_focus;

	gtk_widget_style_get(widget,
						 "interior-focus", &interior_focus,
						 "focus-line-width", &focus_width,
						 NULL);

	if (gtk_entry_get_has_frame(GTK_ENTRY(entry)))
	{
		*xborder = widget->style->xthickness;
		*yborder = widget->style->ythickness;
	}
	else
	{
		*xborder = 0;
		*yborder = 0;
	}

	if (!interior_focus)
	{
		*xborder += focus_width;
		*yborder += focus_width;
	}
}

static void
get_text_area_size(SexyIconEntry *entry, GtkAllocation *alloc)
{
	GtkWidget *widget = GTK_WIDGET(entry);
	GtkRequisition requisition;
	gint xborder, yborder;

	gtk_widget_get_child_requisition(widget, &requisition);
	get_borders(entry, &xborder, &yborder);

	alloc->x      = xborder;
	alloc->y      = yborder;
	alloc->width  = widget->allocation.width - xborder * 2;
	alloc->height = requisition.height       - yborder * 2;
}

static void
get_icon_allocation(SexyIconEntry *icon_entry,
					gboolean left,
					GtkAllocation *widget_alloc,
					GtkAllocation *text_area_alloc,
					GtkAllocation *allocation,
					SexyIconEntryPosition *icon_pos)
{
	gboolean rtl;

	rtl = (gtk_widget_get_direction(GTK_WIDGET(icon_entry)) ==
		   GTK_TEXT_DIR_RTL);

	if (left)
		*icon_pos = (rtl ? SEXY_ICON_ENTRY_SECONDARY : SEXY_ICON_ENTRY_PRIMARY);
	else
		*icon_pos = (rtl ? SEXY_ICON_ENTRY_PRIMARY : SEXY_ICON_ENTRY_SECONDARY);

	allocation->y = text_area_alloc->y;
	allocation->width = get_icon_width(icon_entry, *icon_pos);
	allocation->height = text_area_alloc->height;

	if (left)
		allocation->x = text_area_alloc->x + ICON_MARGIN;
	else
	{
		allocation->x = text_area_alloc->x + text_area_alloc->width -
		                allocation->width - ICON_MARGIN;
	}
}

static void
sexy_icon_entry_realize(GtkWidget *widget)
{
	SexyIconEntry *entry = SEXY_ICON_ENTRY(widget);
	GdkWindowAttr attributes;
	gint attributes_mask;
	int i;

	GTK_WIDGET_CLASS(parent_class)->realize(widget);

	attributes.x = 0;
	attributes.y = 0;
	attributes.width = 1;
	attributes.height = 1;
	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.visual = gtk_widget_get_visual(widget);
	attributes.colormap = gtk_widget_get_colormap(widget);
	attributes.event_mask = gtk_widget_get_events(widget);
	attributes.event_mask |=
		(GDK_EXPOSURE_MASK
		 | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
		 | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

	for (i = 0; i < MAX_ICONS; i++)
	{
		SexyIconInfo *icon_info;

		icon_info = &entry->priv->icons[i];
		icon_info->window = gdk_window_new(widget->window, &attributes,
										   attributes_mask);
		gdk_window_set_user_data(icon_info->window, widget);

		gdk_window_set_background(icon_info->window,
			&widget->style->base[GTK_WIDGET_STATE(widget)]);
	}

	gtk_widget_queue_resize(widget);
}

static void
sexy_icon_entry_unrealize(GtkWidget *widget)
{
	SexyIconEntry *entry = SEXY_ICON_ENTRY(widget);
	int i;

	GTK_WIDGET_CLASS(parent_class)->unrealize(widget);

	for (i = 0; i < MAX_ICONS; i++)
	{
		SexyIconInfo *icon_info = &entry->priv->icons[i];

		gdk_window_destroy(icon_info->window);
		icon_info->window = NULL;
	}
}

static void
sexy_icon_entry_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	GtkEntry *gtkentry;
	SexyIconEntry *entry;
	gint icon_widths = 0;
	int i;

	gtkentry = GTK_ENTRY(widget);
	entry    = SEXY_ICON_ENTRY(widget);

	for (i = 0; i < MAX_ICONS; i++)
	{
		int icon_width = get_icon_width(entry, i);

		if (icon_width > 0)
			icon_widths += icon_width + ICON_MARGIN;
	}

	GTK_WIDGET_CLASS(parent_class)->size_request(widget, requisition);

	if (icon_widths > requisition->width)
		requisition->width += icon_widths;
}

static void
place_windows(SexyIconEntry *icon_entry, GtkAllocation *widget_alloc)
{
	SexyIconEntryPosition left_icon_pos;
	SexyIconEntryPosition right_icon_pos;
	GtkAllocation left_icon_alloc;
	GtkAllocation right_icon_alloc;
	GtkAllocation text_area_alloc;

	get_text_area_size(icon_entry, &text_area_alloc);
	get_icon_allocation(icon_entry, TRUE, widget_alloc, &text_area_alloc,
						&left_icon_alloc, &left_icon_pos);
	get_icon_allocation(icon_entry, FALSE, widget_alloc, &text_area_alloc,
						&right_icon_alloc, &right_icon_pos);

	if (left_icon_alloc.width > 0)
	{
		text_area_alloc.x = left_icon_alloc.x + left_icon_alloc.width +
		                    ICON_MARGIN;
	}

	if (right_icon_alloc.width > 0)
		text_area_alloc.width -= right_icon_alloc.width + ICON_MARGIN;

	text_area_alloc.width -= text_area_alloc.x;

	gdk_window_move_resize(icon_entry->priv->icons[left_icon_pos].window,
						   left_icon_alloc.x, left_icon_alloc.y,
						   left_icon_alloc.width, left_icon_alloc.height);

	gdk_window_move_resize(icon_entry->priv->icons[right_icon_pos].window,
						   right_icon_alloc.x, right_icon_alloc.y,
						   right_icon_alloc.width, right_icon_alloc.height);

	gdk_window_move_resize(GTK_ENTRY(icon_entry)->text_area,
						   text_area_alloc.x, text_area_alloc.y,
						   text_area_alloc.width, text_area_alloc.height);
}

static void
sexy_icon_entry_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	g_return_if_fail(SEXY_IS_ICON_ENTRY(widget));
	g_return_if_fail(allocation != NULL);

	widget->allocation = *allocation;

	GTK_WIDGET_CLASS(parent_class)->size_allocate(widget, allocation);

	if (GTK_WIDGET_REALIZED(widget))
		place_windows(SEXY_ICON_ENTRY(widget), allocation);
}

static GdkPixbuf *
get_pixbuf_from_icon(SexyIconEntry *entry, SexyIconEntryPosition icon_pos)
{
	GdkPixbuf *pixbuf = NULL;
	gchar *stock_id;
	SexyIconInfo *icon_info = &entry->priv->icons[icon_pos];
	GtkIconSize size;

	switch (gtk_image_get_storage_type(GTK_IMAGE(icon_info->icon)))
	{
		case GTK_IMAGE_PIXBUF:
			pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(icon_info->icon));
			g_object_ref(pixbuf);
			break;

		case GTK_IMAGE_STOCK:
			gtk_image_get_stock(GTK_IMAGE(icon_info->icon), &stock_id, &size);
			pixbuf = gtk_widget_render_icon(GTK_WIDGET(entry),
											stock_id, size, NULL);
			break;

		default:
			return NULL;
	}

	return pixbuf;
}

/* Kudos to the gnome-panel guys. */
static void
colorshift_pixbuf(GdkPixbuf *dest, GdkPixbuf *src, int shift)
{
	gint i, j;
	gint width, height, has_alpha, src_rowstride, dest_rowstride;
	guchar *target_pixels;
	guchar *original_pixels;
	guchar *pix_src;
	guchar *pix_dest;
	int val;
	guchar r, g, b;

	has_alpha       = gdk_pixbuf_get_has_alpha(src);
	width           = gdk_pixbuf_get_width(src);
	height          = gdk_pixbuf_get_height(src);
	src_rowstride   = gdk_pixbuf_get_rowstride(src);
	dest_rowstride  = gdk_pixbuf_get_rowstride(dest);
	original_pixels = gdk_pixbuf_get_pixels(src);
	target_pixels   = gdk_pixbuf_get_pixels(dest);

	for (i = 0; i < height; i++)
	{
		pix_dest = target_pixels   + i * dest_rowstride;
		pix_src  = original_pixels + i * src_rowstride;

		for (j = 0; j < width; j++)
		{
			r = *(pix_src++);
			g = *(pix_src++);
			b = *(pix_src++);

			val = r + shift;
			*(pix_dest++) = CLAMP(val, 0, 255);

			val = g + shift;
			*(pix_dest++) = CLAMP(val, 0, 255);

			val = b + shift;
			*(pix_dest++) = CLAMP(val, 0, 255);

			if (has_alpha)
				*(pix_dest++) = *(pix_src++);
		}
	}
}

static void
draw_icon(GtkWidget *widget, SexyIconEntryPosition icon_pos)
{
	SexyIconEntry *entry = SEXY_ICON_ENTRY(widget);
	SexyIconInfo *icon_info = &entry->priv->icons[icon_pos];
	GdkPixbuf *pixbuf;
	gint x, y, width, height;

	if (icon_info->icon == NULL || !GTK_WIDGET_REALIZED(widget))
		return;

	if ((pixbuf = get_pixbuf_from_icon(entry, icon_pos)) == NULL)
		return;

	gdk_drawable_get_size(icon_info->window, &width, &height);

	if (width == 1 || height == 1)
	{
		/*
		 * size_allocate hasn't been called yet. These are the default values.
		 */
		return;
	}

	if (gdk_pixbuf_get_height(pixbuf) > height)
	{
		GdkPixbuf *temp_pixbuf;
		int scale;

		scale = height - (2 * ICON_MARGIN);

		temp_pixbuf = gdk_pixbuf_scale_simple(pixbuf, scale, scale, GDK_INTERP_BILINEAR);

		g_object_unref(pixbuf);

		pixbuf = temp_pixbuf;
	}

	x = (width  - gdk_pixbuf_get_width(pixbuf)) / 2;
	y = (height - gdk_pixbuf_get_height(pixbuf)) / 2;

	if (icon_info->hovered)
	{
		GdkPixbuf *temp_pixbuf;

		temp_pixbuf = gdk_pixbuf_copy(pixbuf);

		colorshift_pixbuf(temp_pixbuf, pixbuf, 30);

		g_object_unref(pixbuf);

		pixbuf = temp_pixbuf;
	}

	gdk_draw_pixbuf(icon_info->window, widget->style->black_gc, pixbuf,
					0, 0, x, y, -1, -1,
					GDK_RGB_DITHER_NORMAL, 0, 0);

	g_object_unref(pixbuf);
}

static gint
sexy_icon_entry_expose(GtkWidget *widget, GdkEventExpose *event)
{
	SexyIconEntry *entry;

	g_return_val_if_fail(SEXY_IS_ICON_ENTRY(widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	entry = SEXY_ICON_ENTRY(widget);

	if (GTK_WIDGET_DRAWABLE(widget))
	{
		gboolean found = FALSE;
		int i;

		for (i = 0; i < MAX_ICONS && !found; i++)
		{
			SexyIconInfo *icon_info = &entry->priv->icons[i];

			if (event->window == icon_info->window)
			{
				gint width;
				GtkAllocation text_area_alloc;

				get_text_area_size(entry, &text_area_alloc);
				gdk_drawable_get_size(icon_info->window, &width, NULL);

				gtk_paint_flat_box(widget->style, icon_info->window,
								   GTK_WIDGET_STATE(widget), GTK_SHADOW_NONE,
								   NULL, widget, "entry_bg",
								   0, 0, width, text_area_alloc.height);

				draw_icon(widget, i);

				found = TRUE;
			}
		}

		if (!found)
			GTK_WIDGET_CLASS(parent_class)->expose_event(widget, event);
	}

	return FALSE;
}

static void
update_icon(GObject *obj, GParamSpec *param, SexyIconEntry *entry)
{
	if (param != NULL)
	{
		const char *name = g_param_spec_get_name(param);

		if (strcmp(name, "pixbuf")   && strcmp(name, "stock")  &&
			strcmp(name, "image")    && strcmp(name, "pixmap") &&
			strcmp(name, "icon_set") && strcmp(name, "pixbuf_animation"))
		{
			return;
		}
	}

	gtk_widget_queue_resize(GTK_WIDGET(entry));
}

static gint
sexy_icon_entry_enter_notify(GtkWidget *widget, GdkEventCrossing *event)
{
	SexyIconEntry *entry = SEXY_ICON_ENTRY(widget);
	int i;

	for (i = 0; i < MAX_ICONS; i++)
	{
		if (event->window == entry->priv->icons[i].window)
		{
			if (sexy_icon_entry_get_icon_highlight(entry, i))
			{
				entry->priv->icons[i].hovered = TRUE;

				update_icon(NULL, NULL, entry);

				break;
			}
		}
	}

	return FALSE;
}

static gint
sexy_icon_entry_leave_notify(GtkWidget *widget, GdkEventCrossing *event)
{
	SexyIconEntry *entry = SEXY_ICON_ENTRY(widget);
	int i;

	for (i = 0; i < MAX_ICONS; i++)
	{
		if (event->window == entry->priv->icons[i].window)
		{
			if (sexy_icon_entry_get_icon_highlight(entry, i))
			{
				entry->priv->icons[i].hovered = FALSE;

				update_icon(NULL, NULL, entry);

				break;
			}
		}
	}

	return FALSE;
}

static gint
sexy_icon_entry_button_press(GtkWidget *widget, GdkEventButton *event)
{
	SexyIconEntry *entry = SEXY_ICON_ENTRY(widget);
	int i;

	for (i = 0; i < MAX_ICONS; i++)
	{
		if (event->window == entry->priv->icons[i].window)
		{
			if (event->button == 1 &&
				sexy_icon_entry_get_icon_highlight(entry, i))
			{
				entry->priv->icons[i].hovered = FALSE;

				update_icon(NULL, NULL, entry);
			}

			g_signal_emit(entry, signals[ICON_PRESSED], 0, i, event->button);

			return TRUE;
		}
	}

	if (GTK_WIDGET_CLASS(parent_class)->button_press_event)
		return GTK_WIDGET_CLASS(parent_class)->button_press_event(widget,
																  event);

	return FALSE;
}

static gint
sexy_icon_entry_button_release(GtkWidget *widget, GdkEventButton *event)
{
	SexyIconEntry *entry = SEXY_ICON_ENTRY(widget);
	int i;

	for (i = 0; i < MAX_ICONS; i++)
	{
		GdkWindow *icon_window = entry->priv->icons[i].window;

		if (event->window == icon_window)
		{
			int width, height;
			gdk_drawable_get_size(icon_window, &width, &height);

			if (event->button == 1 &&
				sexy_icon_entry_get_icon_highlight(entry, i) &&
				event->x >= 0     && event->y >= 0 &&
				event->x <= width && event->y <= height)
			{
				entry->priv->icons[i].hovered = TRUE;

				update_icon(NULL, NULL, entry);
			}

			g_signal_emit(entry, signals[ICON_RELEASED], 0, i, event->button);

			return TRUE;
		}
	}

	if (GTK_WIDGET_CLASS(parent_class)->button_release_event)
		return GTK_WIDGET_CLASS(parent_class)->button_release_event(widget,
																	event);

	return FALSE;
}

/**
 * sexy_icon_entry_new
 *
 * Creates a new SexyIconEntry widget.
 *
 * Returns a new #SexyIconEntry.
 */
GtkWidget *
sexy_icon_entry_new(void)
{
	return GTK_WIDGET(g_object_new(SEXY_TYPE_ICON_ENTRY, NULL));
}

/**
 * sexy_icon_entry_set_icon
 * @entry: A #SexyIconEntry.
 * @position: Icon position.
 * @icon: A #GtkImage to set as the icon.
 *
 * Sets the icon shown in the entry
 */
void
sexy_icon_entry_set_icon(SexyIconEntry *entry, SexyIconEntryPosition icon_pos,
						 GtkImage *icon)
{
	SexyIconInfo *icon_info;

	g_return_if_fail(entry != NULL);
	g_return_if_fail(SEXY_IS_ICON_ENTRY(entry));
	g_return_if_fail(IS_VALID_ICON_ENTRY_POSITION(icon_pos));
	g_return_if_fail(icon == NULL || GTK_IS_IMAGE(icon));

	icon_info = &entry->priv->icons[icon_pos];

	if (icon == icon_info->icon)
		return;

	if (icon_pos == SEXY_ICON_ENTRY_SECONDARY &&
		entry->priv->icon_released_id != 0)
	{
		g_signal_handler_disconnect(entry, entry->priv->icon_released_id);
		entry->priv->icon_released_id = 0;
	}

	if (icon == NULL)
	{
		if (icon_info->icon != NULL)
		{
			gtk_widget_destroy(GTK_WIDGET(icon_info->icon));
			icon_info->icon = NULL;

			/*
			 * Explicitly check, as the pointer may become invalidated
			 * during destruction.
			 */
			if (icon_info->window != NULL && GDK_IS_WINDOW(icon_info->window))
				gdk_window_hide(icon_info->window);
		}
	}
	else
	{
		if (icon_info->window != NULL && icon_info->icon == NULL)
			gdk_window_show(icon_info->window);

		g_signal_connect(G_OBJECT(icon), "notify",
						 G_CALLBACK(update_icon), entry);

		icon_info->icon = icon;
		g_object_ref(icon);
	}

	update_icon(NULL, NULL, entry);
}

/**
 * sexy_icon_entry_set_icon_highlight
 * @entry: A #SexyIconEntry;
 * @position: Icon position.
 * @highlight: TRUE if the icon should highlight on mouse-over
 *
 * Determines whether the icon will highlight on mouse-over.
 */
void
sexy_icon_entry_set_icon_highlight(SexyIconEntry *entry,
								   SexyIconEntryPosition icon_pos,
								   gboolean highlight)
{
	SexyIconInfo *icon_info;

	g_return_if_fail(entry != NULL);
	g_return_if_fail(SEXY_IS_ICON_ENTRY(entry));
	g_return_if_fail(IS_VALID_ICON_ENTRY_POSITION(icon_pos));

	icon_info = &entry->priv->icons[icon_pos];

	if (icon_info->highlight == highlight)
		return;

	icon_info->highlight = highlight;
}

/**
 * sexy_icon_entry_get_icon
 * @entry: A #SexyIconEntry.
 * @position: Icon position.
 *
 * Retrieves the image used for the icon
 *
 * Returns: A #GtkImage.
 */
GtkImage *
sexy_icon_entry_get_icon(const SexyIconEntry *entry,
						 SexyIconEntryPosition icon_pos)
{
	g_return_val_if_fail(entry != NULL, NULL);
	g_return_val_if_fail(SEXY_IS_ICON_ENTRY(entry), NULL);
	g_return_val_if_fail(IS_VALID_ICON_ENTRY_POSITION(icon_pos), NULL);

	return entry->priv->icons[icon_pos].icon;
}

/**
 * sexy_icon_entry_get_icon_highlight
 * @entry: A #SexyIconEntry.
 * @position: Icon position.
 *
 * Retrieves whether entry will highlight the icon on mouseover.
 *
 * Returns: TRUE if icon highlights.
 */
gboolean
sexy_icon_entry_get_icon_highlight(const SexyIconEntry *entry,
								   SexyIconEntryPosition icon_pos)
{
	g_return_val_if_fail(entry != NULL, FALSE);
	g_return_val_if_fail(SEXY_IS_ICON_ENTRY(entry), FALSE);
	g_return_val_if_fail(IS_VALID_ICON_ENTRY_POSITION(icon_pos), FALSE);

	return entry->priv->icons[icon_pos].highlight;
}

static void
clear_button_clicked_cb(SexyIconEntry *icon_entry,
						SexyIconEntryPosition icon_pos,
						int button)
{
	if (icon_pos != SEXY_ICON_ENTRY_SECONDARY || button != 1)
		return;

	gtk_entry_set_text(GTK_ENTRY(icon_entry), "");
}

/**
 * sexy_icon_entry_add_clear_button
 * @icon_entry: A #SexyIconEntry.
 *
 * A convenience function to add a clear button to the end of the entry.
 * This is useful for search boxes.
 */
void
sexy_icon_entry_add_clear_button(SexyIconEntry *icon_entry)
{
	GtkWidget *icon;

	g_return_if_fail(icon_entry != NULL);
	g_return_if_fail(SEXY_IS_ICON_ENTRY(icon_entry));

	icon = gtk_image_new_from_stock(GTK_STOCK_CLEAR, GTK_ICON_SIZE_MENU);
	gtk_widget_show(icon);
	sexy_icon_entry_set_icon(SEXY_ICON_ENTRY(icon_entry),
							 SEXY_ICON_ENTRY_SECONDARY,
							 GTK_IMAGE(icon));
	sexy_icon_entry_set_icon_highlight(SEXY_ICON_ENTRY(icon_entry),
									   SEXY_ICON_ENTRY_SECONDARY, TRUE);

	if (icon_entry->priv->icon_released_id != 0)
	{
		g_signal_handler_disconnect(icon_entry,
									icon_entry->priv->icon_released_id);
	}

	icon_entry->priv->icon_released_id =
		g_signal_connect(G_OBJECT(icon_entry), "icon_released",
						 G_CALLBACK(clear_button_clicked_cb), NULL);
}
