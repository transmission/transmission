// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QSortFilterProxyModel>
#include <QTimer>

#include <libtransmission/tr-macros.h>

class QString;

class FilterMode;
class Prefs;
class Torrent;

class TorrentFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TorrentFilter)

public:
    enum TextMode
    {
        FILTER_BY_NAME,
        FILTER_BY_FILES,
        FILTER_BY_TRACKER
    };

    explicit TorrentFilter(Prefs const& prefs);

protected:
    // QSortFilterProxyModel
    bool filterAcceptsRow(int, QModelIndex const&) const override;
    bool lessThan(QModelIndex const&, QModelIndex const&) const override;

private slots:
    void onPrefChanged(int key);
    void refilter();

private:
    QTimer refilter_timer_;
    Prefs const& prefs_;
};
