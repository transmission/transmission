// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <bitset>

#include <small/map.hpp>

#include <QLineEdit>
#include <QStandardItemModel>
#include <QTimer>
#include <QWidget>

#include "Torrent.h"
#include "Typedefs.h"

class QLabel;
class QString;

class FilterBarComboBox;
class Prefs;
class TorrentFilter;
class TorrentModel;

class FilterBar : public QWidget
{
    Q_OBJECT

public:
    FilterBar(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent = nullptr);
    ~FilterBar() override = default;
    FilterBar(FilterBar&&) = delete;
    FilterBar(FilterBar const&) = delete;
    FilterBar& operator=(FilterBar&&) = delete;
    FilterBar& operator=(FilterBar const&) = delete;

public slots:
    void clear();

private:
    FilterBarComboBox* create_tracker_combo(QStandardItemModel* model);
    FilterBarComboBox* create_activity_combo();
    void refresh_trackers();

    enum : uint8_t
    {
        ACTIVITY,
        TRACKERS,

        NUM_FLAGS
    };

    using Pending = std::bitset<NUM_FLAGS>;

    Prefs& prefs_;
    TorrentModel const& torrents_;
    TorrentFilter const& filter_;

    QStandardItemModel* const tracker_model_ = new QStandardItemModel{ this };

    QLabel* const count_label_ = {};
    FilterBarComboBox* const activity_combo_ = create_activity_combo();
    FilterBarComboBox* const tracker_combo_ = create_tracker_combo(tracker_model_);
    QLineEdit* const line_edit_ = new QLineEdit{ this };

    small::map<QString, int> sitename_counts_;
    QTimer recount_timer_;
    Pending pending_;
    bool is_bootstrapping_ = {};

private slots:
    void recount();
    void recount_soon(Pending const& fields);

    void recount_activity_soon()
    {
        recount_soon(Pending().set(ACTIVITY));
    }

    void recount_trackers_soon()
    {
        recount_soon(Pending().set(TRACKERS));
    }

    void recount_all_soon()
    {
        recount_soon(Pending().set(ACTIVITY).set(TRACKERS));
    }

    void refresh_pref(int key);
    void on_activity_index_changed(int index);
    void on_text_changed(QString const& str);
    void on_torrents_changed(torrent_ids_t const& ids, Torrent::fields_t const& fields);
    void on_tracker_index_changed(int index);
};
