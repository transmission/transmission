// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <libtransmission/transmission.h>
#include <libtransmission/tr-macros.h>

#include <glibmm/objectbase.h>
#include <glibmm/refptr.h>
#include <glibmm/signalproxy.h>
#include <glibmm/ustring.h>
#include <gtkmm/builder.h>
#include <gtkmm/combobox.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/treeview.h>
#include <gtkmm/widget.h>
#include <gtkmm/window.h>

#include <fmt/core.h>
#include <fmt/format.h>

#include <cstddef>
#include <ctime>
#include <functional>
#include <list>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/types.h>

/***
****
***/

extern int const mem_K;
extern char const* const mem_K_str;
extern char const* const mem_M_str;
extern char const* const mem_G_str;
extern char const* const mem_T_str;

extern int const disk_K;
extern char const* const disk_K_str;
extern char const* const disk_M_str;
extern char const* const disk_G_str;
extern char const* const disk_T_str;

extern int const speed_K;
extern char const* const speed_K_str;
extern char const* const speed_M_str;
extern char const* const speed_G_str;
extern char const* const speed_T_str;

/***
****
***/

void gtr_message(std::string const& message);
void gtr_warning(std::string const& message);
void gtr_error(std::string const& message);

/***
****
***/

enum class GtrUnicode
{
    Up,
    Down,
    Inf,
    Bullet
};

Glib::ustring gtr_get_unicode_string(GtrUnicode uni);

/* return a human-readable string for the size given in bytes. */
Glib::ustring tr_strlsize(guint64 size_in_bytes);

/* return a human-readable string for the given ratio. */
Glib::ustring tr_strlratio(double ratio);

std::string tr_format_time_relative(time_t timestamp, time_t origin);
std::string tr_format_time_left(time_t timestamp);
std::string tr_format_time(time_t timestamp);

/***
****
***/

using TrObjectSignalNotifyCallback = void(Glib::RefPtr<Glib::ObjectBase const> const&);

Glib::SignalProxy<TrObjectSignalNotifyCallback> gtr_object_signal_notify(Glib::ObjectBase& object);
void gtr_object_notify_emit(Glib::ObjectBase& object);

void gtr_open_uri(Glib::ustring const& uri);

void gtr_open_file(std::string const& path);

Glib::ustring gtr_get_help_uri();

/***
****
***/

/* backwards-compatible wrapper around gtk_widget_set_visible() */
void gtr_widget_set_visible(Gtk::Widget& widget, bool is_visible);

Gtk::Window& gtr_widget_get_window(Gtk::Widget& widget);

void gtr_window_set_skip_taskbar_hint(Gtk::Window& window, bool value);
void gtr_window_set_urgency_hint(Gtk::Window& window, bool value);
void gtr_window_raise(Gtk::Window& window);

/***
****
***/

void gtr_priority_combo_init(Gtk::ComboBox& combo);

void gtr_combo_box_set_enum(Gtk::ComboBox& combo, std::vector<std::pair<Glib::ustring, int>> const& items);
int gtr_combo_box_get_active_enum(Gtk::ComboBox const& combo);
void gtr_combo_box_set_active_enum(Gtk::ComboBox& combo, int value);

/***
****
***/

void gtr_unrecognized_url_dialog(Gtk::Widget& parent, Glib::ustring const& url);

void gtr_add_torrent_error_dialog(Gtk::Widget& window_or_child, tr_torrent* duplicate_torrent, std::string const& filename);

/* pop up the context menu if a user right-clicks.
   if the row they right-click on isn't selected, select it. */
bool on_tree_view_button_pressed(
    Gtk::TreeView& view,
    double event_x,
    double event_y,
    bool context_menu_requested,
    std::function<void(double, double)> const& callback = {});

/* if the click didn't specify a row, clear the selection */
bool on_tree_view_button_released(Gtk::TreeView& view, double event_x, double event_y);

using TrGdkModifierType = IF_GTKMM4(Gdk::ModifierType, guint);

