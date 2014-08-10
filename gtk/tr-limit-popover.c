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

#include <stdlib.h>
#include <string.h>

#include "tr-limit-popover.h"

struct _TrLimitPopoverPrivate
{
    GtkWidget* download_limit_entry;
    GtkWidget* upload_limit_entry;
    GtkWidget* ratio_limit_entry;

    GtkWidget* upload_limit_check;
    GtkWidget* download_limit_check;
    GtkWidget* ratio_limit_check;
};

/* Signals */
enum
{
    SPEED_LIMIT_UP_CHANGED,
    SPEED_LIMIT_DOWN_CHANGED,
    RATIO_LIMIT_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE(TrLimitPopover, tr_limit_popover, GTK_TYPE_GRID)

static void tr_limit_popover_class_init(TrLimitPopoverClass* klass)
{
    signals[SPEED_LIMIT_UP_CHANGED] = g_signal_new("speed-limit-up", G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET(TrLimitPopoverClass, speed_limit_up), NULL, NULL,
        g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SPEED_LIMIT_DOWN_CHANGED] = g_signal_new("speed-limit-down", G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, G_STRUCT_OFFSET(TrLimitPopoverClass, speed_limit_down), NULL, NULL,
        g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

    signals[RATIO_LIMIT_CHANGED] = g_signal_new("ratio-limit", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
        G_STRUCT_OFFSET(TrLimitPopoverClass, ratio_limit), NULL, NULL, g_cclosure_marshal_VOID__DOUBLE, G_TYPE_NONE, 1,
        G_TYPE_DOUBLE);
}

static void on_entry_activate(GtkWidget* entry, gpointer user_data)
{
    char const* text;
    int limit = 0;
    double ratio = 0.0f;

    TrLimitPopover* popover;
    TrLimitPopoverPrivate* priv;

    popover = (TrLimitPopover*)user_data;
    priv = (TrLimitPopoverPrivate*)popover->priv;

    text = gtk_entry_get_text(GTK_ENTRY(entry));

    if (entry == priv->download_limit_entry)
    {
        limit = atoi(text);
        g_signal_emit(popover, signals[SPEED_LIMIT_DOWN_CHANGED], 0, limit);
    }
    else if (entry == priv->upload_limit_entry)
    {
        limit = atoi(text);
        g_signal_emit(popover, signals[SPEED_LIMIT_UP_CHANGED], 0, limit);
    }
    else if (entry == priv->ratio_limit_entry)
    {
        ratio = atof(text);
        g_signal_emit(popover, signals[RATIO_LIMIT_CHANGED], 0, ratio);
    }
}

static void tr_limit_popover_init(TrLimitPopover* popover)
{
    TrLimitPopoverPrivate* priv;
    GtkWidget* w;

    popover->priv = tr_limit_popover_get_instance_private(popover);
    priv = popover->priv;

    gtk_container_set_border_width(GTK_CONTAINER(popover), 10);
    gtk_grid_set_column_spacing(GTK_GRID(popover), 10);
    gtk_grid_set_row_spacing(GTK_GRID(popover), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(popover), FALSE);

    w = priv->download_limit_check = gtk_check_button_new_with_label("Limit Download");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(w), "win.speed-limit-down-enabled");
    gtk_grid_attach(GTK_GRID(popover), w, 1, 1, 1, 1);

    w = priv->download_limit_entry = gtk_spin_button_new_with_range(0, INT_MAX, 5);
    gtk_entry_set_width_chars(GTK_ENTRY(w), 1);
    g_signal_connect(GTK_ENTRY(w), "activate", G_CALLBACK(on_entry_activate), popover);
    gtk_grid_attach(GTK_GRID(popover), w, 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(popover), gtk_label_new("KB/s"), 3, 1, 1, 1);

    w = priv->upload_limit_check = gtk_check_button_new_with_label("Limit Upload");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(w), "win.speed-limit-up-enabled");
    gtk_grid_attach(GTK_GRID(popover), w, 1, 2, 1, 1);

