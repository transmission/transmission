// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <glibmm.h>
#include <gtkmm.h>

#include "Utils.h"

class Torrent;

class TorrentSorter : public IF_GTKMM4(Gtk::Sorter, Glib::Object)
{
    using CompareFunc = int (*)(Torrent const&, Torrent const&);

#if !GTKMM_CHECK_VERSION(4, 0, 0)
    enum class Change{
        DIFFERENT,
        INVERTED,
        LESS_STRICT,
        MORE_STRICT,
    };
#endif

public:
    void set_mode(std::string_view mode);
    void set_reversed(bool is_reversed);

    int compare(Torrent const& lhs, Torrent const& rhs) const;

    void update(Torrent::ChangeFlags changes);

#if !GTKMM_CHECK_VERSION(4, 0, 0)
    sigc::signal<void()>& signal_changed();
#endif

    static Glib::RefPtr<TorrentSorter> create();

protected:
#if GTKMM_CHECK_VERSION(4, 0, 0)
    Gtk::Ordering compare_vfunc(gpointer lhs, gpointer rhs) override;
    Order get_order_vfunc() override;
#else
    void changed(Change change);
#endif

private:
    TorrentSorter();

private:
    CompareFunc compare_func_ = nullptr;
    bool is_reversed_ = false;

#if !GTKMM_CHECK_VERSION(4, 0, 0)
    sigc::signal<void()> signal_changed_;
#endif
};
