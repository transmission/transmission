// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <giomm/file.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/application.h>

#include <memory>
#include <string>
#include <vector>

class Application : public Gtk::Application
{
public:
    Application(std::string const& config_dir, bool start_paused, bool start_iconified);
    Application(Application&&) = delete;
    Application(Application const&) = delete;
    Application& operator=(Application&&) = delete;
    Application& operator=(Application const&) = delete;
    ~Application() override;

    friend void gtr_actions_handler(Glib::ustring const& action_name, gpointer user_data);

protected:
    void on_startup() override;
    void on_activate() override;
    void on_open(std::vector<Glib::RefPtr<Gio::File>> const& f, Glib::ustring const& hint) override;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
