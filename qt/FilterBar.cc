/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <map>
#include <unordered_map>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStandardItemModel>

#include "Application.h"
#include "FaviconCache.h"
#include "Filters.h"
#include "FilterBar.h"
#include "FilterBarComboBox.h"
#include "FilterBarComboBoxDelegate.h"
#include "Prefs.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "Utils.h"

enum
{
    ActivityRole = FilterBarComboBox::UserRole,
    TrackerRole
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
    row->setData(FilterMode::SHOW_ALL, ActivityRole);
    model->appendRow(row);

    model->appendRow(new QStandardItem); // separator
    delegate->setSeparator(model, model->index(1, 0));

    row = new QStandardItem(QIcon::fromTheme(QStringLiteral("system-run")), tr("Active"));
    row->setData(FilterMode::SHOW_ACTIVE, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QStringLiteral("go-down")), tr("Downloading"));
    row->setData(FilterMode::SHOW_DOWNLOADING, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QStringLiteral("go-up")), tr("Seeding"));
    row->setData(FilterMode::SHOW_SEEDING, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QStringLiteral("media-playback-pause")), tr("Paused"));
    row->setData(FilterMode::SHOW_PAUSED, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QStringLiteral("dialog-ok")), tr("Finished"));
    row->setData(FilterMode::SHOW_FINISHED, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Verifying"));
    row->setData(FilterMode::SHOW_VERIFYING, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QStringLiteral("process-stop")), tr("Error"));
    row->setData(FilterMode::SHOW_ERROR, ActivityRole);
    model->appendRow(row);

    c->setModel(model);
    return c;
}

/***
****
***/

namespace
{

QString getCountString(int n)
{
    return QStringLiteral("%L1").arg(n);
}

} // namespace

void FilterBar::refreshTrackers()
{
    enum
    {
        ROW_TOTALS = 0, ROW_SEPARATOR, ROW_FIRST_TRACKER
    };

    auto torrents_per_host = std::unordered_map<QString, int>{};
    for (auto const& tor : torrents_.torrents())
    {
        for (auto const& display_name : tor->trackerDisplayNames())
        {
            ++torrents_per_host[display_name];
        }
    }

    // update the "All" row
    auto const num_trackers = torrents_per_host.size();
    auto* item = tracker_model_->item(ROW_TOTALS);
    item->setData(int(num_trackers), FilterBarComboBox::CountRole);
    item->setData(getCountString(num_trackers), FilterBarComboBox::CountStringRole);

    auto updateTrackerItem = [](QStandardItem* i, auto const& it)
        {
            auto const& display_name = it->first;
            auto const& count = it->second;
            auto const icon = qApp->faviconCache().find(FaviconCache::getKey(display_name));
            i->setData(display_name, Qt::DisplayRole);
            i->setData(display_name, TrackerRole);
            i->setData(getCountString(count), FilterBarComboBox::CountStringRole);
            i->setData(icon, Qt::DecorationRole);
            i->setData(int(count), FilterBarComboBox::CountRole);
            return i;
        };

    auto new_trackers = std::map<QString, int>(torrents_per_host.begin(), torrents_per_host.end());
    auto old_it = tracker_counts_.cbegin();
    auto new_it = new_trackers.cbegin();
    auto const old_end = tracker_counts_.cend();
    auto const new_end = new_trackers.cend();
    bool any_added = false;
    int row = ROW_FIRST_TRACKER;

    while ((old_it != old_end) || (new_it != new_end))
    {
        if ((old_it == old_end) || ((new_it != new_end) && (old_it->first > new_it->first)))
        {
            tracker_model_->insertRow(row, updateTrackerItem(new QStandardItem(1), new_it));
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
            updateTrackerItem(tracker_model_->item(row), new_it);
            ++old_it;
            ++new_it;
            ++row;
        }
    }

    if (any_added) // the one added might match our filter...
    {
        refreshPref(Prefs::FILTER_TRACKERS);
    }

    tracker_counts_.swap(new_trackers);
}

FilterBarComboBox* FilterBar::createTrackerCombo(QStandardItemModel* model)
{
    auto* c = new FilterBarComboBox(this);
    auto* delegate = new FilterBarComboBoxDelegate(this, c);
    c->setItemDelegate(delegate);

    auto* row = new QStandardItem(tr("All"));
    row->setData(QString(), TrackerRole);
    int const count = torrents_.rowCount();
    row->setData(count, FilterBarComboBox::CountRole);
    row->setData(getCountString(count), FilterBarComboBox::CountStringRole);
    model->appendRow(row);

    model->appendRow(new QStandardItem); // separator
    delegate->setSeparator(model, model->index(1, 0));

    c->setModel(model);
    return c;
}

