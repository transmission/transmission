// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QWidget>

#include <libtransmission/tr-macros.h>

#include "FaviconCache.h"
#include "Torrent.h"
#include "Typedefs.h"

class QLabel;
class QLineEdit;
class QStandardItem;
class QStandardItemModel;
class QString;

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
    FilterBarComboBox* createCombo(QStandardItemModel*);

    Prefs& prefs_;
    TorrentModel const& torrents_;
    TorrentFilter const& filter_;

    FilterBarComboBox* activity_combo_ = {};
    FilterBarComboBox* path_combo_ = {};
    FilterBarComboBox* tracker_combo_ = {};
    QLabel* count_label_ = {};
    QLineEdit* line_edit_ = {};
    bool is_bootstrapping_ = {};

private slots:
    void refreshPref(int key);
    void onActivityIndexChanged(int index);
    void onPathIndexChanged(int index);
    void onTextChanged(QString const&);
    void onTrackerIndexChanged(int index);
};
