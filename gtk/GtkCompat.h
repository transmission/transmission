// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cairommconfig.h>
#include <glibmmconfig.h>
#include <gtkmmconfig.h>
#include <pangommconfig.h>

#include <cstddef>

// NOLINTBEGIN(cppcoreguidelines-macro-usage)

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

#ifndef CAIROMM_CHECK_VERSION
#define CAIROMM_CHECK_VERSION(major, minor, micro) \
    (CAIROMM_MAJOR_VERSION > (major) || (CAIROMM_MAJOR_VERSION == (major) && CAIROMM_MINOR_VERSION > (minor)) || \
     (CAIROMM_MAJOR_VERSION == (major) && CAIROMM_MINOR_VERSION == (minor) && CAIROMM_MICRO_VERSION >= (micro)))
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

#if CAIROMM_CHECK_VERSION(1, 16, 0)
#define IF_CAIROMM1_16(ThenValue, ElseValue) ThenValue
#else
#define IF_CAIROMM1_16(ThenValue, ElseValue) ElseValue
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
#define TR_GTK_TREE_MODEL_FLAGS(Code) IF_GTKMM4(Gtk::TreeModel::Flags::Code, Gtk::TREE_MODEL_##Code)
#define TR_GTK_TREE_VIEW_COLUMN_SIZING(Code) IF_GTKMM4(Gtk::TreeViewColumn::Sizing::Code, Gtk::TREE_VIEW_COLUMN_##Code)

#define TR_GTK_TREE_MODEL_CHILD_ITER(Obj) IF_GTKMM4((Obj).get_iter(), (Obj))
#define TR_GTK_WIDGET_GET_ROOT(Obj) IF_GTKMM4((Obj).get_root(), (Obj).get_toplevel())

#define TR_GDK_COLORSPACE(Code) IF_GTKMM4(Gdk::Colorspace::Code, Gdk::COLORSPACE_##Code)
#define TR_GDK_EVENT_TYPE(Code) IF_GTKMM4(Gdk::Event::Type::Code, GdkEventType::GDK_##Code)
#define TR_GDK_DRAG_ACTION(Code) IF_GTKMM4(Gdk::DragAction::Code, Gdk::ACTION_##Code)
#define TR_GDK_MODIFIED_TYPE(Code) IF_GTKMM4(Gdk::ModifierType::Code, GdkModifierType::GDK_##Code)

#define TR_GLIB_FILE_TEST(Code) IF_GLIBMM2_68(Glib::FileTest::Code, Glib::FILE_TEST_##Code)
#define TR_GLIB_NODE_TREE_TRAVERSE_FLAGS(Cls, Code) IF_GLIBMM2_68(Cls::TraverseFlags::Code, Cls::TRAVERSE_##Code)
#define TR_GLIB_PARAM_FLAGS(Code) IF_GLIBMM2_68(Glib::ParamFlags::Code, Glib::PARAM_##Code)
#define TR_GLIB_SPAWN_FLAGS(Code) IF_GLIBMM2_68(Glib::SpawnFlags::Code, Glib::SPAWN_##Code)
#define TR_GLIB_USER_DIRECTORY(Code) IF_GLIBMM2_68(Glib::UserDirectory::Code, Glib::USER_DIRECTORY_##Code)

#define TR_GLIB_EXCEPTION_WHAT(Obj) IF_GLIBMM2_68((Obj).what(), (Obj).what().c_str())

#define TR_GIO_APP_INFO_CREATE_FLAGS(Code) IF_GLIBMM2_68(Gio::AppInfo::CreateFlags::Code, Gio::APP_INFO_CREATE_##Code)
#define TR_GIO_APPLICATION_FLAGS(Code) IF_GLIBMM2_68(Gio::Application::Flags::Code, Gio::APPLICATION_##Code)
#define TR_GIO_DBUS_BUS_TYPE(Code) IF_GLIBMM2_68(Gio::DBus::BusType::Code, Gio::DBus::BUS_TYPE_##Code)
#define TR_GIO_DBUS_PROXY_FLAGS(Code) IF_GLIBMM2_68(Gio::DBus::ProxyFlags::Code, Gio::DBus::PROXY_FLAGS_##Code)
#define TR_GIO_FILE_MONITOR_EVENT(Code) IF_GLIBMM2_68(Gio::FileMonitor::Event::Code, Gio::FILE_MONITOR_EVENT_##Code)

#define TR_CAIRO_SURFACE_FORMAT(Code) IF_CAIROMM1_16(Cairo::Surface::Format::Code, Cairo::FORMAT_##Code)
#define TR_CAIRO_CONTEXT_OPERATOR(Code) IF_CAIROMM1_16(Cairo::Context::Operator::Code, Cairo::OPERATOR_##Code)

#define TR_PANGO_ALIGNMENT(Code) IF_PANGOMM2_48(Pango::Alignment::Code, Pango::ALIGN_##Code)
#define TR_PANGO_ELLIPSIZE_MODE(Code) IF_PANGOMM2_48(Pango::EllipsizeMode::Code, Pango::ELLIPSIZE_##Code)
#define TR_PANGO_WEIGHT(Code) IF_PANGOMM2_48(Pango::Weight::Code, Pango::WEIGHT_##Code)

// NOLINTEND(cppcoreguidelines-macro-usage)

namespace Glib
{

#if !GLIBMM_CHECK_VERSION(2, 68, 0)

template<typename T>
class RefPtr;

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
