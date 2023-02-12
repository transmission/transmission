// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>

#include <glibmm.h>
#include <gtkmm.h>

#include <libtransmission/tr-macros.h>

class Session;

class MainWindow : public Gtk::ApplicationWindow
{
public:
    MainWindow(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Gtk::Application& app,
        Glib::RefPtr<Gio::ActionGroup> const& actions,
        Glib::RefPtr<Session> const& core);
    ~MainWindow() override;

    TR_DISABLE_COPY_MOVE(MainWindow)

    static std::unique_ptr<MainWindow> create(
        Gtk::Application& app,
        Glib::RefPtr<Gio::ActionGroup> const& actions,
        Glib::RefPtr<Session> const& core);

    Glib::RefPtr<Gtk::TreeSelection> get_selection() const;

    void set_busy(bool isBusy);
    void refresh();

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
