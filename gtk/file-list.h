/*
 * This file Copyright (C) 2009-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>

#include <gtkmm.h>

class TrCore;

class FileList : public Gtk::ScrolledWindow
{
public:
    FileList(Glib::RefPtr<TrCore> const& core, int torrent_id);
    ~FileList() override;

    void clear();
    void set_torrent(int torrent_id);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
