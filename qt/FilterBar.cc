// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FilterBar.h"

#include <cstdint> // uint64_t
#include <map>
#include <unordered_map>
#include <utility>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStandardItemModel>

#include "Application.h"
#include "FaviconCache.h"
#include "FilterBarComboBox.h"
#include "FilterBarComboBoxDelegate.h"
#include "Filters.h"
#include "IconCache.h"
#include "Prefs.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "Utils.h"

enum
{
    ACTIVITY_ROLE = FilterBarComboBox::UserRole,
    TRACKER_ROLE
};

/***
****
***/

FilterBarComboBox* FilterBar::createActivityCombo()
{
    auto* c = new FilterBarComboBox(this);
    auto* delegate = new FilterBarComboBoxDelegate(this, c);
    c->setItemDelegate(delegate);

    auto* model = new QStandardItemModel(this);

    auto* row = new QStandardItem(tr("All"));
    row->setData(FilterMode::SHOW_ALL, ACTIVITY_ROLE);
    model->appendRow(row);

    model->appendRow(new QStandardItem); // separator
    FilterBarComboBoxDelegate::setSeparator(model, model->index(1, 0));

    auto const& icons = IconCache::get();

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("system-run")), tr("Active"));
    row->setData(FilterMode::SHOW_ACTIVE, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("go-down")), tr("Downloading"));
    row->setData(FilterMode::SHOW_DOWNLOADING, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("go-up")), tr("Seeding"));
    row->setData(FilterMode::SHOW_SEEDING, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("media-playback-pause")), tr("Paused"));
    row->setData(FilterMode::SHOW_PAUSED, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("dialog-ok")), tr("Finished"));
    row->setData(FilterMode::SHOW_FINISHED, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("view-refresh")), tr("Verifying"));
    row->setData(FilterMode::SHOW_VERIFYING, ACTIVITY_ROLE);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("process-stop")), tr("Error"));
    row->setData(FilterMode::SHOW_ERROR, ACTIVITY_ROLE);
    model->appendRow(row);

    c->setModel(model);
    return c;
}

/***
****
***/

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

void FilterBar::refreshTrackers()
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
    item->setData(static_cast<int>(num_trackers), FilterBarComboBox::CountRole);
    item->setData(getCountString(num_trackers), FilterBarComboBox::CountStringRole);

    auto update_tracker_item = [](QStandardItem* i, auto const& it)
    {
        auto const& [sitename, count] = *it;
        auto const display_name = FaviconCache::getDisplayName(sitename);
        auto const icon = trApp->faviconCache().find(sitename);

        i->setData(display_name, Qt::DisplayRole);
        i->setData(display_name, TRACKER_ROLE);
        i->setData(getCountString(static_cast<size_t>(count)), FilterBarComboBox::CountStringRole);
        i->setData(icon, Qt::DecorationRole);
        i->setData(static_cast<int>(count), FilterBarComboBox::CountRole);

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

FilterBarComboBox* FilterBar::createTrackerCombo(QStandardItemModel* model)
{
    auto* c = new FilterBarComboBox(this);
    auto* delegate = new FilterBarComboBoxDelegate(this, c);
    c->setItemDelegate(delegate);

    auto* row = new QStandardItem(tr("All"));
    row->setData(QString(), TRACKER_ROLE);
    int const count = torrents_.rowCount();
    row->setData(count, FilterBarComboBox::CountRole);
    row->setData(getCountString(static_cast<size_t>(count)), FilterBarComboBox::CountStringRole);
    model->appendRow(row);

    model->appendRow(new QStandardItem); // separator
    FilterBarComboBoxDelegate::setSeparator(model, model->index(1, 0));

    c->setModel(model);
    return c;
}

/***
****
***/

FilterBar::FilterBar(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent)
    : QWidget(parent)
    , prefs_(prefs)
    , torrents_(torrents)
    , filter_(filter)
    , is_bootstrapping_(true)
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(3, 3, 3, 3);

    count_label_ = new QLabel(tr("Show:"), this);
    h->addWidget(count_label_);

    h->addWidget(activity_combo_);

    tracker_combo_ = createTrackerCombo(tracker_model_);
    h->addWidget(tracker_combo_);

    h->addStretch();

    line_edit_->setClearButtonEnabled(true);
    line_edit_->setPlaceholderText(tr("Search…"));
    line_edit_->setMaximumWidth(250);
    h->addWidget(line_edit_, 1);
    connect(line_edit_, &QLineEdit::textChanged, this, &FilterBar::onTextChanged);

    // listen for changes from the other players
    connect(&prefs_, &Prefs::changed, this, &FilterBar::refreshPref);
    connect(activity_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterBar::onActivityIndexChanged);
    connect(tracker_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterBar::onTrackerIndexChanged);
    connect(&torrents_, &TorrentModel::modelReset, this, &FilterBar::recountAllSoon);
    connect(&torrents_, &TorrentModel::rowsInserted, this, &FilterBar::recountAllSoon);
    connect(&torrents_, &TorrentModel::rowsRemoved, this, &FilterBar::recountAllSoon);
    connect(&torrents_, &TorrentModel::torrentsChanged, this, &FilterBar::onTorrentsChanged);
    connect(&recount_timer_, &QTimer::timeout, this, &FilterBar::recount);
    connect(&trApp->faviconCache(), &FaviconCache::pixmapReady, this, &FilterBar::recountTrackersSoon);

    recountAllSoon();
    is_bootstrapping_ = false; // NOLINT cppcoreguidelines-prefer-member-initializer

    // initialize our state
    for (int const key : { Prefs::FILTER_MODE, Prefs::FILTER_TRACKERS })
    {
        refreshPref(key);
    }
}

