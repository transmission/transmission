/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <bitset>
#include <map>

#include <QString>
#include <QWidget>

#include "FaviconCache.h"
#include "Macros.h"
#include "Torrent.h"
#include "Typedefs.h"

class QLabel;
class QLineEdit;
class QStandardItemModel;
class QTimer;

class FilterBarComboBox;
class Prefs;
class TorrentFilter;
class TorrentModel;

class FilterBar : public QWidget
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(FilterBar)

public:
    FilterBar(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent = nullptr);

public slots:
    void clear();

private:
    FilterBarComboBox* createTrackerCombo(QStandardItemModel*);
    FilterBarComboBox* createActivityCombo();
    void refreshTrackers();

    enum
    {
        ACTIVITY,
        TRACKERS,

        NUM_FLAGS
    };

    using Pending = std::bitset<NUM_FLAGS>;

    Prefs& prefs_;
    TorrentModel const& torrents_;
    TorrentFilter const& filter_;

    std::map<FaviconCache::Key, int> tracker_counts_;
    FilterBarComboBox* activity_combo_ = {};
    FilterBarComboBox* tracker_combo_ = {};
    QLabel* count_label_ = {};
    QStandardItemModel* tracker_model_ = {};
    QTimer* recount_timer_ = {};
    QLineEdit* line_edit_ = {};
    Pending pending_ = {};
    bool is_bootstrapping_ = {};

private slots:
    void recount();
    void recountSoon(Pending const& fields);
    void recountActivitySoon() { recountSoon(Pending().set(ACTIVITY)); }
    void recountTrackersSoon() { recountSoon(Pending().set(TRACKERS)); }
    void recountAllSoon() { recountSoon(Pending().set(ACTIVITY).set(TRACKERS)); }

    void refreshPref(int key);
    void onActivityIndexChanged(int index);
    void onTextChanged(QString const&);
    void onTorrentsChanged(torrent_ids_t const&, Torrent::fields_t const& fields);
    void onTrackerIndexChanged(int index);
};
