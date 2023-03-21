// This file Copyright © 2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "FilterUI.h"

#include <cstdint> // uint64_t
#include <map>
#include <unordered_map>

#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>

#include "Application.h"
#include "FaviconCache.h"
#include "Filters.h"
#include "Prefs.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "FilterBarComboBox.h"
#include "FilterUIDelegate.h"

enum
{
    ACTIVITY_ROLE = FilterUI::UserRole,
    TRACKER_ROLE
};

QStandardItemModel* FilterUI::createActivityModel(QObject* object)
{
    auto* model = new QStandardItemModel(object);

    auto* row = new QStandardItem(object->tr("All"));
    row->setData(FilterMode::SHOW_ALL, ACTIVITY_ROLE);
    model->appendRow(row);

    model->appendRow(new QStandardItem); // separator
    FilterUIDelegate::setSeparator(model, model->index(1, 0));

    auto const& icons = IconCache::get();

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("system-run")), object->tr("Active"));
    row->setData(FilterMode::SHOW_ACTIVE, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("go-down")), object->tr("Downloading"));
    row->setData(FilterMode::SHOW_DOWNLOADING, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("go-up")), object->tr("Seeding"));
    row->setData(FilterMode::SHOW_SEEDING, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("media-playback-pause")), object->tr("Paused"));
    row->setData(FilterMode::SHOW_PAUSED, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("dialog-ok")), object->tr("Finished"));
    row->setData(FilterMode::SHOW_FINISHED, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("view-refresh")), object->tr("Verifying"));
    row->setData(FilterMode::SHOW_VERIFYING, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("process-stop")), object->tr("Error"));
    row->setData(FilterMode::SHOW_ERROR, ACTIVITY_ROLE);
    model->appendRow(row);

    return model;
}

namespace
{

QString getCountString(size_t n)
{
    return QStringLiteral("%L1").arg(n);
}

Torrent::fields_t constexpr TrackerFields = {
    static_cast<uint64_t>(1) << Torrent::TRACKER_STATS,
};

auto constexpr ActivityFields = FilterMode::TorrentFields;

} // namespace

void FilterUI::refreshTrackers()
{
    enum
    {
        ROW_TOTALS = 0,
        ROW_SEPARATOR,
        ROW_FIRST_TRACKER
    };

    auto torrents_per_sitename = std::unordered_map<QString, int>{};
    for (auto const& tor : torrents_.torrents())
    {
        for (auto const& sitename : tor->sitenames())
        {
            ++torrents_per_sitename[sitename];
        }
    }

    // update the "All" row
    auto const num_trackers = torrents_per_sitename.size();
    auto* item = tracker_model_->item(ROW_TOTALS);
    item->setData(static_cast<int>(num_trackers), CountRole);
    item->setData(getCountString(num_trackers), CountStringRole);

    auto update_tracker_item = [](QStandardItem* i, auto const& it)
    {
        auto const& [sitename, count] = *it;
        auto const display_name = FaviconCache::getDisplayName(sitename);
        auto const icon = trApp->faviconCache().find(sitename);

        i->setData(display_name, Qt::DisplayRole);
        i->setData(display_name, TRACKER_ROLE);
        i->setData(getCountString(static_cast<size_t>(count)), CountStringRole);
        i->setData(icon, Qt::DecorationRole);
        i->setData(static_cast<int>(count), CountRole);

        return i;
    };

    auto new_trackers = std::map<QString, int>(torrents_per_sitename.begin(), torrents_per_sitename.end());
    auto old_it = sitename_counts_.cbegin();
    auto new_it = new_trackers.cbegin();
    auto const old_end = sitename_counts_.cend();
    auto const new_end = new_trackers.cend();
    bool any_added = false;
    int row = ROW_FIRST_TRACKER;

    while ((old_it != old_end) || (new_it != new_end))
    {
        if ((old_it == old_end) || ((new_it != new_end) && (old_it->first > new_it->first)))
        {
            tracker_model_->insertRow(row, update_tracker_item(new QStandardItem(1), new_it));
            any_added = true;
            ++new_it;
            ++row;
        }
        else if ((new_it == new_end) || ((old_it != old_end) && (old_it->first < new_it->first)))
        {
            tracker_model_->removeRow(row);
            ++old_it;
        }
        else // update
        {
            update_tracker_item(tracker_model_->item(row), new_it);
            ++old_it;
            ++new_it;
            ++row;
        }
    }

    if (any_added) // the one added might match our filter...
    {
        refreshPref(Prefs::FILTER_TRACKERS);
    }

    sitename_counts_.swap(new_trackers);
}

FilterUI::FilterUI(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent)
    : QWidget(parent)
    , prefs_(prefs)
    , torrents_(torrents)
    , filter_(filter)
    , is_bootstrapping_(true)
{
    connect(&prefs_, &Prefs::changed, this, &FilterUI::refreshPref);
    connect(&torrents_, &TorrentModel::modelReset, this, &FilterUI::recountAllSoon);
    connect(&torrents_, &TorrentModel::rowsInserted, this, &FilterUI::recountAllSoon);
    connect(&torrents_, &TorrentModel::rowsRemoved, this, &FilterUI::recountAllSoon);
    connect(&torrents_, &TorrentModel::torrentsChanged, this, &FilterUI::onTorrentsChanged);
    connect(&recount_timer_, &QTimer::timeout, this, &FilterUI::recount);
    connect(&trApp->faviconCache(), &FaviconCache::pixmapReady, this, &FilterUI::recountTrackersSoon);
}

void FilterUI::toggleUI(bool checked)
{
    prefs_.toggleBool(Prefs::FILTERBAR_ALT_VIEW);
}

void FilterUI::onTextChanged(QString const& str)
{
    if (!is_bootstrapping_)
    {
        prefs_.set(Prefs::FILTER_TEXT, str.trimmed());
    }
}

void FilterUI::onTorrentsChanged(torrent_ids_t const& ids, Torrent::fields_t const& changed_fields)
{
    Q_UNUSED(ids)

    if ((changed_fields & TrackerFields).any())
    {
        recountTrackersSoon();
    }

    if ((changed_fields & ActivityFields).any())
    {
        recountActivitySoon();
    }
}

void FilterUI::recountSoon(Pending const& fields)
{
    pending_ |= fields;

    if (!recount_timer_.isActive())
    {
        recount_timer_.setSingleShot(true);
        recount_timer_.start(800);
    }
}
