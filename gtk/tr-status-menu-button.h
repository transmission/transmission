/*
 * tr-status-menu-button.h
 * This file is part of gedit
 *
 * Copyright (C) 2008 - Jesse van den Kieboom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define TR_TYPE_STATUS_MENU_BUTTON (tr_status_menu_button_get_type())
#define TR_STATUS_MENU_BUTTON(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TR_TYPE_STATUS_MENU_BUTTON, TrStatusMenuButton))
#define TR_STATUS_MENU_BUTTON_CONST(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TR_TYPE_STATUS_MENU_BUTTON, TrStatusMenuButton const))
#define TR_STATUS_MENU_BUTTON_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), TR_TYPE_STATUS_MENU_BUTTON, TrStatusMenuButtonClass))
#define TR_IS_STATUS_MENU_BUTTON(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), TR_TYPE_STATUS_MENU_BUTTON))
#define TR_IS_STATUS_MENU_BUTTON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), TR_TYPE_STATUS_MENU_BUTTON))
#define TR_STATUS_MENU_BUTTON_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), TR_TYPE_STATUS_MENU_BUTTON, TrStatusMenuButtonClass))

typedef struct _TrStatusMenuButton TrStatusMenuButton;
typedef struct _TrStatusMenuButtonPrivate TrStatusMenuButtonPrivate;
typedef struct _TrStatusMenuButtonClass TrStatusMenuButtonClass;
typedef struct _TrStatusMenuButtonClassPrivate TrStatusMenuButtonClassPrivate;

struct _TrStatusMenuButton
{
    GtkMenuButton parent;

    TrStatusMenuButtonPrivate* priv;
};

struct _TrStatusMenuButtonClass
{
    GtkMenuButtonClass parent_class;

    TrStatusMenuButtonClassPrivate* priv;
};

GType tr_status_menu_button_get_type(void) G_GNUC_CONST;

GtkWidget* tr_status_menu_button_new(void);

void tr_status_menu_button_set_label(TrStatusMenuButton* button, gchar const* label);

gchar const* tr_status_menu_button_get_label(TrStatusMenuButton* button);

G_END_DECLS

/* ex:set ts=8 noet: */
