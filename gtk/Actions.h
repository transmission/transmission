/*
 * This file Copyright (C) 2007-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <gtkmm.h>

class Session;

#define WINDOW_ICON "transmission-main-window-icon"
#define TRAY_ICON "transmission-tray-icon"
#define NOTIFICATION_ICON "transmission-notification-icon"

void gtr_actions_init(Glib::RefPtr<Gtk::UIManager> const& ui_manager, void* callback_user_data);
void gtr_actions_set_core(Glib::RefPtr<Session> const& core);
void gtr_actions_handler(Glib::ustring const& action_name, void* user_data);

void gtr_action_activate(Glib::ustring const& action_name);
void gtr_action_set_sensitive(Glib::ustring const& action_name, bool is_sensitive);
void gtr_action_set_toggled(Glib::ustring const& action_name, bool is_toggled);
void gtr_action_set_important(Glib::ustring const& action_name, bool is_important);
Gtk::Widget* gtr_action_get_widget(Glib::ustring const& path);

template<typename T>
inline T* gtr_action_get_widget(Glib::ustring const& path)
{
    return static_cast<T*>(gtr_action_get_widget(path));
}
