// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>

#include <gtkmm.h>

#include <libtransmission/tr-macros.h>

class Session;

class MakeDialog : public Gtk::Dialog
{
public:
    ~MakeDialog() override;

    TR_DISABLE_COPY_MOVE(MakeDialog)

    static std::unique_ptr<MakeDialog> create(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

protected:
    MakeDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
