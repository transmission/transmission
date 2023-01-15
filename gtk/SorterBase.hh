// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "SorterBase.h"

#if GTKMM_CHECK_VERSION(4, 0, 0)

template<typename T>
Gtk::Ordering SorterBase<T>::compare_vfunc(gpointer lhs, gpointer rhs)
{
    auto const* const concrete_lhs = dynamic_cast<T const*>(Glib::wrap_auto(static_cast<GObject*>(lhs)));
    auto const* const concrete_rhs = dynamic_cast<T const*>(Glib::wrap_auto(static_cast<GObject*>(rhs)));

    if (concrete_lhs == nullptr && concrete_lhs == nullptr)
    {
        g_return_val_if_reached(Gtk::Ordering::EQUAL);
    }

    g_return_val_if_fail(concrete_lhs != nullptr, Gtk::Ordering::SMALLER);
    g_return_val_if_fail(concrete_rhs != nullptr, Gtk::Ordering::LARGER);

    return Gtk::Ordering{ compare(*concrete_lhs, *concrete_rhs) };
}

template<typename T>
Gtk::Sorter::Order SorterBase<T>::get_order_vfunc()
{
    return Gtk::Sorter::Order::PARTIAL;
}

#else

template<typename T>
sigc::signal<void(typename SorterBase<T>::Change)>& SorterBase<T>::signal_changed()
{
    return signal_changed_;
}

template<typename T>
void SorterBase<T>::changed(Change change)
{
    signal_changed_.emit(change);
}

#endif
