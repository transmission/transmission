// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"
#include "Utils.h"

#include <giomm/listmodel.h>
#include <giomm/menumodel.h>
#include <giomm/simpleactiongroup.h>
#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/builder.h>

inline auto const GTR_KEY_copy_magnet_link_to_clipboard = Glib::ustring{ "copy_magnet_link_to_clipboard" };
inline auto const GTR_KEY_delete_torrent = Glib::ustring{ "delete_torrent" };
inline auto const GTR_KEY_deselect_all = Glib::ustring{ "deselect_all" };
inline auto const GTR_KEY_donate = Glib::ustring{ "donate" };
inline auto const GTR_KEY_edit_preferences = Glib::ustring{ "edit_preferences" };
inline auto const GTR_KEY_help = Glib::ustring{ "help" };
inline auto const GTR_KEY_main_window_popup = Glib::ustring{ "main_window_popup" };
inline auto const GTR_KEY_new_torrent = Glib::ustring{ "new_torrent" };
inline auto const GTR_KEY_open_torrent = Glib::ustring{ "open_torrent" };
inline auto const GTR_KEY_open_torrent_folder = Glib::ustring{ "open_torrent_folder" };
inline auto const GTR_KEY_open_torrent_from_url = Glib::ustring{ "open_torrent_from_url" };
inline auto const GTR_KEY_pause_all_torrents = Glib::ustring{ "pause_all_torrents" };
inline auto const GTR_KEY_present_main_window = Glib::ustring{ "present_main_window" };
inline auto const GTR_KEY_queue_move_bottom = Glib::ustring{ "queue_move_bottom" };
inline auto const GTR_KEY_queue_move_down = Glib::ustring{ "queue_move_down" };
inline auto const GTR_KEY_queue_move_top = Glib::ustring{ "queue_move_top" };
inline auto const GTR_KEY_queue_move_up = Glib::ustring{ "queue_move_up" };
inline auto const GTR_KEY_quit = Glib::ustring{ "quit" };
inline auto const GTR_KEY_relocate_torrent = Glib::ustring{ "relocate_torrent" };
inline auto const GTR_KEY_remove_torrent = Glib::ustring{ "remove_torrent" };
inline auto const GTR_KEY_select_all = Glib::ustring{ "select_all" };
inline auto const GTR_KEY_show_about_dialog = Glib::ustring{ "show_about_dialog" };
inline auto const GTR_KEY_show_stats = Glib::ustring{ "show_stats" };
inline auto const GTR_KEY_show_torrent_properties = Glib::ustring{ "show_torrent_properties" };
inline auto const GTR_KEY_sort_torrents = Glib::ustring{ "sort_torrents" };
inline auto const GTR_KEY_start_all_torrents = Glib::ustring{ "start_all_torrents" };
inline auto const GTR_KEY_toggle_main_window = Glib::ustring{ "toggle_main_window" };
inline auto const GTR_KEY_toggle_message_log = Glib::ustring{ "toggle_message_log" };
inline auto const GTR_KEY_torrent_reannounce = Glib::ustring{ "torrent_reannounce" };
inline auto const GTR_KEY_torrent_start = Glib::ustring{ "torrent_start" };
inline auto const GTR_KEY_torrent_start_now = Glib::ustring{ "torrent_start_now" };
inline auto const GTR_KEY_torrent_stop = Glib::ustring{ "torrent_stop" };
inline auto const GTR_KEY_torrent_verify = Glib::ustring{ "torrent_verify" };

class Session;

Glib::RefPtr<Gio::SimpleActionGroup> gtr_actions_init(Glib::RefPtr<Gtk::Builder> const& builder, gpointer callback_user_data);
void gtr_actions_set_core(Glib::RefPtr<Session> const& core);
void gtr_actions_handler(Glib::ustring const& action_name, gpointer user_data);

void gtr_action_activate(Glib::ustring const& action_name);
void gtr_action_set_sensitive(Glib::ustring const& action_name, bool is_sensitive);
void gtr_action_set_toggled(Glib::ustring const& action_name, bool is_toggled);
Glib::RefPtr<Glib::Object> gtr_action_get_object(Glib::ustring const& name);

#if GTKMM_CHECK_VERSION(4, 0, 0)
Glib::RefPtr<Gio::ListModel> gtr_shortcuts_get_from_menu(Glib::RefPtr<Gio::MenuModel> const& menu);
#endif

template<typename T>
inline Glib::RefPtr<T> gtr_action_get_object(Glib::ustring const& name)
{
    return gtr_ptr_static_cast<T>(gtr_action_get_object(name));
}
