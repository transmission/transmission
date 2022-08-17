// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// _AppIndicatorClass::{fallback,unfallback} use deprecated GtkStatusIcon
#undef GTK_DISABLE_DEPRECATED
// We're using deprecated Gtk::StatusItem ourselves as well
#undef GTKMM_DISABLE_DEPRECATED

#include <memory>
#include <string>

#include <glibmm.h>
#include <glibmm/i18n.h>

#ifdef HAVE_LIBAPPINDICATOR
#include <libappindicator/app-indicator.h>
#endif

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include "Actions.h"
#include "Session.h"
#include "SystemTrayIcon.h"
#include "Utils.h"

using namespace std::literals;

namespace
{

auto const TrayIconName = Glib::ustring("transmission-tray-icon"s);
auto const AppIconName = Glib::ustring("transmission"s);
auto const AppName = Glib::ustring("transmission-gtk"s);

} // namespace

class SystemTrayIcon::Impl
{
public:
    Impl(Gtk::Window& main_window, Glib::RefPtr<Session> const& core);
    ~Impl();

    TR_DISABLE_COPY_MOVE(Impl)

    void refresh();

private:
    void activated();
    void popup(guint button, guint when);

    [[nodiscard]] std::string make_tooltip_text() const;

private:
    Glib::RefPtr<Session> const core_;

    Gtk::Menu* menu_;

#ifdef HAVE_LIBAPPINDICATOR
    AppIndicator* indicator_;
#else
    Glib::RefPtr<Gtk::StatusIcon> icon_;
#endif
};

#ifdef HAVE_LIBAPPINDICATOR

SystemTrayIcon::Impl::~Impl()
{
    g_object_unref(indicator_);
}

void SystemTrayIcon::Impl::refresh()
{
}

#else

SystemTrayIcon::Impl::~Impl() = default;

void SystemTrayIcon::Impl::activated()
{
    gtr_action_activate("toggle-main-window");
}

void SystemTrayIcon::Impl::popup(guint /*button*/, guint /*when*/)
{
    menu_->popup_at_pointer(nullptr);
}

void SystemTrayIcon::Impl::refresh()
{
    icon_->set_tooltip_text(make_tooltip_text());
}

#endif

namespace
{

Glib::ustring getIconName()
{
    Glib::ustring icon_name;

    // if the tray's icon is a 48x48 file, use it.
    // otherwise, use the fallback builtin icon.
    if (auto theme = Gtk::IconTheme::get_default(); !theme->has_icon(TrayIconName))
    {
        icon_name = AppIconName;
    }
    else
    {
        auto const icon_info = theme->lookup_icon(TrayIconName, 48, Gtk::ICON_LOOKUP_USE_BUILTIN);
        bool const icon_is_builtin = icon_info.get_filename().empty();

        icon_name = icon_is_builtin ? AppIconName : TrayIconName;
    }

    return icon_name;
}

} // namespace

SystemTrayIcon::SystemTrayIcon(Gtk::Window& main_window, Glib::RefPtr<Session> const& core)
    : impl_(std::make_unique<Impl>(main_window, core))
{
}

SystemTrayIcon::~SystemTrayIcon() = default;

void SystemTrayIcon::refresh()
{
    impl_->refresh();
}

SystemTrayIcon::Impl::Impl(Gtk::Window& main_window, Glib::RefPtr<Session> const& core)
    : core_(core)
{
    auto const icon_name = getIconName();
    menu_ = Gtk::make_managed<Gtk::Menu>(gtr_action_get_object<Gio::Menu>("icon-popup"));
    menu_->attach_to_widget(main_window);

#ifdef HAVE_LIBAPPINDICATOR

    indicator_ = app_indicator_new(AppName.c_str(), icon_name.c_str(), APP_INDICATOR_CATEGORY_SYSTEM_SERVICES);
    app_indicator_set_status(indicator_, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_menu(indicator_, Glib::unwrap(menu_));
    app_indicator_set_title(indicator_, Glib::get_application_name().c_str());

#else

    icon_ = Gtk::StatusIcon::create(icon_name);
    icon_->signal_activate().connect(sigc::mem_fun(*this, &Impl::activated));
    icon_->signal_popup_menu().connect(sigc::mem_fun(*this, &Impl::popup));

#endif
}

std::string SystemTrayIcon::Impl::make_tooltip_text() const
{
    auto const* const session = core_->get_session();
    return fmt::format(
        _("{upload_speed} ▲ {download_speed} ▼"),
        fmt::arg("upload_speed", tr_formatter_speed_KBps(tr_sessionGetRawSpeed_KBps(session, TR_UP))),
        fmt::arg("download_speed", tr_formatter_speed_KBps(tr_sessionGetRawSpeed_KBps(session, TR_DOWN))));
}
