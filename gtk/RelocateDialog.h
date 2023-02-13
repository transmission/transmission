// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h>
#include <libtransmission/tr-macros.h>

#include <glibmm/refptr.h>
#include <gtkmm/builder.h>
#include <gtkmm/dialog.h>
#include <gtkmm/window.h>

#include <memory>
#include <vector>

class Session;

class RelocateDialog : public Gtk::Dialog
{
public:
    RelocateDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core,
        std::vector<tr_torrent_id_t> const& torrent_ids);
    ~RelocateDialog() override;

    TR_DISABLE_COPY_MOVE(RelocateDialog)

    static std::unique_ptr<RelocateDialog> create(
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core,
        std::vector<tr_torrent_id_t> const& torrent_ids);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