void setup_tree_view_button_event_handling(
    Gtk::TreeView& view,
    std::function<bool(guint, TrGdkModifierType, double, double, bool)> const& press_callback,
    std::function<bool(double, double)> const& release_callback);

/* move a file to the trashcan if GIO is available; otherwise, delete it */
bool gtr_file_trash_or_remove(std::string const& filename, tr_error** error);

void gtr_paste_clipboard_url_into_entry(Gtk::Entry& entry);

/* Only call gtk_label_set_text() if the new text differs from the old.
 * This prevents the label from having to recalculate its size
 * and prevents selected text in the label from being deselected */
void gtr_label_set_text(Gtk::Label& lb, Glib::ustring const& text);

template<typename T>
inline T gtr_str_strip(T const& text)
{
    auto const new_begin = text.find_first_not_of("\t\n\v\f\r ");
    auto const new_end = text.find_last_not_of("\t\n\v\f\r ");
    return new_begin == T::npos ? T() : text.substr(new_begin, new_end == T::npos ? new_end : new_end - new_begin + 1);
}

template<typename T>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
constexpr int gtr_compare_generic(T const& lhs, T const& rhs)
{
    using std::rel_ops::operator>;

    if (lhs < rhs)
    {
        return -1;
    }

    if (lhs > rhs)
    {
        return 1;
    }

    return 0;
}

std::string gtr_get_full_resource_path(std::string const& rel_path);

/***
****
***/

class Session;

extern size_t const max_recent_dirs;
std::list<std::string> gtr_get_recent_dirs(std::string const& pref);
void gtr_save_recent_dir(std::string const& pref, Glib::RefPtr<Session> const& core, std::string const& dir);

template<typename T, typename U>
inline Glib::RefPtr<T> gtr_ptr_static_cast(Glib::RefPtr<U> const& ptr)
{
#if GLIBMM_CHECK_VERSION(2, 68, 0)
    return std::static_pointer_cast<T>(ptr);
#else
    return Glib::RefPtr<T>::cast_static(ptr);
#endif
}

template<typename T, typename U>
inline Glib::RefPtr<T> gtr_ptr_dynamic_cast(Glib::RefPtr<U> const& ptr)
{
#if GLIBMM_CHECK_VERSION(2, 68, 0)
    return std::dynamic_pointer_cast<T>(ptr);
#else
    return Glib::RefPtr<T>::cast_dynamic(ptr);
#endif
}

template<>
struct std::hash<Glib::ustring>
{
    std::size_t operator()(Glib::ustring const& s) const
    {
        return std::hash<std::string>()(s.raw());
    }
};

template<>
struct fmt::formatter<Glib::ustring> : formatter<std::string>
{
    template<typename FormatContext>
    auto format(Glib::ustring const& ustr, FormatContext& ctx) const
    {
        return formatter<std::string>::format(ustr.raw(), ctx);
    }
};

template<typename T>
T* gtr_get_widget(Glib::RefPtr<Gtk::Builder> const& builder, Glib::ustring const& name)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    return builder->get_widget<T>(name);
#else
    T* widget = nullptr;
    builder->get_widget(name, widget);
    return widget;
#endif
}

template<typename T, typename... ArgTs>
T* gtr_get_widget_derived(Glib::RefPtr<Gtk::Builder> const& builder, Glib::ustring const& name, ArgTs&&... args)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    return Gtk::Builder::get_widget_derived<T>(builder, name, std::forward<ArgTs>(args)...);
#else
    T* widget = nullptr;
    builder->get_widget_derived(name, widget, std::forward<ArgTs>(args)...);
    return widget;
#endif
}

template<typename F>
void gtr_window_on_close(Gtk::Window& widget, F&& callback)
{
    auto bool_callback = [callback]() mutable -> bool
    {
        if constexpr (std::is_same_v<void, std::invoke_result_t<decltype(callback)>>)
        {
            callback();
            return false;
        }
        else
        {
            return callback();
        }
    };

#if GTKMM_CHECK_VERSION(4, 0, 0)
    widget.signal_close_request().connect(bool_callback, false);
#else
    widget.signal_delete_event().connect(sigc::hide<0>(bool_callback), false);
#endif
}
