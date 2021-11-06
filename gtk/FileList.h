// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>

#include <gtkmm.h>

#include <libtransmission/tr-macros.h>

class Session;

class FileList : public Gtk::ScrolledWindow
{
public:
    FileList(Glib::RefPtr<Session> const& core, tr_torrent_id_t torrent_id);
    FileList(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::ustring const& view_name,
        Glib::RefPtr<Session> const& core,
        tr_torrent_id_t torrent_id);
    ~FileList() override;

    TR_DISABLE_COPY_MOVE(FileList)

    void clear();
    void set_torrent(tr_torrent_id_t torrent_id);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
