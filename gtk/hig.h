/******************************************************************************
 * $Id:$
 *
 * Copyright (c) 2007 Transmission authors and contributors
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

#ifndef __HIG_H__
#define __HIG_H__

#include <gtk/gtkwidget.h>

/**
*** utility code to make it slightly less painful to create
*** dialogs compliant with Gnome's Human Interface Guidelines
**/

GtkWidget* hig_workarea_create (void);

void
hig_workarea_add_section_divider (GtkWidget   * table,
                                  int         * row);

void
hig_workarea_add_section_title (GtkWidget   * table,
                                int         * row,
                                const char  * section_title);

void
hig_workarea_add_section_spacer (GtkWidget   * table,
                                 int           row,
                                 int           items_in_section);

void
hig_workarea_add_wide_control (GtkWidget   * table,
                               int         * row,
                               GtkWidget   * w);

GtkWidget*
hig_workarea_add_wide_checkbutton (GtkWidget   * table,
                                   int         * row,
                                   const char  * mnemonic_string,
                                   gboolean      is_active);

GtkWidget*
hig_workarea_add_label (GtkWidget   * table,
                        int           row,
                        const char  * mnemonic_string);

void
hig_workarea_add_label_w (GtkWidget   * table,
                          int           row,
                          GtkWidget   * label_widget);

void
hig_workarea_add_control (GtkWidget   * table,
                          int           row,
                          GtkWidget   * control);

GtkWidget*
hig_workarea_add_row (GtkWidget   * table,
                      int         * row,
                      const char  * mnemonic_string,
                      GtkWidget   * control,
                      GtkWidget   * mnemonic_or_null_if_control_is_mnemonic);

void
hig_workarea_add_row_w (GtkWidget   * table,
                        int         * row,
                        GtkWidget   * label,
                        GtkWidget   * control,
                        GtkWidget   * mnemonic_or_null_if_control_is_mnemonic);

void
hig_workarea_finish (GtkWidget   * table,
                     int         * row);

/**
***
**/

enum
{
    GUI_PAD_SMALL = 3,
    GUI_PAD = 6,
    GUI_PAD_BIG = 12,
    GUI_PAD_LARGE = 12
};

#endif /* __HIG_H__ */
