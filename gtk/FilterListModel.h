// This file Copyright © 2023-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <giomm/listmodel.h>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gtkmm/filterlistmodel.h>
#else
#include <gtkmm/treemodel.h>
#include <gtkmm/treemodelfilter.h>
#endif

template<typename ItemT>
class FilterBase;

template<typename ItemT>
class FilterListModel : public IF_GTKMM4(Gtk::FilterListModel, Gtk::TreeModelFilter)
{
public:
    using FilterType = FilterBase<ItemT>;

public:
    FilterListModel(Glib::RefPtr<Gio::ListModel> const& model, Glib::RefPtr<FilterType> const& filter);

#if !GTKMM_CHECK_VERSION(4, 0, 0)
    FilterListModel(Glib::RefPtr<Gtk::TreeModel> const& model, Glib::RefPtr<FilterType> const& filter);
    ~FilterListModel() override;

    guint get_n_items() const;

    sigc::signal<void(guint, guint, guint)>& signal_items_changed();
#endif

    template<typename ModelT>
    static Glib::RefPtr<FilterListModel<ItemT>> create(
        Glib::RefPtr<ModelT> const& model,
        Glib::RefPtr<FilterType> const& filter);

private:
#if !GTKMM_CHECK_VERSION(4, 0, 0)
    bool matches_all_ = false;
    bool matches_none_ = false;

    sigc::signal<void(guint, guint, guint)> signal_items_changed_;

    sigc::connection signal_changed_tag_;
#endif
};
