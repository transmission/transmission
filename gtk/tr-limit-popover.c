/*
 * tr-limit-popover.c
 * This file is part of Transmission
 *
 * Copyright (C) 2014 - Derek Willian Stavis
 *
 * transmission is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transmission is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with transmission. If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "tr-limit-popover.h"

struct _TrLimitPopoverPrivate
{
    GtkWidget* download_limit_entry;
    GtkWidget* upload_limit_entry;
    GtkWidget* seed_limit_entry;
};

/* Signals */
enum
{
    LIMITS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE(TrLimitPopover, tr_limit_popover, GTK_TYPE_GRID)

static void tr_limit_popover_class_init(TrLimitPopoverClass* klass)
{
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);

    /*
    signals[LIMITS_CHANGED] = g_signal_new("limits-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
        G_STRUCT_OFFSET(TrLimitPopoverClass, limits_changed), NULL, NULL, g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
        GTK_SOURCE_TYPE_LANGUAGE);
    */
}

static void tr_limit_popover_init(TrLimitPopover* popover)
{
    TrLimitPopoverPrivate* priv;
    GtkStyleContext* context;
    GtkWidget* w;

    popover->priv = tr_limit_popover_get_instance_private(popover);
    priv = popover->priv;

    gtk_container_set_border_width(GTK_CONTAINER(popover), 10);
    gtk_grid_set_column_spacing(GTK_GRID(popover), 10);
    gtk_grid_set_row_spacing(GTK_GRID(popover), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(popover), FALSE);

    w = gtk_check_button_new_with_label("Limit Download");
    gtk_grid_attach(GTK_GRID(popover), w, 1, 1, 1, 1);

    priv->download_limit_entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(priv->download_limit_entry), 1);
    gtk_grid_attach(GTK_GRID(popover), priv->download_limit_entry, 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(popover), gtk_label_new("KB/s"), 3, 1, 1, 1);

    w = gtk_check_button_new_with_label("Limit Upload");
    gtk_label_set_justify(GTK_LABEL(w), GTK_JUSTIFY_RIGHT);
    gtk_grid_attach(GTK_GRID(popover), w, 1, 2, 1, 1);

    priv->upload_limit_entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(priv->upload_limit_entry), 1);
    gtk_grid_attach(GTK_GRID(popover), priv->upload_limit_entry, 2, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(popover), gtk_label_new("KB/s"), 3, 2, 1, 1);

    w = gtk_check_button_new_with_label("Stop seeding at ratio");
    gtk_label_set_justify(GTK_LABEL(w), GTK_JUSTIFY_RIGHT);
    gtk_grid_attach(GTK_GRID(popover), w, 1, 3, 1, 1);

    priv->seed_limit_entry = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(priv->seed_limit_entry), 1);
    gtk_grid_attach(GTK_GRID(popover), priv->seed_limit_entry, 2, 3, 1, 1);

    w = gtk_button_new_with_label("Apply Limits");
    gtk_label_set_justify(GTK_LABEL(w), GTK_JUSTIFY_RIGHT);
    context = gtk_widget_get_style_context(w);

    gtk_style_context_add_class(context, GTK_STYLE_CLASS_SUGGESTED_ACTION);
    gtk_style_context_add_class(context, GTK_STYLE_CLASS_DEFAULT);

    gtk_grid_attach(GTK_GRID(popover), w, 1, 4, 3, 1);
}

TrLimitPopover* tr_limit_popover_new()
{
	return g_object_new(TR_TYPE_LIMIT_POPOVER, NULL);
}

/* ex:set ts=4 noet: */
