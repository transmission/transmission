/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <gtk/gtk.h>
#include "hig.h"

GtkWidget*
hig_workarea_create (void)
{
    GtkWidget * grid = gtk_grid_new ();

    gtk_container_set_border_width (GTK_CONTAINER (grid), GUI_PAD_BIG);
    gtk_grid_set_row_spacing (GTK_GRID (grid), GUI_PAD);
    gtk_grid_set_column_spacing (GTK_GRID (grid), GUI_PAD_BIG);

    return grid;
}

void
hig_workarea_add_section_divider (GtkWidget * t, guint * row)
{
    GtkWidget * w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);

    gtk_widget_set_size_request (w, 0u, 6u);
    gtk_grid_attach (GTK_GRID (t), w, 0, *row, 2, 1);
    ++ * row;
}

void
hig_workarea_add_section_title_widget (GtkWidget * t, guint * row, GtkWidget * w)
{
    gtk_widget_set_hexpand (w, TRUE);
    gtk_grid_attach (GTK_GRID (t), w, 0, *row, 2, 1);
    ++ * row;
}

void
hig_workarea_add_section_title (GtkWidget *  t, guint * row, const char * section_title)
{
    char buf[512];
    GtkWidget * l;

    g_snprintf (buf, sizeof (buf), "<b>%s</b>", section_title);
    l = gtk_label_new (buf);
    gtk_misc_set_alignment (GTK_MISC (l), 0.0f, 0.5f);
    gtk_label_set_use_markup (GTK_LABEL (l), TRUE);
    hig_workarea_add_section_title_widget (t, row, l);
}

void
hig_workarea_add_wide_control (GtkWidget * t, guint * row, GtkWidget * w)
{
    gtk_widget_set_hexpand (w, TRUE);
    gtk_widget_set_margin_left (w, 18);
    gtk_grid_attach (GTK_GRID (t), w, 0, *row, 2, 1);
    ++ * row;
}
void
hig_workarea_add_wide_tall_control (GtkWidget * t, guint * row, GtkWidget * w)
{
    gtk_widget_set_hexpand (w, TRUE);
    gtk_widget_set_vexpand (w, TRUE);
    hig_workarea_add_wide_control (t, row, w);
}

GtkWidget *
hig_workarea_add_wide_checkbutton (GtkWidget  * t,
                                   guint      * row,
                                   const char * mnemonic_string,
                                   gboolean     is_active)
{
    GtkWidget * w = gtk_check_button_new_with_mnemonic (mnemonic_string);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), is_active);
    hig_workarea_add_wide_control (t, row, w);
    return w;
}

void
hig_workarea_add_label_w (GtkWidget * t, guint row, GtkWidget * w)
{
    gtk_widget_set_margin_left (w, 18);
    if (GTK_IS_MISC (w))
        gtk_misc_set_alignment (GTK_MISC (w), 0.0f, 0.5f);
    if (GTK_IS_LABEL (w))
        gtk_label_set_use_markup (GTK_LABEL (w), TRUE);
    gtk_grid_attach (GTK_GRID (t), w, 0, row, 1, 1);
}

static void
hig_workarea_add_tall_control (GtkWidget * t, guint row, GtkWidget * control)
{
    if (GTK_IS_MISC (control))
        gtk_misc_set_alignment (GTK_MISC (control), 0.0f, 0.5f);

    g_object_set (control, "expand", TRUE, NULL);
    gtk_grid_attach (GTK_GRID (t), control, 1, row, 1, 1);
}

static void
hig_workarea_add_control (GtkWidget * t, guint row, GtkWidget * control)
{
    if (GTK_IS_MISC (control))
        gtk_misc_set_alignment (GTK_MISC (control), 0.0f, 0.5f);

    gtk_widget_set_hexpand (control, TRUE);
    gtk_grid_attach (GTK_GRID (t), control, 1, row, 1, 1);
}

void
hig_workarea_add_row_w (GtkWidget * t,
                        guint     * row,
                        GtkWidget * label,
                        GtkWidget * control,
                        GtkWidget * mnemonic)
{
    hig_workarea_add_label_w (t, *row, label);
    hig_workarea_add_control (t, *row, control);
    if (GTK_IS_LABEL (label))
        gtk_label_set_mnemonic_widget (GTK_LABEL (label),
                                       mnemonic ? mnemonic : control);
    ++ * row;
}

GtkWidget*
hig_workarea_add_row (GtkWidget  * t,
                      guint      * row,
                      const char * mnemonic_string,
                      GtkWidget  * control,
                      GtkWidget  * mnemonic)
{
    GtkWidget * l = gtk_label_new_with_mnemonic (mnemonic_string);

    hig_workarea_add_row_w (t, row, l, control, mnemonic);
    return l;
}

GtkWidget*
hig_workarea_add_tall_row (GtkWidget  * table,
                           guint      * row,
                           const char * mnemonic_string,
                           GtkWidget  * control,
                           GtkWidget  * mnemonic)
{
    GtkWidget * l = gtk_label_new_with_mnemonic (mnemonic_string);
    GtkWidget * h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget * v = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start (GTK_BOX (h), l, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (v), h, FALSE, FALSE, GUI_PAD_SMALL);

    hig_workarea_add_label_w (table, *row, v);
    hig_workarea_add_tall_control (table, *row, control);

    if (GTK_IS_LABEL (l))
        gtk_label_set_mnemonic_widget (GTK_LABEL (l),
                                       mnemonic ? mnemonic : control);

    ++ * row;
    return l;
}
