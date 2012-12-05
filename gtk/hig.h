/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2 (b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef GTR_HIG_H
#define GTR_HIG_H

#include <gtk/gtk.h>

/**
*** utility code for making dialog layout that follows the Gnome HIG.
*** see section 8.2.2, Visual Design > Window Layout > Dialogs.
**/

GtkWidget* hig_workarea_create (void);

void       hig_workarea_add_section_divider (GtkWidget * table,
                                             guint     *  row);

void       hig_workarea_add_section_title_widget (GtkWidget * t,
                                                  guint     * row,
                                                  GtkWidget * w);

void       hig_workarea_add_section_title (GtkWidget  *  table,
                                           guint      * row,
                                           const char * section_title);

void       hig_workarea_add_wide_tall_control (GtkWidget * table,
                                               guint     * row,
                                               GtkWidget * w);

void       hig_workarea_add_wide_control (GtkWidget * table,
                                          guint     * row,
                                          GtkWidget * w);

GtkWidget* hig_workarea_add_wide_checkbutton (GtkWidget  * table,
                                              guint      * row,
                                              const char * mnemonic_string,
                                              gboolean     is_active);

void       hig_workarea_add_label_w (GtkWidget * table,
                                     guint       row,
                                     GtkWidget * label_widget);

GtkWidget* hig_workarea_add_tall_row (GtkWidget  * table,
                                      guint      * row,
                                      const char * mnemonic_string,
                                      GtkWidget  * control,
                                      GtkWidget  * mnemonic_or_null_for_control);

GtkWidget* hig_workarea_add_row (GtkWidget  * table,
                                 guint      * row,
                                 const char * mnemonic_string,
                                 GtkWidget  * control,
                                 GtkWidget  * mnemonic_or_null_for_control);

void       hig_workarea_add_row_w (GtkWidget * table,
                                   guint     * row,
                                   GtkWidget * label,
                                   GtkWidget * control,
                                   GtkWidget * mnemonic_or_null_for_control);

enum
{
    GUI_PAD_SMALL = 3,
    GUI_PAD = 6,
    GUI_PAD_BIG = 12,
    GUI_PAD_LARGE = 12
};

#endif /* GTR_HIG_H */
