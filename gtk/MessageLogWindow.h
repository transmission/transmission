/*
 * This file Copyright (C) 2008-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>

#include <gtkmm.h>

class Session;

class MessageLogWindow : public Gtk::Window
{
public:
    ~MessageLogWindow() override;

    static std::unique_ptr<MessageLogWindow> create(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

protected:
    MessageLogWindow(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

    void on_show() override;
    void on_hide() override;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