    w = priv->upload_limit_entry = gtk_spin_button_new_with_range(0, INT_MAX, 5);
    gtk_entry_set_width_chars(GTK_ENTRY(w), 1);
    g_signal_connect(GTK_ENTRY(w), "activate", G_CALLBACK(on_entry_activate), popover);
    gtk_grid_attach(GTK_GRID(popover), w, 2, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(popover), gtk_label_new("KB/s"), 3, 2, 1, 1);

    w = priv->ratio_limit_check = gtk_check_button_new_with_label("Stop seeding at ratio");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(w), "win.ratio-limit-enabled");
    gtk_grid_attach(GTK_GRID(popover), w, 1, 3, 1, 1);

    w = priv->ratio_limit_entry = gtk_spin_button_new_with_range(0, 1000, .05);
    gtk_entry_set_width_chars(GTK_ENTRY(w), 1);
    g_signal_connect(GTK_ENTRY(w), "activate", G_CALLBACK(on_entry_activate), popover);
    gtk_grid_attach(GTK_GRID(popover), w, 2, 3, 1, 1);
}

TrLimitPopover* tr_limit_popover_new()
{
	return g_object_new(TR_TYPE_LIMIT_POPOVER, NULL);
}

// - Speed limit up -

int tr_limit_popover_get_speed_limit_up(TrLimitPopover* popover)
{
	GtkSpinButton* entry = GTK_SPIN_BUTTON(popover->priv->upload_limit_entry);
	return gtk_spin_button_get_value_as_int(entry);
}

void tr_limit_popover_set_speed_limit_up(TrLimitPopover* popover, int value)
{
	GtkSpinButton* entry = GTK_SPIN_BUTTON(popover->priv->upload_limit_entry);
	gtk_spin_button_set_value(entry, value);
}

void tr_limit_popover_set_speed_limit_up_enabled(TrLimitPopover* popover, gboolean enabled)
{
	GtkToggleButton* check = GTK_TOGGLE_BUTTON(popover->priv->upload_limit_check);
	gtk_toggle_button_set_active(check, enabled);
}

// - Speed limit down -

int tr_limit_popover_get_speed_limit_down(TrLimitPopover* popover)
{
	GtkSpinButton* entry = GTK_SPIN_BUTTON(popover->priv->download_limit_entry);
	return gtk_spin_button_get_value_as_int(entry);
}

void tr_limit_popover_set_speed_limit_down(TrLimitPopover* popover, int value)
{
	GtkSpinButton* entry = GTK_SPIN_BUTTON(popover->priv->download_limit_entry);
	gtk_spin_button_set_value(entry, value);
}

void tr_limit_popover_set_speed_limit_down_enabled(TrLimitPopover* popover, gboolean enabled)
{
	GtkToggleButton* check = GTK_TOGGLE_BUTTON(popover->priv->download_limit_check);
	gtk_toggle_button_set_active(check, enabled);
}

// - Ratio limit -

double tr_limit_popover_get_ratio_limit(TrLimitPopover* popover)
{
	GtkSpinButton* entry = GTK_SPIN_BUTTON(popover->priv->ratio_limit_entry);
	return gtk_spin_button_get_value(entry);
}

void tr_limit_popover_set_ratio_limit(TrLimitPopover* popover, double value)
{
	GtkSpinButton* entry = GTK_SPIN_BUTTON(popover->priv->ratio_limit_entry);
	gtk_spin_button_set_value(entry, value);
}

void tr_limit_popover_set_ratio_limit_enabled(TrLimitPopover* popover, gboolean enabled)
{
	GtkToggleButton* check = GTK_TOGGLE_BUTTON(popover->priv->ratio_limit_check);
	gtk_toggle_button_set_active(check, enabled);
}

/* ex:set ts=4 noet: */