/***
****
***/

FilterBar::FilterBar(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent) :
    QWidget(parent),
    prefs_(prefs),
    torrents_(torrents),
    filter_(filter),
    recount_timer_(new QTimer(this)),
    is_bootstrapping_(true)
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(3, 3, 3, 3);

    count_label_ = new QLabel(tr("Show:"), this);
    h->addWidget(count_label_);

    activity_combo_ = createActivityCombo();
    h->addWidget(activity_combo_);

    tracker_model_ = new QStandardItemModel(this);
    tracker_combo_ = createTrackerCombo(tracker_model_);
    h->addWidget(tracker_combo_);

    h->addStretch();

    line_edit_ = new QLineEdit(this);
    line_edit_->setClearButtonEnabled(true);
    line_edit_->setPlaceholderText(tr("Search..."));
    line_edit_->setMaximumWidth(250);
    h->addWidget(line_edit_, 1);
    connect(line_edit_, SIGNAL(textChanged(QString)), this, SLOT(onTextChanged(QString)));

    // listen for changes from the other players
    connect(&prefs_, SIGNAL(changed(int)), this, SLOT(refreshPref(int)));
    connect(activity_combo_, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivityIndexChanged(int)));
    connect(tracker_combo_, SIGNAL(currentIndexChanged(int)), this, SLOT(onTrackerIndexChanged(int)));
    connect(&torrents_, SIGNAL(modelReset()), this, SLOT(recountSoon()));
    connect(&torrents_, SIGNAL(rowsInserted(QModelIndex, int, int)), this, SLOT(recountSoon()));
    connect(&torrents_, SIGNAL(rowsRemoved(QModelIndex, int, int)), this, SLOT(recountSoon()));
    connect(&torrents_, SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(recountSoon()));
    connect(recount_timer_, SIGNAL(timeout()), this, SLOT(recount()));

    recountSoon();
    refreshTrackers();
    is_bootstrapping_ = false;

    // initialize our state
    for (int const key : { Prefs::FILTER_MODE, Prefs::FILTER_TRACKERS })
    {
        refreshPref(key);
    }
}

FilterBar::~FilterBar()
{
    delete recount_timer_;
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
            QAbstractItemModel* model = activity_combo_->model();
            QModelIndexList indices = model->match(model->index(0, 0), ActivityRole, m.mode());
            activity_combo_->setCurrentIndex(indices.isEmpty() ? 0 : indices.first().row());
            break;
        }

    case Prefs::FILTER_TRACKERS:
        {
            auto const display_name = prefs_.getString(key);
            auto rows = tracker_model_->findItems(display_name);
            if (!rows.isEmpty())
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
        QString str;
        bool const is_tracker = !tracker_combo_->itemData(i, TrackerRole).toString().isEmpty();

        if (!is_tracker)
        {
            // show all
        }
        else
        {
            str = tracker_combo_->itemData(i, TrackerRole).toString();
            int const pos = str.lastIndexOf(QLatin1Char('.'));

            if (pos >= 0)
            {
                str.truncate(pos + 1);
            }
        }

        prefs_.set(Prefs::FILTER_TRACKERS, str);
    }
}

void FilterBar::onActivityIndexChanged(int i)
{
    if (!is_bootstrapping_)
    {
        FilterMode const mode = activity_combo_->itemData(i, ActivityRole).toInt();
        prefs_.set(Prefs::FILTER_MODE, mode);
    }
}

/***
****
***/

void FilterBar::recountSoon()
{
    if (!recount_timer_->isActive())
    {
        recount_timer_->setSingleShot(true);
        recount_timer_->start(800);
    }
}

void FilterBar::recount()
{
    QAbstractItemModel* model = activity_combo_->model();

    int torrents_per_mode[FilterMode::NUM_MODES] = {};
    filter_.countTorrentsPerMode(torrents_per_mode);

    for (int row = 0, n = model->rowCount(); row < n; ++row)
    {
        QModelIndex index = model->index(row, 0);
        int const mode = index.data(ActivityRole).toInt();
        int const count = torrents_per_mode[mode];
        model->setData(index, count, FilterBarComboBox::CountRole);
        model->setData(index, getCountString(count), FilterBarComboBox::CountStringRole);
    }

    refreshTrackers();
}
