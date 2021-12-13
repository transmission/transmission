/*
 * This file Copyright (C) 2007-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>

#include <glibmm.h>
#include <gtkmm.h>

struct tr_torrent;

class TorrentCellRenderer : public Gtk::CellRenderer
{
public:
    TorrentCellRenderer();
    ~TorrentCellRenderer() override;

    Glib::PropertyProxy<gpointer> property_torrent();
    Glib::PropertyProxy<double> property_piece_upload_speed();
    Glib::PropertyProxy<double> property_piece_download_speed();
    Glib::PropertyProxy<int> property_bar_height();
    Glib::PropertyProxy<bool> property_compact();

protected:
    void get_preferred_width_vfunc(Gtk::Widget& widget, int& minimum_width, int& natural_width) const override;
    void get_preferred_height_vfunc(Gtk::Widget& widget, int& minimum_height, int& natural_height) const override;
    void render_vfunc(
        Cairo::RefPtr<Cairo::Context> const& cr,
        Gtk::Widget& widget,
        Gdk::Rectangle const& background_area,
        Gdk::Rectangle const& cell_area,
        Gtk::CellRendererState flags) override;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
