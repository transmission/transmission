// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef>
#include <ctime>
#include <functional>
#include <list>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/types.h>

#include <glibmm.h>
#include <gtkmm.h>

#include <fmt/core.h>
#include <fmt/format.h>

#include <libtransmission/transmission.h>
#include <libtransmission/tr-macros.h>

#include "Session.h"

/***
****
***/

#ifndef GTKMM_CHECK_VERSION
#define GTKMM_CHECK_VERSION(major, minor, micro) \
    (GTKMM_MAJOR_VERSION > (major) || (GTKMM_MAJOR_VERSION == (major) && GTKMM_MINOR_VERSION > (minor)) || \
     (GTKMM_MAJOR_VERSION == (major) && GTKMM_MINOR_VERSION == (minor) && GTKMM_MICRO_VERSION >= (micro)))
#endif

#ifndef GLIBMM_CHECK_VERSION
#define GLIBMM_CHECK_VERSION(major, minor, micro) \
    (GLIBMM_MAJOR_VERSION > (major) || (GLIBMM_MAJOR_VERSION == (major) && GLIBMM_MINOR_VERSION > (minor)) || \
     (GLIBMM_MAJOR_VERSION == (major) && GLIBMM_MINOR_VERSION == (minor) && GLIBMM_MICRO_VERSION >= (micro)))
#endif

#ifndef PANGOMM_CHECK_VERSION
#define PANGOMM_CHECK_VERSION(major, minor, micro) \
    (PANGOMM_MAJOR_VERSION > (major) || (PANGOMM_MAJOR_VERSION == (major) && PANGOMM_MINOR_VERSION > (minor)) || \
     (PANGOMM_MAJOR_VERSION == (major) && PANGOMM_MINOR_VERSION == (minor) && PANGOMM_MICRO_VERSION >= (micro)))
#endif

#if GTKMM_CHECK_VERSION(4, 0, 0)
#define IF_GTKMM4(ThenValue, ElseValue) ThenValue
#else
#define IF_GTKMM4(ThenValue, ElseValue) ElseValue
#endif

#if GLIBMM_CHECK_VERSION(2, 68, 0)
#define IF_GLIBMM2_68(ThenValue, ElseValue) ThenValue
#else
#define IF_GLIBMM2_68(ThenValue, ElseValue) ElseValue
#endif

#if PANGOMM_CHECK_VERSION(2, 48, 0)
#define IF_PANGOMM2_48(ThenValue, ElseValue) ThenValue
#else
#define IF_PANGOMM2_48(ThenValue, ElseValue) ElseValue
#endif

#define TR_GTK_ALIGN(Code) IF_GTKMM4(Gtk::Align::Code, Gtk::ALIGN_##Code)
#define TR_GTK_BUTTONS_TYPE(Code) IF_GTKMM4(Gtk::ButtonsType::Code, Gtk::BUTTONS_##Code)
#define TR_GTK_CELL_RENDERER_STATE(Code) IF_GTKMM4(Gtk::CellRendererState::Code, Gtk::CELL_RENDERER_##Code)
#define TR_GTK_FILE_CHOOSER_ACTION(Code) IF_GTKMM4(Gtk::FileChooser::Action::Code, Gtk::FILE_CHOOSER_ACTION_##Code)
#define TR_GTK_MESSAGE_TYPE(Code) IF_GTKMM4(Gtk::MessageType::Code, Gtk::MESSAGE_##Code)
#define TR_GTK_ORIENTATION(Code) IF_GTKMM4(Gtk::Orientation::Code, Gtk::ORIENTATION_##Code)
#define TR_GTK_POLICY_TYPE(Code) IF_GTKMM4(Gtk::PolicyType::Code, Gtk::POLICY_##Code)
#define TR_GTK_RESPONSE_TYPE(Code) IF_GTKMM4(Gtk::ResponseType::Code, Gtk::RESPONSE_##Code)
#define TR_GTK_SELECTION_MODE(Code) IF_GTKMM4(Gtk::SelectionMode::Code, Gtk::SELECTION_##Code)
#define TR_GTK_SORT_TYPE(Code) IF_GTKMM4(Gtk::SortType::Code, Gtk::SORT_##Code)
#define TR_GTK_STATE_FLAGS(Code) IF_GTKMM4(Gtk::StateFlags::Code, Gtk::STATE_FLAG_##Code)
#define TR_GTK_TREE_VIEW_COLUMN_SIZING(Code) IF_GTKMM4(Gtk::TreeViewColumn::Sizing::Code, Gtk::TREE_VIEW_COLUMN_##Code)

#define TR_GTK_TREE_MODEL_CHILD_ITER(Obj) IF_GTKMM4((Obj).get_iter(), (Obj))
#define TR_GTK_WIDGET_GET_ROOT(Obj) IF_GTKMM4((Obj).get_root(), (Obj).get_toplevel())

#define TR_GDK_COLORSPACE(Code) IF_GTKMM4(Gdk::Colorspace::Code, Gdk::COLORSPACE_##Code)
#define TR_GDK_EVENT_TYPE(Code) IF_GTKMM4(Gdk::Event::Type::Code, GdkEventType::GDK_##Code)
#define TR_GDK_DRAG_ACTION(Code) IF_GTKMM4(Gdk::DragAction::Code, Gdk::ACTION_##Code)
#define TR_GDK_MODIFIED_TYPE(Code) IF_GTKMM4(Gdk::ModifierType::Code, GdkModifierType::GDK_##Code)

