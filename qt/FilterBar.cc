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
    FilterBarComboBox* c = new FilterBarComboBox(this);
    FilterBarComboBoxDelegate* delegate = new FilterBarComboBoxDelegate(this, c);
    c->setItemDelegate(delegate);

    QStandardItemModel* model = new QStandardItemModel(this);

    QStandardItem* row = new QStandardItem(tr("All"));
    row->setData(FilterMode::SHOW_ALL, ActivityRole);
    model->appendRow(row);

    model->appendRow(new QStandardItem); // separator
    delegate->setSeparator(model, model->index(1, 0));

    row = new QStandardItem(QIcon::fromTheme(QLatin1String("system-run")), tr("Active"));
    row->setData(FilterMode::SHOW_ACTIVE, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QLatin1String("go-down")), tr("Downloading"));
    row->setData(FilterMode::SHOW_DOWNLOADING, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QLatin1String("go-up")), tr("Seeding"));
    row->setData(FilterMode::SHOW_SEEDING, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QLatin1String("media-playback-pause")), tr("Paused"));
    row->setData(FilterMode::SHOW_PAUSED, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QLatin1String("dialog-ok")), tr("Finished"));
    row->setData(FilterMode::SHOW_FINISHED, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QLatin1String("view-refresh")), tr("Verifying"));
    row->setData(FilterMode::SHOW_VERIFYING, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(QIcon::fromTheme(QLatin1String("process-stop")), tr("Error"));
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
    return QString::fromLatin1("%L1").arg(n);
}

} // namespace

void FilterBar::refreshTrackers()
{
    enum
    {
        ROW_TOTALS = 0, ROW_SEPARATOR, ROW_FIRST_TRACKER
    };

    auto torrentsPerHost = std::unordered_map<QString, int>{};
    for (auto const& tor : myTorrents.torrents())
    {
        for (auto const& displayName : tor->trackerDisplayNames())
        {
            ++torrentsPerHost[displayName];
        }
    }

    // update the "All" row
    auto const num_trackers = torrentsPerHost.size();
    auto item = myTrackerModel->item(ROW_TOTALS);
    item->setData(int(num_trackers), FilterBarComboBox::CountRole);
    item->setData(getCountString(num_trackers), FilterBarComboBox::CountStringRole);

    auto updateTrackerItem = [](QStandardItem* i, auto const& it)
        {
            auto const& displayName = it->first;
            auto const& count = it->second;
            auto const icon = qApp->faviconCache().find(FaviconCache::getKey(displayName));
            i->setData(displayName, Qt::DisplayRole);
            i->setData(displayName, TrackerRole);
            i->setData(getCountString(count), FilterBarComboBox::CountStringRole);
            i->setData(icon, Qt::DecorationRole);
            i->setData(int(count), FilterBarComboBox::CountRole);
            return i;
        };

    auto newTrackers = std::map<QString, int>(torrentsPerHost.begin(), torrentsPerHost.end());
    auto old_it = myTrackerCounts.cbegin();
    auto new_it = newTrackers.cbegin();
    auto const old_end = myTrackerCounts.cend();
    auto const new_end = newTrackers.cend();
    bool anyAdded = false;
    int row = ROW_FIRST_TRACKER;

    while ((old_it != old_end) || (new_it != new_end))
    {
        if ((old_it == old_end) || ((new_it != new_end) && (old_it->first > new_it->first)))
        {
            myTrackerModel->insertRow(row, updateTrackerItem(new QStandardItem(1), new_it));
            anyAdded = true;
            ++new_it;
            ++row;
        }
        else if ((new_it == new_end) || ((old_it != old_end) && (old_it->first < new_it->first)))
        {
            myTrackerModel->removeRow(row);
            ++old_it;
        }
        else // update
        {
            updateTrackerItem(myTrackerModel->item(row), new_it);
            ++old_it;
            ++new_it;
            ++row;
        }
    }

    if (anyAdded) // the one added might match our filter...
    {
        refreshPref(Prefs::FILTER_TRACKERS);
    }

    myTrackerCounts.swap(newTrackers);
}