/***
****
***/

void FilterBar::clear()
{
    activity_combo_->setCurrentIndex(0);
    tracker_combo_->setCurrentIndex(0);
    line_edit_->clear();
}

/***
****
***/

void FilterBar::refreshPref(int key)
{
    switch (key)
    {
    case Prefs::FILTER_MODE:
        {
            auto const m = prefs_.get<FilterMode>(key);
            QAbstractItemModel const* const model = activity_combo_->model();
            QModelIndexList indices = model->match(model->index(0, 0), ACTIVITY_ROLE, m.mode());
            activity_combo_->setCurrentIndex(indices.isEmpty() ? 0 : indices.first().row());
            break;
        }

    case Prefs::FILTER_TRACKERS:
        {
            auto const display_name = prefs_.getString(key);

            if (auto rows = tracker_model_->findItems(display_name); !rows.isEmpty())
            {
                tracker_combo_->setCurrentIndex(rows.front()->row());
            }
            else // hm, we don't seem to have this tracker anymore...
            {
                bool const is_bootstrapping = tracker_model_->rowCount() <= 2;

                if (!is_bootstrapping)
                {
                    prefs_.set(key, QString());
                }
            }

            break;
        }
    }
}

void FilterBar::onTorrentsChanged(torrent_ids_t const& ids, Torrent::fields_t const& changed_fields)
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

void FilterBar::onTextChanged(QString const& str)
{
    if (!is_bootstrapping_)
    {
        prefs_.set(Prefs::FILTER_TEXT, str.trimmed());
    }
}

void FilterBar::onTrackerIndexChanged(int i)
{
    if (!is_bootstrapping_)
    {
        auto const display_name = tracker_combo_->itemData(i, TRACKER_ROLE).toString();
        prefs_.set(Prefs::FILTER_TRACKERS, display_name);
    }
}

void FilterBar::onActivityIndexChanged(int i)
{
    if (!is_bootstrapping_)
    {
        auto const mode = FilterMode(activity_combo_->itemData(i, ACTIVITY_ROLE).toInt());
        prefs_.set(Prefs::FILTER_MODE, mode);
    }
}

/***
****
***/

void FilterBar::recountSoon(Pending const& fields)
{
    pending_ |= fields;

    if (!recount_timer_.isActive())
    {
        recount_timer_.setSingleShot(true);
        recount_timer_.start(800);
    }
}

void FilterBar::recount()
{
    QAbstractItemModel* model = activity_combo_->model();

    decltype(pending_) pending = {};
    std::swap(pending_, pending);

    if (pending[ACTIVITY])
    {
        auto const torrents_per_mode = filter_.countTorrentsPerMode();

        for (int row = 0, n = model->rowCount(); row < n; ++row)
        {
            auto const index = model->index(row, 0);
            auto const mode = index.data(ACTIVITY_ROLE).toInt();
            auto const count = torrents_per_mode[mode];
            model->setData(index, count, FilterBarComboBox::CountRole);
            model->setData(index, getCountString(static_cast<size_t>(count)), FilterBarComboBox::CountStringRole);
        }
    }

    if (pending[TRACKERS])
    {
        refreshTrackers();
    }
}