#define TR_GLIB_FILE_TEST(Code) IF_GLIBMM2_68(Glib::FileTest::Code, Glib::FILE_TEST_##Code)
#define TR_GLIB_NODE_TREE_TRAVERSE_FLAGS(Cls, Code) IF_GLIBMM2_68(Cls::TraverseFlags::Code, Cls::TRAVERSE_##Code)
#define TR_GLIB_SPAWN_FLAGS(Code) IF_GLIBMM2_68(Glib::SpawnFlags::Code, Glib::SPAWN_##Code)
#define TR_GLIB_USER_DIRECTORY(Code) IF_GLIBMM2_68(Glib::UserDirectory::Code, Glib::USER_DIRECTORY_##Code)

#define TR_GLIB_EXCEPTION_WHAT(Obj) IF_GLIBMM2_68((Obj).what(), (Obj).what().c_str())

#define TR_GIO_APP_INFO_CREATE_FLAGS(Code) IF_GLIBMM2_68(Gio::AppInfo::CreateFlags::Code, Gio::APP_INFO_CREATE_##Code)
#define TR_GIO_APPLICATION_FLAGS(Code) IF_GLIBMM2_68(Gio::Application::Flags::Code, Gio::APPLICATION_##Code)
#define TR_GIO_DBUS_BUS_TYPE(Code) IF_GLIBMM2_68(Gio::DBus::BusType::Code, Gio::DBus::BUS_TYPE_##Code)
#define TR_GIO_DBUS_PROXY_FLAGS(Code) IF_GLIBMM2_68(Gio::DBus::ProxyFlags::Code, Gio::DBus::PROXY_FLAGS_##Code)
#define TR_GIO_FILE_MONITOR_EVENT(Code) IF_GLIBMM2_68(Gio::FileMonitor::Event::Code, Gio::FILE_MONITOR_EVENT_##Code)

#define TR_CAIRO_SURFACE_FORMAT(Code) IF_GTKMM4(Cairo::Surface::Format::Code, Cairo::FORMAT_##Code)
#define TR_CAIRO_CONTEXT_OPERATOR(Code) IF_GTKMM4(Cairo::Context::Operator::Code, Cairo::OPERATOR_##Code)

#define TR_PANGO_ALIGNMENT(Code) IF_PANGOMM2_48(Pango::Alignment::Code, Pango::ALIGN_##Code)
#define TR_PANGO_ELLIPSIZE_MODE(Code) IF_PANGOMM2_48(Pango::EllipsizeMode::Code, Pango::ELLIPSIZE_##Code)
#define TR_PANGO_WEIGHT(Code) IF_PANGOMM2_48(Pango::Weight::Code, Pango::WEIGHT_##Code)

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

Glib::ustring gtr_get_unicode_string(GtrUnicode);

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

Gtk::ComboBox* gtr_priority_combo_new();
void gtr_priority_combo_init(Gtk::ComboBox& combo);
#define gtr_priority_combo_get_value(w) gtr_combo_box_get_active_enum(w)
#define gtr_priority_combo_set_value(w, val) gtr_combo_box_set_active_enum(w, val)

Gtk::ComboBox* gtr_combo_box_new_enum(std::vector<std::pair<Glib::ustring, int>> const& items);
void gtr_combo_box_set_enum(Gtk::ComboBox& combo, std::vector<std::pair<Glib::ustring, int>> const& items);
int gtr_combo_box_get_active_enum(Gtk::ComboBox const&);
void gtr_combo_box_set_active_enum(Gtk::ComboBox&, int value);

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

std::string gtr_get_full_resource_path(std::string const& rel_path);

/***
****
***/

extern size_t const max_recent_dirs;
std::list<std::string> gtr_get_recent_dirs(std::string const& pref);
void gtr_save_recent_dir(std::string const& pref, Glib::RefPtr<Session> const& core, std::string const& dir);

namespace gtr_detail
{

#if G_ENCODE_VERSION(GLIBMM_MAJOR_VERSION, GLIBMM_MINOR_VERSION) < G_ENCODE_VERSION(2, 62)

template<typename T>
inline T const& sprintify(T const& arg)
{
    return arg;
}

inline char const* sprintify(Glib::ustring const& arg)
{
    return arg.c_str();
}

inline char const* sprintify(std::string const& arg)
{
    return arg.c_str();
}

#endif

} // namespace gtr_detail

template<typename T, typename U>
inline Glib::RefPtr<T> gtr_ptr_static_cast(Glib::RefPtr<U> const& ptr)
{
#if G_ENCODE_VERSION(GLIBMM_MAJOR_VERSION, GLIBMM_MINOR_VERSION) < G_ENCODE_VERSION(2, 68)
    return Glib::RefPtr<T>::cast_static(ptr);
#else
    return std::static_pointer_cast<T>(ptr);
#endif
}

template<typename T, typename U>
inline Glib::RefPtr<T> gtr_ptr_dynamic_cast(Glib::RefPtr<U> const& ptr)
{
#if G_ENCODE_VERSION(GLIBMM_MAJOR_VERSION, GLIBMM_MINOR_VERSION) < G_ENCODE_VERSION(2, 68)
    return Glib::RefPtr<T>::cast_dynamic(ptr);
#else
    return std::dynamic_pointer_cast<T>(ptr);
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

namespace Glib
{

#if G_ENCODE_VERSION(GLIBMM_MAJOR_VERSION, GLIBMM_MINOR_VERSION) < G_ENCODE_VERSION(2, 68)

template<typename T>
inline bool operator==(RefPtr<T> const& lhs, std::nullptr_t /*rhs*/)
{
    return !lhs;
}

template<typename T>
inline bool operator!=(RefPtr<T> const& lhs, std::nullptr_t /*rhs*/)
{
    return !(lhs == nullptr);
}

template<typename T>
inline RefPtr<T> make_refptr_for_instance(T* object)
{
    return RefPtr<T>(object);
}

#endif

} // namespace Glib
