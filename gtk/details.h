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

class TrCore;

class DetailsDialog : public Gtk::Dialog
{
public:
    ~DetailsDialog() override;

    static std::unique_ptr<DetailsDialog> create(Gtk::Window& parent, Glib::RefPtr<TrCore> const& core);

    void set_torrents(std::vector<int> const& torrent_ids);

protected:
    DetailsDialog(Gtk::Window& parent, Glib::RefPtr<TrCore> const& core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
