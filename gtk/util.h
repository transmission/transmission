/*
 * This file Copyright (C) 2008-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <functional>
#include <sys/types.h>
#include <glibmm.h>
#include <gtkmm.h>

#include <libtransmission/transmission.h>
#include <libtransmission/tr-macros.h>

extern int const mem_K;
extern char const* mem_K_str;
extern char const* mem_M_str;
extern char const* mem_G_str;
extern char const* mem_T_str;

extern int const disk_K;
extern char const* disk_K_str;
extern char const* disk_M_str;
extern char const* disk_G_str;
extern char const* disk_T_str;

extern int const speed_K;
extern char const* speed_K_str;
extern char const* speed_M_str;
extern char const* speed_G_str;
extern char const* speed_T_str;

enum
{
    GTR_UNICODE_UP,
    GTR_UNICODE_DOWN,
    GTR_UNICODE_INF,
    GTR_UNICODE_BULLET
};

Glib::ustring gtr_get_unicode_string(int);

/* return a percent formatted string of either x.xx, xx.x or xxx */
Glib::ustring tr_strlpercent(double x);

/* return a human-readable string for the size given in bytes. */
Glib::ustring tr_strlsize(guint64 size);

/* return a human-readable string for the given ratio. */
Glib::ustring tr_strlratio(double ratio);

/* return a human-readable string for the time given in seconds. */
Glib::ustring tr_strltime(time_t secs);

/***
****
***/

/* http://www.legaltorrents.com/some/announce/url --> legaltorrents.com */
Glib::ustring gtr_get_host_from_url(Glib::ustring const& url);

bool gtr_is_magnet_link(Glib::ustring const& str);

bool gtr_is_hex_hashcode(std::string const& str);

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

class TrCore;

class FreeSpaceLabel : public Gtk::Label
{
public:
    FreeSpaceLabel(Glib::RefPtr<TrCore> const& core, std::string const& dir = {});
    ~FreeSpaceLabel() override;

    void set_dir(std::string const& dir);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};

/***
****
***/

void gtr_unrecognized_url_dialog(Gtk::Widget& parent, Glib::ustring const& url);

void gtr_add_torrent_error_dialog(
    Gtk::Widget& window_or_child,
    int err,
    tr_torrent* duplicate_torrent,
    std::string const& filename);

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

template<>
struct std::hash<Glib::ustring>
{
    std::size_t operator()(Glib::ustring const& s) const
    {
        return std::hash<std::string>()(s.raw());
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
