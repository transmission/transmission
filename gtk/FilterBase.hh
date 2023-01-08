// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "FilterBase.h"

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include "Utils.h"
#endif

template<typename T>
bool FilterBase<T>::matches_all() const
{
    return false;
}

template<typename T>
bool FilterBase<T>::matches_none() const
{
    return false;
}

#if GTKMM_CHECK_VERSION(4, 0, 0)

template<typename T>
bool FilterBase<T>::match_vfunc(Glib::RefPtr<Glib::ObjectBase> const& object)
{
    auto const concrete_object = gtr_ptr_dynamic_cast<T>(object);
    g_return_val_if_fail(concrete_object != nullptr, false);

    return match(*concrete_object);
}

template<typename T>
typename FilterBase<T>::Match FilterBase<T>::get_strictness_vfunc()
{
    if (matches_all())
    {
        return Match::ALL;
    }

    if (matches_none())
    {
        return Match::NONE;
    }

    return Match::SOME;
}

#else

template<typename T>
sigc::signal<void(typename FilterBase<T>::Change)>& FilterBase<T>::signal_changed()
{
    return signal_changed_;
}

template<typename T>
void FilterBase<T>::changed(Change change)
{
    signal_changed_.emit(change);
}

#endif
