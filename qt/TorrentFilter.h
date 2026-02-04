// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>

#include <QSortFilterProxyModel>
#include <QTimer>

#include "Filters.h"

class QString;

class Prefs;
class Torrent;

class TorrentFilter : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit TorrentFilter(Prefs const& prefs);
    ~TorrentFilter() override = default;
    TorrentFilter(TorrentFilter&&) = delete;
    TorrentFilter(TorrentFilter const&) = delete;
    TorrentFilter& operator=(TorrentFilter&&) = delete;
    TorrentFilter& operator=(TorrentFilter const&) = delete;

    [[nodiscard]] std::array<int, ShowModeCount> count_torrents_per_mode() const;

protected:
    // QSortFilterProxyModel
    [[nodiscard]] bool filterAcceptsRow(int source_row, QModelIndex const& source_parent) const override;
    [[nodiscard]] bool lessThan(QModelIndex const& source_left, QModelIndex const& source_right) const override;

private slots:
    void on_pref_changed(int key);
    void refilter();

private:
    QTimer refilter_timer_;
    Prefs const& prefs_;
};
