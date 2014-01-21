/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef GTR_ACTIONS_H
#define GTR_ACTIONS_H

#include <gtk/gtk.h>
#include "tr-core.h"

#define WINDOW_ICON        "transmission-main-window-icon"
#define TRAY_ICON          "transmission-tray-icon"
#define NOTIFICATION_ICON  "transmission-notification-icon"


void        gtr_actions_init (GtkUIManager * ui_manager, gpointer callback_user_data);
void        gtr_actions_set_core (TrCore * core);
void        gtr_actions_handler (const char * action_name, gpointer user_data);

void        gtr_action_activate    (const char * action_name);
void        gtr_action_set_sensitive (const char * action_name, gboolean is_sensitive);
void        gtr_action_set_toggled (const char * action_name, gboolean is_toggled);
void        gtr_action_set_important (const char * action_name, gboolean is_important);
GtkWidget*  gtr_action_get_widget  (const char * path);

#endif
