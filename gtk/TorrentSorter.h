// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "SorterBase.h"
#include "Torrent.h"

#include <glibmm/refptr.h>

class TorrentSorter : public SorterBase<Torrent>
{
    using CompareFunc = int (*)(Torrent const&, Torrent const&);

public:
    void set_mode(std::string_view mode);
    void set_reversed(bool is_reversed);

    // SorterBase<Torrent>
    int compare(Torrent const& lhs, Torrent const& rhs) const override;

    void update(Torrent::ChangeFlags changes);

    static Glib::RefPtr<TorrentSorter> create();

private:
    TorrentSorter();

private:
    CompareFunc compare_func_ = nullptr;
    bool is_reversed_ = false;
};
