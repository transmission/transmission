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

typedef struct _TrCore TrCore;

class MessageLogWindow : public Gtk::Window
{
public:
    ~MessageLogWindow() override;

    static std::unique_ptr<MessageLogWindow> create(Gtk::Window& parent, TrCore* core);

protected:
    MessageLogWindow(Gtk::Window& parent, TrCore* core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
