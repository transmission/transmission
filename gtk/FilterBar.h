// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <libtransmission/tr-macros.h>

#include <giomm/listmodel.h>
#include <glibmm/extraclassinit.h>
#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/builder.h>
#include <gtkmm/treemodel.h>

#include <memory>

class Session;

class FilterBarExtraInit : public Glib::ExtraClassInit
{
public:
    FilterBarExtraInit();

private:
    static void class_init(void* klass, void* user_data);
    static void instance_init(GTypeInstance* instance, void* klass);
};

class FilterBar
    : public FilterBarExtraInit
    , public Gtk::Box
{
public:
    using Model = IF_GTKMM4(Gio::ListModel, Gtk::TreeModel);

public:
    FilterBar();
    FilterBar(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    ~FilterBar() override;

    TR_DISABLE_COPY_MOVE(FilterBar)

    Glib::RefPtr<Model> get_filter_model() const;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
