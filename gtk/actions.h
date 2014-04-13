/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <gtk/gtk.h>
#include "tr-core.h"

#define WINDOW_ICON "transmission-main-window-icon"
#define TRAY_ICON "transmission-tray-icon"
#define NOTIFICATION_ICON "transmission-notification-icon"

void gtr_actions_init(GtkApplication* app, gpointer callback_user_data);
void gtr_actions_add_to_map(GActionMap* map, gpointer callback_user_data);
void gtr_actions_set_core(TrCore* core);
void gtr_actions_handler(char const* action_name, gpointer user_data);

void gtr_action_activate(char const* action_name);
void gtr_action_set_sensitive(char const* action_name, gboolean is_sensitive);
void gtr_action_set_toggled(char const* action_name, gboolean is_toggled);
GMenuModel* gtr_action_get_menu_model(char const* id);
