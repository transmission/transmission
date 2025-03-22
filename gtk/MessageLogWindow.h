// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <glibmm/refptr.h>
#include <gtkmm/builder.h>
#include <gtkmm/window.h>

#include <memory>

class Session;

class MessageLogWindow : public Gtk::Window
{
public:
    MessageLogWindow(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core);
    MessageLogWindow(MessageLogWindow&&) = delete;
    MessageLogWindow(MessageLogWindow const&) = delete;
    MessageLogWindow& operator=(MessageLogWindow&&) = delete;
    MessageLogWindow& operator=(MessageLogWindow const&) = delete;
    ~MessageLogWindow() override;

    static std::unique_ptr<MessageLogWindow> create(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
