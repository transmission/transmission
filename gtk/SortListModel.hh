// This file Copyright © 2023-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "SorterBase.hh"
#include "SortListModel.h"

#if !GTKMM_CHECK_VERSION(4, 0, 0)
#include "ListModelAdapter.h"
#include "Utils.h"
#endif

template<typename ItemT>
SortListModel<ItemT>::SortListModel(Glib::RefPtr<Gio::ListModel> const& model, Glib::RefPtr<SorterType> const& sorter)
#if GTKMM_CHECK_VERSION(4, 0, 0)
    : Gtk::SortListModel(model, sorter)
#else
    : SortListModel(gtr_ptr_static_cast<Gtk::TreeModel>(ListModelAdapter::create<ItemT>(model)), sorter)
#endif
{
}

#if !GTKMM_CHECK_VERSION(4, 0, 0)

template<typename ItemT>
SortListModel<ItemT>::SortListModel(Glib::RefPtr<Gtk::TreeModel> const& model, Glib::RefPtr<SorterType> const& sorter)
    : Gtk::TreeModelSort(model)
{
    static auto const& self_col = ItemT::get_columns().self;

    auto const sort_func = [sorter](const_iterator const& lhs, const_iterator const& rhs)
    {
        auto const* const lhs_self = lhs->get_value(self_col);
        auto const* const rhs_self = rhs->get_value(self_col);

        if (lhs_self == nullptr && rhs_self == nullptr)
        {
            g_return_val_if_reached(0);
        }

        g_return_val_if_fail(lhs_self != nullptr, -1);
        g_return_val_if_fail(rhs_self != nullptr, 1);

        return sorter->compare(*lhs_self, *rhs_self);
    };

    set_default_sort_func(sort_func);

    signal_changed_tag_ = sorter->signal_changed().connect([this, sort_func](auto /*changes*/)
                                                           { set_default_sort_func(sort_func); });
}

template<typename ItemT>
SortListModel<ItemT>::~SortListModel()
{
    signal_changed_tag_.disconnect();
}

#endif

template<typename ItemT>
template<typename ModelT>
Glib::RefPtr<SortListModel<ItemT>> SortListModel<ItemT>::create(
    Glib::RefPtr<ModelT> const& model,
    Glib::RefPtr<SorterType> const& sorter)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new SortListModel(model, sorter));
}
