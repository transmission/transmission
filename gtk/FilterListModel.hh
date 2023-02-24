// This file Copyright © 2023-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "FilterBase.hh"
#include "FilterListModel.h"

#if !GTKMM_CHECK_VERSION(4, 0, 0)
#include "ListModelAdapter.h"
#include "Utils.h"
#endif

template<typename ItemT>
FilterListModel<ItemT>::FilterListModel(Glib::RefPtr<Gio::ListModel> const& model, Glib::RefPtr<FilterType> const& filter)
#if GTKMM_CHECK_VERSION(4, 0, 0)
    : Gtk::FilterListModel(model, filter)
#else
    : FilterListModel(gtr_ptr_static_cast<Gtk::TreeModel>(ListModelAdapter::create<ItemT>(model)), filter)
#endif
{
}

#if !GTKMM_CHECK_VERSION(4, 0, 0)

template<typename ItemT>
FilterListModel<ItemT>::FilterListModel(Glib::RefPtr<Gtk::TreeModel> const& model, Glib::RefPtr<FilterType> const& filter)
    : Gtk::TreeModelFilter(model)
    , matches_all_(filter->matches_all())
    , matches_none_(filter->matches_none())
{
    static auto const& self_col = ItemT::get_columns().self;

    auto const filter_func = [this, filter](const_iterator const& iter)
    {
        if (matches_all_)
        {
            return true;
        }

        if (matches_none_)
        {
            return false;
        }

        auto const* const self = iter->get_value(self_col);
        g_return_val_if_fail(self != nullptr, false);

        return filter->match(*self);
    };

    set_visible_func(filter_func);

    signal_changed_tag_ = filter->signal_changed().connect(
        [this, filter](auto /*changes*/)
        {
            matches_all_ = filter->matches_all();
            matches_none_ = filter->matches_none();
            refilter();
        });

    signal_row_inserted().connect([this](auto const& path, auto const& /*iter*/)
                                  { signal_items_changed_.emit(path.front(), 0, 1); });
    signal_row_deleted().connect([this](auto const& path) { signal_items_changed_.emit(path.front(), 1, 0); });
}

template<typename ItemT>
FilterListModel<ItemT>::~FilterListModel()
{
    signal_changed_tag_.disconnect();
}

template<typename ItemT>
guint FilterListModel<ItemT>::get_n_items() const
{
    return children().size();
}

template<typename ItemT>
sigc::signal<void(guint, guint, guint)>& FilterListModel<ItemT>::signal_items_changed()
{
    return signal_items_changed_;
}

#endif

template<typename ItemT>
template<typename ModelT>
Glib::RefPtr<FilterListModel<ItemT>> FilterListModel<ItemT>::create(
    Glib::RefPtr<ModelT> const& model,
    Glib::RefPtr<FilterType> const& filter)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new FilterListModel(model, filter));
}
