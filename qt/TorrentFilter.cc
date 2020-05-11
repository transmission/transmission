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
    myPrefs(prefs)
{
    connect(&myPrefs, &Prefs::changed, this, &TorrentFilter::onPrefChanged);
    connect(&myRefilterTimer, &QTimer::timeout, this, &TorrentFilter::refilter);

    setDynamicSortFilter(true);

    refilter();
}

TorrentFilter::~TorrentFilter()
{
}

/***
****
***/

void TorrentFilter::onPrefChanged(int key)
{
    // For refiltering nearly immediately. Used to debounce batched prefs changes.
    static int const fast_msec = 50;

    // For waiting a little longer. Useful when user is typing the filter text.
    static int const slow_msec = 500;

    std::optional<int> msec;
    switch (key)
    {
    case Prefs::FILTER_TEXT:
        // special case for isEmpty: user probably hit the 'clear' button
        msec = myPrefs.getString(key).isEmpty() ? fast_msec : slow_msec;
        break;

    case Prefs::FILTER_MODE:
    case Prefs::FILTER_TRACKERS:
    case Prefs::SORT_MODE:
    case Prefs::SORT_REVERSED:
        msec = fast_msec;
        break;
    }

    // if this pref change affects filtering, ensure that a refilter is queued
    if (msec && !myRefilterTimer.isActive())
    {
        myRefilterTimer.setSingleShot(true);
        myRefilterTimer.start(*msec);
    }
}

void TorrentFilter::refilter()
{
    invalidate();
    sort(0, myPrefs.getBool(Prefs::SORT_REVERSED) ? Qt::AscendingOrder : Qt::DescendingOrder);
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
    Torrent const* a = sourceModel()->data(left, TorrentModel::TorrentRole).value<Torrent const*>();
    Torrent const* b = sourceModel()->data(right, TorrentModel::TorrentRole).value<Torrent const*>();

    switch (myPrefs.get<SortMode>(Prefs::SORT_MODE).mode())
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

    // fall through

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

    // fall through

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

    // fall through

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

bool TorrentFilter::trackerFilterAcceptsTorrent(Torrent const* tor, QString const& tracker) const
{
    return tracker.isEmpty() || tor->hasTrackerSubstring(tracker);
}

bool TorrentFilter::activityFilterAcceptsTorrent(Torrent const* tor, FilterMode const& m) const
{
    bool accepts;

    switch (m.mode())
    {
    case FilterMode::SHOW_ACTIVE:
        accepts = tor->peersWeAreUploadingTo() > 0 || tor->peersWeAreDownloadingFrom() > 0 || tor->isVerifying();
        break;

    case FilterMode::SHOW_DOWNLOADING:
        accepts = tor->isDownloading() || tor->isWaitingToDownload();
        break;

    case FilterMode::SHOW_SEEDING:
        accepts = tor->isSeeding() || tor->isWaitingToSeed();
        break;

    case FilterMode::SHOW_PAUSED:
        accepts = tor->isPaused();
        break;

    case FilterMode::SHOW_FINISHED:
        accepts = tor->isFinished();
        break;

    case FilterMode::SHOW_VERIFYING:
        accepts = tor->isVerifying() || tor->isWaitingToVerify();
        break;

    case FilterMode::SHOW_ERROR:
        accepts = tor->hasError();
        break;

    default: // FilterMode::SHOW_ALL
        accepts = true;
        break;
    }

    return accepts;
}

bool TorrentFilter::filterAcceptsRow(int sourceRow, QModelIndex const& sourceParent) const
{
    QModelIndex childIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    Torrent const* tor = childIndex.model()->data(childIndex, TorrentModel::TorrentRole).value<Torrent const*>();
    bool accepts = true;

    if (accepts)
    {
        FilterMode const m = myPrefs.get<FilterMode>(Prefs::FILTER_MODE);
        accepts = activityFilterAcceptsTorrent(tor, m);
    }

    if (accepts)
    {
        QString const trackers = myPrefs.getString(Prefs::FILTER_TRACKERS);
        accepts = trackerFilterAcceptsTorrent(tor, trackers);
    }

    if (accepts)
    {
        QString const text = myPrefs.getString(Prefs::FILTER_TEXT);

        if (!text.isEmpty())
        {
            accepts = tor->name().contains(text, Qt::CaseInsensitive);
        }
    }

    return accepts;
}

void TorrentFilter::countTorrentsPerMode(int* setmeCounts) const
{
    std::fill_n(setmeCounts, static_cast<std::size_t>(FilterMode::NUM_MODES), 0);

    for (auto const& tor : dynamic_cast<TorrentModel*>(sourceModel())->torrents())
    {
        for (int mode = 0; mode < FilterMode::NUM_MODES; ++mode)
        {
            if (activityFilterAcceptsTorrent(tor, mode))
            {
                ++setmeCounts[mode];
            }
        }
    }
}
