/*
 * This file Copyright (C) 2007-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>
#include <vector>

#include <glibmm.h>
#include <gtkmm.h>

#include <libtransmission/tr-macros.h>

class Session;

class DetailsDialog : public Gtk::Dialog
{
public:
    ~DetailsDialog() override;

    TR_DISABLE_COPY_MOVE(DetailsDialog)

    static std::unique_ptr<DetailsDialog> create(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

    void set_torrents(std::vector<int> const& torrent_ids);

protected:
    DetailsDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
