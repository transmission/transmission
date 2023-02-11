// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/tr-macros.h>

#include <glibmm/refptr.h>
#include <gtkmm/window.h>

#include <memory>

class Session;

class SystemTrayIcon
{
public:
    SystemTrayIcon(Gtk::Window& main_window, Glib::RefPtr<Session> const& core);
    ~SystemTrayIcon();

    TR_DISABLE_COPY_MOVE(SystemTrayIcon)

    void refresh();

    static bool is_available();
    static std::unique_ptr<SystemTrayIcon> create(Gtk::Window& main_window, Glib::RefPtr<Session> const& core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
