/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QSortFilterProxyModel>

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
    TorrentFilter(Prefs const& prefs);
    virtual ~TorrentFilter();

    int hiddenRowCount() const;

    void countTorrentsPerMode(int* setmeCounts) const;

protected:
    // QSortFilterProxyModel
    virtual bool filterAcceptsRow(int, QModelIndex const&) const;
    virtual bool lessThan(QModelIndex const&, QModelIndex const&) const;

private:
    bool activityFilterAcceptsTorrent(Torrent const* tor, FilterMode const& mode) const;
    bool trackerFilterAcceptsTorrent(Torrent const* tor, QString const& tracker) const;

private slots:
    void refreshPref(int key);

private:
    Prefs const& myPrefs;
};
