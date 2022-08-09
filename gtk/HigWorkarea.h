// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <gtkmm.h>

#include <libtransmission/tr-macros.h>

/**
*** utility code for making dialog layout that follows the Gnome HIG.
*** see section 8.2.2, Visual Design > Window Layout > Dialogs.
**/

class HigWorkarea : public Gtk::Grid
{
public:
    HigWorkarea();

    TR_DISABLE_COPY_MOVE(HigWorkarea)

    void add_section_divider(guint& row);
    void add_section_title_widget(guint& row, Gtk::Widget& w);
    void add_section_title(guint& row, Glib::ustring const& section_title);
    void add_wide_tall_control(guint& row, Gtk::Widget& w);
    void add_wide_control(guint& row, Gtk::Widget& w);
    Gtk::CheckButton* add_wide_checkbutton(guint& row, Glib::ustring const& mnemonic_string, bool is_active);
    void add_label_w(guint row, Gtk::Widget& label_widget);
    Gtk::Label* add_tall_row(
        guint& row,
        Glib::ustring const& mnemonic_string,
        Gtk::Widget& control,
        Gtk::Widget* mnemonic_or_null_for_control = nullptr);
    Gtk::Label* add_row(
        guint& row,
        Glib::ustring const& mnemonic_string,
        Gtk::Widget& control,
        Gtk::Widget* mnemonic_or_null_for_control = nullptr);
    void add_row_w(
        guint& row,
        Gtk::Widget& label_widget,
        Gtk::Widget& control,
        Gtk::Widget* mnemonic_or_null_for_control = nullptr);

private:
    void add_tall_control(guint row, Gtk::Widget& control);
    void add_control(guint row, Gtk::Widget& control);
};

auto inline constexpr GUI_PAD_SMALL = int{ 3 };
auto inline constexpr GUI_PAD = int{ 6 };
auto inline constexpr GUI_PAD_BIG = int{ 12 };
auto inline constexpr GUI_PAD_LARGE = int{ 12 };
