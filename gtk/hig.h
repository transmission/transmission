/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <gtk/gtk.h>

/**
*** utility code for making dialog layout that follows the Gnome HIG.
*** see section 8.2.2, Visual Design > Window Layout > Dialogs.
**/

GtkWidget* hig_workarea_create(void);

void hig_workarea_add_section_divider(GtkWidget* table, guint* row);

void hig_workarea_add_section_title_widget(GtkWidget* t, guint* row, GtkWidget* w);

void hig_workarea_add_section_title(GtkWidget* table, guint* row, char const* section_title);

void hig_workarea_add_wide_tall_control(GtkWidget* table, guint* row, GtkWidget* w);

void hig_workarea_add_wide_control(GtkWidget* table, guint* row, GtkWidget* w);

GtkWidget* hig_workarea_add_wide_checkbutton(GtkWidget* table, guint* row, char const* mnemonic_string, gboolean is_active);

void hig_workarea_add_label_w(GtkWidget* table, guint row, GtkWidget* label_widget);

GtkWidget* hig_workarea_add_tall_row(GtkWidget* table, guint* row, char const* mnemonic_string, GtkWidget* control,
    GtkWidget* mnemonic_or_null_for_control);

GtkWidget* hig_workarea_add_row(GtkWidget* table, guint* row, char const* mnemonic_string, GtkWidget* control,
    GtkWidget* mnemonic_or_null_for_control);

void hig_workarea_add_row_w(GtkWidget* table, guint* row, GtkWidget* label, GtkWidget* control,
    GtkWidget* mnemonic_or_null_for_control);

enum
{
    GUI_PAD_SMALL = 3,
    GUI_PAD = 6,
    GUI_PAD_BIG = 12,
    GUI_PAD_LARGE = 12
};
