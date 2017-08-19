/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

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

enum
{
    ActivityRole = FilterBarComboBox::UserRole,
    TrackerRole
};

namespace
{

QString readableHostName(QString const& host)
{
    // get the readable name...
    QString name = host;
    int const pos = name.lastIndexOf(QLatin1Char('.'));

    if (pos >= 0)
    {
        name.truncate(pos);
    }

    if (!name.isEmpty())
    {
        name[0] = name[0].toUpper();
    }

    return name;
}

} // namespace

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

void FilterBar::refreshTrackers()
{
    FaviconCache& favicons = qApp->faviconCache();
    int const firstTrackerRow = 2; // skip over the "All" and separator...

    // pull info from the tracker model...
    QSet<QString> oldHosts;

    for (int row = firstTrackerRow;; ++row)
    {
        QModelIndex index = myTrackerModel->index(row, 0);

        if (!index.isValid())
        {
            break;
        }

        oldHosts << index.data(TrackerRole).toString();
    }

    // pull the new stats from the torrent model...
    QSet<QString> newHosts;
    QMap<QString, int> torrentsPerHost;

    for (int row = 0;; ++row)
    {
        QModelIndex index = myTorrents.index(row, 0);

        if (!index.isValid())
        {
            break;
        }

        Torrent const* tor = index.data(TorrentModel::TorrentRole).value<Torrent const*>();
        QSet<QString> torrentNames;

        for (QString const& host : tor->hosts())
        {
            newHosts.insert(host);
            torrentNames.insert(readableHostName(host));
        }

        for (QString const& name : torrentNames)
        {
            ++torrentsPerHost[name];
        }
    }

    // update the "All" row
    myTrackerModel->setData(myTrackerModel->index(0, 0), myTorrents.rowCount(), FilterBarComboBox::CountRole);
    myTrackerModel->setData(myTrackerModel->index(0, 0), getCountString(myTorrents.rowCount()),
        FilterBarComboBox::CountStringRole);

    // rows to update
    for (QString const& host : oldHosts & newHosts)
    {
        QString const name = readableHostName(host);
        QStandardItem* row = myTrackerModel->findItems(name).front();
        int const count = torrentsPerHost[name];
        row->setData(count, FilterBarComboBox::CountRole);
        row->setData(getCountString(count), FilterBarComboBox::CountStringRole);
        row->setData(QIcon(favicons.findFromHost(host)), Qt::DecorationRole);
    }

    // rows to remove
    for (QString const& host : oldHosts - newHosts)
    {
        QString const name = readableHostName(host);
        QStandardItem* item = myTrackerModel->findItems(name).front();

        if (!item->data(TrackerRole).toString().isEmpty()) // don't remove "All"
        {
            myTrackerModel->removeRows(item->row(), 1);
        }
    }

    // rows to add
    bool anyAdded = false;

    for (QString const& host : newHosts - oldHosts)
    {
        QString const name = readableHostName(host);

        if (!myTrackerModel->findItems(name).isEmpty())
        {
            continue;
        }

        // find the sorted position to add this row
        int i = firstTrackerRow;

        for (int n = myTrackerModel->rowCount(); i < n; ++i)
        {
            QString const rowName = myTrackerModel->index(i, 0).data(Qt::DisplayRole).toString();

            if (rowName >= name)
            {
                break;
            }
        }

        // add the row
        QStandardItem* row = new QStandardItem(favicons.findFromHost(host), name);
        int const count = torrentsPerHost[host];
        row->setData(count, FilterBarComboBox::CountRole);
        row->setData(getCountString(count), FilterBarComboBox::CountStringRole);
        row->setData(QIcon(favicons.findFromHost(host)), Qt::DecorationRole);
        row->setData(host, TrackerRole);
        myTrackerModel->insertRow(i, row);
        anyAdded = true;
    }

    if (anyAdded) // the one added might match our filter...
    {
        refreshPref(Prefs::FILTER_TRACKERS);
    }
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
    QList<int> initKeys;
    initKeys << Prefs::FILTER_MODE << Prefs::FILTER_TRACKERS;

    for (int const key : initKeys)
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
            QString const tracker = myPrefs.getString(key);
            QString const name = readableHostName(tracker);
            QList<QStandardItem*> rows = myTrackerModel->findItems(name);

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

QString FilterBar::getCountString(int n) const
{
    return QString::fromLatin1("%L1").arg(n);
}
