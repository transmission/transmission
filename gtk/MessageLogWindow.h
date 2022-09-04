// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>

#include <gtkmm.h>

#include <libtransmission/tr-macros.h>

class Session;

class MessageLogWindow : public Gtk::Window
{
public:
    MessageLogWindow(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core);
    ~MessageLogWindow() override;

    TR_DISABLE_COPY_MOVE(MessageLogWindow)

    static std::unique_ptr<MessageLogWindow> create(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

protected:
    void on_show() override;
    void on_hide() override;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
