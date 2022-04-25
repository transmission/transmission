// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include <sys/types.h>

#include <glibmm.h>
#include <gtkmm.h>

#include <fmt/core.h>
#include <fmt/format.h>

#include <libtransmission/transmission.h>
#include <libtransmission/tr-macros.h>

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

enum class GtrUnicode
{
    Up,
    Down,
    Inf,
    Bullet
};

Glib::ustring gtr_get_unicode_string(GtrUnicode);

/* return a human-readable string for the size given in bytes. */
Glib::ustring tr_strlsize(guint64 size);

/* return a human-readable string for the given ratio. */
Glib::ustring tr_strlratio(double ratio);

/* return a human-readable string for the time given in seconds. */
Glib::ustring tr_strltime(time_t secs);

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
void gtr_widget_set_visible(Gtk::Widget&, bool);

void gtr_dialog_set_content(Gtk::Dialog& dialog, Gtk::Widget& content);

/***
****
***/

Gtk::ComboBox* gtr_priority_combo_new();
#define gtr_priority_combo_get_value(w) gtr_combo_box_get_active_enum(w)
#define gtr_priority_combo_set_value(w, val) gtr_combo_box_set_active_enum(w, val)

Gtk::ComboBox* gtr_combo_box_new_enum(std::vector<std::pair<Glib::ustring, int>> const& items);
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
    Gtk::TreeView* view,
    GdkEventButton* event,
    std::function<void(GdkEventButton*)> const& callback = {});

/* if the click didn't specify a row, clear the selection */
bool on_tree_view_button_released(Gtk::TreeView* view, GdkEventButton* event);

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

template<typename... Ts>
inline Glib::ustring gtr_sprintf(char const* fmt, Ts const&... args)
{
#if G_ENCODE_VERSION(GLIBMM_MAJOR_VERSION, GLIBMM_MINOR_VERSION) < G_ENCODE_VERSION(2, 62)
    auto* const c_str = g_strdup_printf(fmt, gtr_detail::sprintify(args)...);
    Glib::ustring ustr(c_str);
    g_free(c_str);

    return ustr;
#else
    return Glib::ustring::sprintf(fmt, args...);
#endif
}

template<typename... Ts>
inline Glib::ustring gtr_sprintf(Glib::ustring const& fmt, Ts const&... args)
{
#if G_ENCODE_VERSION(GLIBMM_MAJOR_VERSION, GLIBMM_MINOR_VERSION) < G_ENCODE_VERSION(2, 62)
    return gtr_sprintf(fmt.c_str(), args...);
#else
    return Glib::ustring::sprintf(fmt, args...);
#endif
}

template<typename T>
inline T* gtr_get_ptr(Glib::RefPtr<T> const& ptr)
{
#if G_ENCODE_VERSION(GLIBMM_MAJOR_VERSION, GLIBMM_MINOR_VERSION) < G_ENCODE_VERSION(2, 56)
    return ptr.operator->();
#else
    return ptr.get();
#endif
}

template<typename T, typename U>
inline Glib::RefPtr<T> gtr_ptr_static_cast(Glib::RefPtr<U> const& ptr)
{
#if G_ENCODE_VERSION(GLIBMM_MAJOR_VERSION, GLIBMM_MINOR_VERSION) < G_ENCODE_VERSION(2, 68)
    return Glib::RefPtr<T>::cast_static(ptr);
#else
    return std::static_pointer_cast<T>(ptr);
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
