// This file Copyright © 2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <bitset>
#include <map>

#include <QLineEdit>
#include <QStandardItemModel>
#include <QTimer>
#include <QWidget>

#include <libtransmission/tr-macros.h>

#include "FaviconCache.h"
#include "Torrent.h"
#include "Typedefs.h"

class QString;

class Prefs;
class TorrentFilter;
class TorrentModel;

class FilterUI : public QWidget
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(FilterUI)

public:
    FilterUI(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent = nullptr);

    enum
    {
        CountRole = Qt::UserRole + 1,
        CountStringRole,
        UserRole
    };

    QStandardItemModel* createActivityModel(QObject* object);

protected:
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

    std::map<QString, int> sitename_counts_;
    QStandardItemModel* tracker_model_ = new QStandardItemModel{ this };
    QTimer recount_timer_;
    Pending pending_ = {};
    bool is_bootstrapping_ = {};

protected slots:
    virtual void recount() = 0;
    void recountSoon(Pending const& fields);

    void recountActivitySoon()
    {
        recountSoon(Pending().set(ACTIVITY));
    }

    void recountTrackersSoon()
    {
        recountSoon(Pending().set(TRACKERS));
    }

    void recountAllSoon()
    {
        recountSoon(Pending().set(ACTIVITY).set(TRACKERS));
    }

    virtual void refreshPref(int key) = 0;

    void toggleUI(bool checked);
    void onTextChanged(QString const&);
    void onTorrentsChanged(torrent_ids_t const&, Torrent::fields_t const& fields);
};
