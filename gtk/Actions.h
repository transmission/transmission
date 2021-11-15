/*
 * This file Copyright (C) 2007-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <gtkmm.h>

#include "Utils.h"

class Session;

#define WINDOW_ICON "transmission-main-window-icon"
#define TRAY_ICON "transmission-tray-icon"
#define NOTIFICATION_ICON "transmission-notification-icon"

Glib::RefPtr<Gio::SimpleActionGroup> gtr_actions_init(Glib::RefPtr<Gtk::Builder> const& builder, void* callback_user_data);
void gtr_actions_set_core(Glib::RefPtr<Session> const& core);
void gtr_actions_handler(Glib::ustring const& action_name, void* user_data);

void gtr_action_activate(Glib::ustring const& action_name);
void gtr_action_set_sensitive(Glib::ustring const& action_name, bool is_sensitive);
void gtr_action_set_toggled(Glib::ustring const& action_name, bool is_toggled);
Gtk::Widget* gtr_action_get_widget(Glib::ustring const& name);
Glib::RefPtr<Glib::Object> gtr_action_get_object(Glib::ustring const& name);

template<typename T>
inline T* gtr_action_get_widget(Glib::ustring const& name)
{
    return static_cast<T*>(gtr_action_get_widget(name));
}

template<typename T>
inline Glib::RefPtr<T> gtr_action_get_object(Glib::ustring const& name)
{
    return gtr_ptr_static_cast<T>(gtr_action_get_object(name));
}
