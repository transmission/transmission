/*
 * This file Copyright (C) 2009-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>
#include <vector>

#include <gtkmm.h>

class Session;

class RelocateDialog : public Gtk::Dialog
{
public:
    ~RelocateDialog() override;

    static std::unique_ptr<RelocateDialog> create(
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core,
        std::vector<int> const& torrent_ids);

protected:
    RelocateDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core, std::vector<int> const& torrent_ids);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
