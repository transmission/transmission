// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <gtkmm.h>

#include "Utils.h"

class Session;

Glib::RefPtr<Gio::SimpleActionGroup> gtr_actions_init(Glib::RefPtr<Gtk::Builder> const& builder, gpointer callback_user_data);
void gtr_actions_set_core(Glib::RefPtr<Session> const& core);
void gtr_actions_handler(Glib::ustring const& action_name, gpointer user_data);

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
