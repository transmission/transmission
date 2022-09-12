// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/tr-macros.h>

#include "HigWorkarea.h"
#include "Utils.h"

HigWorkarea::HigWorkarea()
{
    set_border_width(GUI_PAD_BIG);
    set_row_spacing(GUI_PAD);
    set_column_spacing(GUI_PAD_BIG);
}

void HigWorkarea::add_section_divider(guint& row)
{
    auto* w = Gtk::make_managed<Gtk::Fixed>();
    w->set_size_request(0U, 6U);
    attach(*w, 0, row, 2, 1);
    ++row;
}

void HigWorkarea::add_section_title_widget(guint& row, Gtk::Widget& w)
{
    w.set_hexpand(true);
    attach(w, 0, row, 2, 1);
    ++row;
}

void HigWorkarea::add_section_title(guint& row, Glib::ustring const& section_title)
{
    auto* l = Gtk::make_managed<Gtk::Label>(gtr_sprintf("<b>%s</b>", section_title));
    l->set_halign(TR_GTK_ALIGN(START));
    l->set_valign(TR_GTK_ALIGN(CENTER));
    l->set_use_markup(true);
    add_section_title_widget(row, *l);
}

void HigWorkarea::add_wide_control(guint& row, Gtk::Widget& w)
{
    w.set_hexpand(true);
    w.set_margin_start(18);
    attach(w, 0, row, 2, 1);
    ++row;
}

void HigWorkarea::add_wide_tall_control(guint& row, Gtk::Widget& w)
{
    w.set_hexpand(true);
    w.set_vexpand(true);
    add_wide_control(row, w);
}

Gtk::CheckButton* HigWorkarea::add_wide_checkbutton(guint& row, Glib::ustring const& mnemonic_string, bool is_active)
{
    auto* w = Gtk::make_managed<Gtk::CheckButton>(mnemonic_string, true);
    w->set_active(is_active);
    add_wide_control(row, *w);
    return w;
}

void HigWorkarea::add_label_w(guint row, Gtk::Widget& w)
{
    w.set_margin_start(18);

    if (auto* label = dynamic_cast<Gtk::Label*>(&w); label != nullptr)
    {
        label->set_halign(TR_GTK_ALIGN(START));
        label->set_valign(TR_GTK_ALIGN(CENTER));
        label->set_use_markup(true);
    }

    attach(w, 0, row, 1, 1);
}

void HigWorkarea::add_tall_control(guint row, Gtk::Widget& control)
{
    if (auto* label = dynamic_cast<Gtk::Label*>(&control); label != nullptr)
    {
        label->set_halign(TR_GTK_ALIGN(START));
        label->set_valign(TR_GTK_ALIGN(CENTER));
    }

    control.set_hexpand(true);
    control.set_vexpand(true);
    attach(control, 1, row, 1, 1);
}

void HigWorkarea::add_control(guint row, Gtk::Widget& control)
{
    if (auto* label = dynamic_cast<Gtk::Label*>(&control); label != nullptr)
    {
        label->set_halign(TR_GTK_ALIGN(START));
        label->set_valign(TR_GTK_ALIGN(CENTER));
    }

    control.set_hexpand(true);
    attach(control, 1, row, 1, 1);
}

void HigWorkarea::add_row_w(guint& row, Gtk::Widget& label_widget, Gtk::Widget& control, Gtk::Widget* mnemonic)
{
    add_label_w(row, label_widget);
    add_control(row, control);

    if (auto* label = dynamic_cast<Gtk::Label*>(&label_widget); label != nullptr)
    {
        label->set_mnemonic_widget(mnemonic != nullptr ? *mnemonic : control);
    }

    ++row;
}

Gtk::Label* HigWorkarea::add_row(guint& row, Glib::ustring const& mnemonic_string, Gtk::Widget& control, Gtk::Widget* mnemonic)
{
    auto* l = Gtk::make_managed<Gtk::Label>(mnemonic_string, true);
    add_row_w(row, *l, control, mnemonic);
    return l;
}

Gtk::Label* HigWorkarea::add_tall_row(
    guint& row,
    Glib::ustring const& mnemonic_string,
    Gtk::Widget& control,
    Gtk::Widget* mnemonic)
{
    auto* l = Gtk::make_managed<Gtk::Label>(mnemonic_string, true);
    auto* h = Gtk::make_managed<Gtk::Box>(TR_GTK_ORIENTATION(HORIZONTAL), 0);
    auto* v = Gtk::make_managed<Gtk::Box>(TR_GTK_ORIENTATION(VERTICAL), 0);
    h->pack_start(*l, false, false, 0);
    v->pack_start(*h, false, false, GUI_PAD_SMALL);

    add_label_w(row, *v);
    add_tall_control(row, control);

    l->set_mnemonic_widget(mnemonic ? *mnemonic : control);

    ++row;
    return l;
}
