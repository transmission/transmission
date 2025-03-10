// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h>

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/builder.h>
#include <gtkmm/scrolledwindow.h>

#include <memory>

class Session;

class FileList : public Gtk::ScrolledWindow
{
public:
    FileList(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::ustring const& view_name,
        Glib::RefPtr<Session> const& core,
        tr_torrent_id_t torrent_id);
    FileList(FileList&&) = delete;
    FileList(FileList const&) = delete;
    FileList& operator=(FileList&&) = delete;
    FileList& operator=(FileList const&) = delete;
    ~FileList() override;

    void clear();
    void set_torrent(tr_torrent_id_t torrent_id);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
