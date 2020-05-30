/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <array>

#include <QSortFilterProxyModel>
#include <QTimer>

#include "Filters.h"

class QString;

class FilterMode;
class Prefs;
class Torrent;

class TorrentFilter : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    enum TextMode
    {
        FILTER_BY_NAME,
        FILTER_BY_FILES,
        FILTER_BY_TRACKER
    };

public:
    explicit TorrentFilter(Prefs const& prefs);
    virtual ~TorrentFilter();
    [[nodiscard]] std::array<int, FilterMode::NUM_MODES> countTorrentsPerMode() const;

protected:
    // QSortFilterProxyModel
    bool filterAcceptsRow(int, QModelIndex const&) const override;
    bool lessThan(QModelIndex const&, QModelIndex const&) const override;

private slots:
    void onPrefChanged(int key);
    void refilter();

private:
    bool activityFilterAcceptsTorrent(Torrent const* tor, FilterMode const& mode) const;
    bool trackerFilterAcceptsTorrent(Torrent const* tor, QString const& tracker) const;

    QTimer refilter_timer_;
    Prefs const& prefs_;
};
