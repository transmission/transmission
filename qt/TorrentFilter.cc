// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <optional>

#include "Filters.h"
#include "Prefs.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "Utils.h"

TorrentFilter::TorrentFilter(Prefs const& prefs)
    : prefs_(prefs)
{
    connect(&prefs_, &Prefs::changed, this, &TorrentFilter::onPrefChanged);
    connect(&refilter_timer_, &QTimer::timeout, this, &TorrentFilter::refilter);

    setDynamicSortFilter(true);

    refilter();
}

/***
****
***/

void TorrentFilter::onPrefChanged(int key)
{
    // For refiltering nearly immediately. Used to debounce batched prefs changes.
    static int const FastMSec = 50;

    // For waiting a little longer. Useful when user is typing the filter text.
    static int const SlowMSec = 500;

    std::optional<int> msec;
    switch (key)
    {
    case Prefs::FILTER_TEXT:
        // special case for isEmpty: user probably hit the 'clear' button
        msec = prefs_.getString(key).isEmpty() ? FastMSec : SlowMSec;
        break;

    case Prefs::FILTER_MODE:
    case Prefs::FILTER_TRACKERS:
        msec = FastMSec;
        break;
    }

    // if this pref change affects filtering, ensure that a refilter is queued
    if (msec && !refilter_timer_.isActive())
    {
        refilter_timer_.setSingleShot(true);
        refilter_timer_.start(*msec);
    }
}

void TorrentFilter::refilter()
{
    invalidate();
}

bool TorrentFilter::filterAcceptsRow(int source_row, QModelIndex const& source_parent) const
{
    QModelIndex const child_index = sourceModel()->index(source_row, 0, source_parent);
    auto const& tor = *child_index.model()->data(child_index, TorrentModel::TorrentRole).value<Torrent const*>();
    bool accepts = true;

    if (accepts)
    {
        auto const m = prefs_.get<FilterMode>(Prefs::FILTER_MODE);
        accepts = m.test(tor);
    }

    if (accepts)
    {
        auto const display_name = prefs_.getString(Prefs::FILTER_TRACKERS);
        accepts = display_name.isEmpty() || tor.includesTracker(display_name.toLower());
    }

    if (accepts)
    {
        auto const text = prefs_.getString(Prefs::FILTER_TEXT);
        accepts = text.isEmpty() || tor.name().contains(text, Qt::CaseInsensitive) ||
            tor.hash().toString().contains(text, Qt::CaseInsensitive);
    }

    return accepts;
}

std::array<int, FilterMode::NUM_MODES> TorrentFilter::countTorrentsPerMode() const
{
    std::array<int, FilterMode::NUM_MODES> torrent_counts = {};

    for (auto const& tor : dynamic_cast<TorrentModel*>(sourceModel())->torrents())
    {
        for (int mode = 0; mode < FilterMode::NUM_MODES; ++mode)
        {
            if (FilterMode::test(*tor, mode))
            {
                ++torrent_counts[mode];
            }
        }
    }

    return torrent_counts;
}
