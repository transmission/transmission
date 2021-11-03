/*
 * This file Copyright (C) 2007-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>

#include <gtkmm.h>

class Session;

class StatsDialog : public Gtk::Dialog
{
public:
    ~StatsDialog() override;

    static std::unique_ptr<StatsDialog> create(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

protected:
    StatsDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