FilterBarComboBox* FilterBar::createTrackerCombo(QStandardItemModel* model)
{
    FilterBarComboBox* c = new FilterBarComboBox(this);
    FilterBarComboBoxDelegate* delegate = new FilterBarComboBoxDelegate(this, c);
    c->setItemDelegate(delegate);

    QStandardItem* row = new QStandardItem(tr("All"));
    row->setData(QString(), TrackerRole);
    int const count = myTorrents.rowCount();
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
    myPrefs(prefs),
    myTorrents(torrents),
    myFilter(filter),
    myRecountTimer(new QTimer(this)),
    myIsBootstrapping(true)
{
    QHBoxLayout* h = new QHBoxLayout(this);
    h->setContentsMargins(3, 3, 3, 3);

    myCountLabel = new QLabel(tr("Show:"), this);
    h->addWidget(myCountLabel);

    myActivityCombo = createActivityCombo();
    h->addWidget(myActivityCombo);

    myTrackerModel = new QStandardItemModel(this);
    myTrackerCombo = createTrackerCombo(myTrackerModel);
    h->addWidget(myTrackerCombo);

    h->addStretch();

    myLineEdit = new QLineEdit(this);
    myLineEdit->setClearButtonEnabled(true);
    myLineEdit->setPlaceholderText(tr("Search..."));
    myLineEdit->setMaximumWidth(250);
    h->addWidget(myLineEdit, 1);
    connect(myLineEdit, SIGNAL(textChanged(QString)), this, SLOT(onTextChanged(QString)));

    // listen for changes from the other players
    connect(&myPrefs, SIGNAL(changed(int)), this, SLOT(refreshPref(int)));
    connect(myActivityCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onActivityIndexChanged(int)));
    connect(myTrackerCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(onTrackerIndexChanged(int)));
    connect(&myTorrents, SIGNAL(modelReset()), this, SLOT(recountSoon()));
    connect(&myTorrents, SIGNAL(rowsInserted(QModelIndex, int, int)), this, SLOT(recountSoon()));
    connect(&myTorrents, SIGNAL(rowsRemoved(QModelIndex, int, int)), this, SLOT(recountSoon()));
    connect(&myTorrents, SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(recountSoon()));
    connect(myRecountTimer, SIGNAL(timeout()), this, SLOT(recount()));

    recountSoon();
    refreshTrackers();
    myIsBootstrapping = false;

    // initialize our state
    for (int const key : { Prefs::FILTER_MODE, Prefs::FILTER_TRACKERS })
    {
        refreshPref(key);
    }
}

FilterBar::~FilterBar()
{
    delete myRecountTimer;
}

/***
****
***/

void FilterBar::clear()
{
    myActivityCombo->setCurrentIndex(0);
    myTrackerCombo->setCurrentIndex(0);
    myLineEdit->clear();
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
            FilterMode const m = myPrefs.get<FilterMode>(key);
            QAbstractItemModel* model = myActivityCombo->model();
            QModelIndexList indices = model->match(model->index(0, 0), ActivityRole, m.mode());
            myActivityCombo->setCurrentIndex(indices.isEmpty() ? 0 : indices.first().row());
            break;
        }

    case Prefs::FILTER_TRACKERS:
        {
            auto const displayName = myPrefs.getString(key);
            auto rows = myTrackerModel->findItems(displayName);
            if (!rows.isEmpty())
            {
                myTrackerCombo->setCurrentIndex(rows.front()->row());
            }
            else // hm, we don't seem to have this tracker anymore...
            {
                bool const isBootstrapping = myTrackerModel->rowCount() <= 2;

                if (!isBootstrapping)
                {
                    myPrefs.set(key, QString());
                }
            }

            break;
        }
    }
}

void FilterBar::onTextChanged(QString const& str)
{
    if (!myIsBootstrapping)
    {
        myPrefs.set(Prefs::FILTER_TEXT, str.trimmed());
    }
}

void FilterBar::onTrackerIndexChanged(int i)
{
    if (!myIsBootstrapping)
    {
        QString str;
        bool const isTracker = !myTrackerCombo->itemData(i, TrackerRole).toString().isEmpty();

        if (!isTracker)
        {
            // show all
        }
        else
        {
            str = myTrackerCombo->itemData(i, TrackerRole).toString();
            int const pos = str.lastIndexOf(QLatin1Char('.'));

            if (pos >= 0)
            {
                str.truncate(pos + 1);
            }
        }

        myPrefs.set(Prefs::FILTER_TRACKERS, str);
    }
}

void FilterBar::onActivityIndexChanged(int i)
{
    if (!myIsBootstrapping)
    {
        FilterMode const mode = myActivityCombo->itemData(i, ActivityRole).toInt();
        myPrefs.set(Prefs::FILTER_MODE, mode);
    }
}

/***
****
***/

void FilterBar::recountSoon()
{
    if (!myRecountTimer->isActive())
    {
        myRecountTimer->setSingleShot(true);
        myRecountTimer->start(800);
    }
}

void FilterBar::recount()
{
    QAbstractItemModel* model = myActivityCombo->model();

    int torrentsPerMode[FilterMode::NUM_MODES] = {};
    myFilter.countTorrentsPerMode(torrentsPerMode);

    for (int row = 0, n = model->rowCount(); row < n; ++row)
    {
        QModelIndex index = model->index(row, 0);
        int const mode = index.data(ActivityRole).toInt();
        int const count = torrentsPerMode[mode];
        model->setData(index, count, FilterBarComboBox::CountRole);
        model->setData(index, getCountString(count), FilterBarComboBox::CountStringRole);
    }

    refreshTrackers();
}
