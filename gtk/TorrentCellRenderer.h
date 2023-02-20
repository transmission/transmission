// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <libtransmission/tr-macros.h>

#include <glibmm/propertyproxy.h>
#include <gtkmm/cellrenderer.h>

#include <memory>

class Torrent;

class TorrentCellRenderer : public Gtk::CellRenderer
{
    using SnapshotPtr = IF_GTKMM4(Glib::RefPtr<Gtk::Snapshot>, Cairo::RefPtr<Cairo::Context>);

public:
    TorrentCellRenderer();
    ~TorrentCellRenderer() override;

    TR_DISABLE_COPY_MOVE(TorrentCellRenderer)

    Glib::PropertyProxy<Torrent*> property_torrent();

    Glib::PropertyProxy<int> property_bar_height();
    Glib::PropertyProxy<bool> property_compact();

protected:
    void get_preferred_width_vfunc(Gtk::Widget& widget, int& minimum_width, int& natural_width) const override;
    void get_preferred_height_vfunc(Gtk::Widget& widget, int& minimum_height, int& natural_height) const override;
    void IF_GTKMM4(snapshot_vfunc, render_vfunc)(
        SnapshotPtr const& snapshot,
        Gtk::Widget& widget,
        Gdk::Rectangle const& background_area,
        Gdk::Rectangle const& cell_area,
        Gtk::CellRendererState flags) override;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
