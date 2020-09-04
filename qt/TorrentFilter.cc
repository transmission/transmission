/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <optional>

#include "Filters.h"
#include "Prefs.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "Utils.h"

TorrentFilter::TorrentFilter(Prefs const& prefs) :
    prefs_(prefs),
    tracker_key_(FaviconCache::getKey(prefs_.getString(Prefs::FILTER_TRACKERS))),
    filter_mode_(prefs_.get<FilterMode>(Prefs::FILTER_MODE))
{
    connect(&prefs_, &Prefs::changed, this, &TorrentFilter::onPrefChanged);
    connect(&refilter_timer_, &QTimer::timeout, this, &TorrentFilter::refilter);

    setDynamicSortFilter(true);

    refilter();
}

Torrent const* TorrentFilter::torrent(QModelIndex const& index) const
{
    return static_cast<TorrentModel*>(sourceModel())->torrent(index);
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

    case Prefs::FILTER_TRACKERS:
        tracker_key_ = FaviconCache::getKey(prefs_.getString(key));
        msec = FastMSec;
        break;

    case Prefs::FILTER_MODE:
        filter_mode_ = prefs_.get<FilterMode>(key);
        msec = FastMSec;
        break;

    case Prefs::SORT_MODE:
    case Prefs::SORT_REVERSED:
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
    sort(0, prefs_.getBool(Prefs::SORT_REVERSED) ? Qt::AscendingOrder : Qt::DescendingOrder);
}

/***
****
***/

namespace
{

template<typename T>
int compare(T const a, T const b)
{
    if (a < b)
    {
        return -1;
    }

    if (b < a)
    {
        return 1;
    }

    return 0;
}

} // namespace

bool TorrentFilter::lessThan(QModelIndex const& left, QModelIndex const& right) const
{
    int val = 0;
    auto source_model = static_cast<TorrentModel*>(sourceModel());
    auto const* const a = source_model->torrent(left);
    auto const* const b = source_model->torrent(right);

    switch (prefs_.get<SortMode>(Prefs::SORT_MODE).mode())
    {
    case SortMode::SORT_BY_QUEUE:
        if (val == 0)
        {
            val = -compare(a->queuePosition(), b->queuePosition());
        }

        break;

    case SortMode::SORT_BY_SIZE:
        if (val == 0)
        {
            val = compare(a->sizeWhenDone(), b->sizeWhenDone());
        }

        break;

    case SortMode::SORT_BY_AGE:
        if (val == 0)
        {
            val = compare(a->dateAdded(), b->dateAdded());
        }

        break;

    case SortMode::SORT_BY_ID:
        if (val == 0)
        {
            val = compare(a->id(), b->id());
        }

        break;

    case SortMode::SORT_BY_ACTIVITY:
        if (val == 0)
        {
            val = compare(a->downloadSpeed() + a->uploadSpeed(), b->downloadSpeed() + b->uploadSpeed());
        }

        if (val == 0)
        {
            val = compare(a->peersWeAreUploadingTo() + a->webseedsWeAreDownloadingFrom(),
                b->peersWeAreUploadingTo() + b->webseedsWeAreDownloadingFrom());
        }

        [[fallthrough]];

    case SortMode::SORT_BY_STATE:
        if (val == 0)
        {
            val = -compare(a->isPaused(), b->isPaused());
        }

        if (val == 0)
        {
            val = compare(a->getActivity(), b->getActivity());
        }

        if (val == 0)
        {
            val = -compare(a->queuePosition(), b->queuePosition());
        }

        if (val == 0)
        {
            val = compare(a->hasError(), b->hasError());
        }

        [[fallthrough]];

    case SortMode::SORT_BY_PROGRESS:
        if (val == 0)
        {
            val = compare(a->metadataPercentDone(), b->metadataPercentDone());
        }

        if (val == 0)
        {
            val = compare(a->percentComplete(), b->percentComplete());
        }

        if (val == 0)
        {
            val = a->compareSeedRatio(*b);
        }

        if (val == 0)
        {
            val = -compare(a->queuePosition(), b->queuePosition());
        }

        [[fallthrough]];

    case SortMode::SORT_BY_RATIO:
        if (val == 0)
        {
            val = a->compareRatio(*b);
        }

        break;

    case SortMode::SORT_BY_ETA:
        if (val == 0)
        {
            val = a->compareETA(*b);
        }

        break;

    default:
        break;
    }

    if (val == 0)
    {
        val = -a->name().compare(b->name(), Qt::CaseInsensitive);
    }

    if (val == 0)
    {
        val = compare(a->hashString(), b->hashString());
    }

    return val < 0;
}

/***
****
***/

bool TorrentFilter::filterAcceptsRow(int source_row, QModelIndex const& source_parent) const
{
    auto const child_index = sourceModel()->index(source_row, 0, source_parent);
    auto const& tor = *torrent(child_index);
    bool accepts = true;

    if (accepts)
    {
        accepts = filter_mode_.test(tor);
    }

    if (accepts)
    {
        accepts = tracker_key_.isEmpty() || tor.includesTracker(tracker_key_);
    }

    if (accepts)
    {
        auto const text = prefs_.getString(Prefs::FILTER_TEXT);
        accepts = text.isEmpty() ||
            tor.name().contains(text, Qt::CaseInsensitive) ||
            tor.hashString().contains(text, Qt::CaseInsensitive);
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
