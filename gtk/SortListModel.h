// This file Copyright © 2023-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <giomm/listmodel.h>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gtkmm/sortlistmodel.h>
#else
#include <gtkmm/treemodel.h>
#include <gtkmm/treemodelsort.h>
#endif

template<typename ItemT>
class SorterBase;

template<typename ItemT>
class SortListModel : public IF_GTKMM4(Gtk::SortListModel, Gtk::TreeModelSort)
{
public:
    using SorterType = SorterBase<ItemT>;

public:
    SortListModel(Glib::RefPtr<Gio::ListModel> const& model, Glib::RefPtr<SorterType> const& sorter);

#if !GTKMM_CHECK_VERSION(4, 0, 0)
    SortListModel(Glib::RefPtr<Gtk::TreeModel> const& model, Glib::RefPtr<SorterType> const& sorter);
    ~SortListModel() override;
#endif

    template<typename ModelT>
    static Glib::RefPtr<SortListModel<ItemT>> create(Glib::RefPtr<ModelT> const& model, Glib::RefPtr<SorterType> const& sorter);

private:
#if !GTKMM_CHECK_VERSION(4, 0, 0)
    sigc::connection signal_changed_tag_;
#endif
};
