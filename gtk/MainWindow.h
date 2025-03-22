// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <giomm/actiongroup.h>
#include <glibmm/refptr.h>
#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/builder.h>

#include <memory>

class Session;
class Torrent;

class MainWindow : public Gtk::ApplicationWindow
{
public:
    MainWindow(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Gtk::Application& app,
        Glib::RefPtr<Gio::ActionGroup> const& actions,
        Glib::RefPtr<Session> const& core);
    MainWindow(MainWindow&&) = delete;
    MainWindow(MainWindow const&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow const&) = delete;
    ~MainWindow() override;

    static std::unique_ptr<MainWindow> create(
        Gtk::Application& app,
        Glib::RefPtr<Gio::ActionGroup> const& actions,
        Glib::RefPtr<Session> const& core);

    void for_each_selected_torrent(std::function<void(Glib::RefPtr<Torrent> const&)> const& callback) const;
    bool for_each_selected_torrent_until(std::function<bool(Glib::RefPtr<Torrent> const&)> const& callback) const;

    void select_all();
    void unselect_all();

    void set_busy(bool isBusy);
    void refresh();

    sigc::signal<void()>& signal_selection_changed();

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
