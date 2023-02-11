// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glibmm.h>
#include <gtkmm.h>

#include <libtransmission/tr-macros.h>

class Application : public Gtk::Application
{
public:
    Application(std::string const& config_dir, bool start_paused, bool is_iconified);
    ~Application() override;

    TR_DISABLE_COPY_MOVE(Application)

    friend void gtr_actions_handler(Glib::ustring const& action_name, gpointer user_data);

protected:
    void on_startup() override;
    void on_activate() override;
    void on_open(std::vector<Glib::RefPtr<Gio::File>> const& f, Glib::ustring const& hint) override;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
