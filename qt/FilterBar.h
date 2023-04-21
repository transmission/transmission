// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <bitset>
#include <map>

#include <QLineEdit>
#include <QStandardItemModel>
#include <QTimer>
#include <QWidget>

#include <libtransmission/tr-macros.h>
#include <QComboBox>

#include "FaviconCache.h"
#include "Torrent.h"
#include "Typedefs.h"
#include "FilterUI.h"
#include "IconToolButton.h"

class QLabel;
class QString;

class FilterBarComboBox;
class Prefs;
class TorrentFilter;
class TorrentModel;

class FilterBar : public FilterUI
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(FilterBar)

public:
    FilterBar(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent = nullptr);

public slots:
    void clear();

protected:
    QComboBox* createTrackerUI(QStandardItemModel*);
    QComboBox* createActivityUI();

    QComboBox* const activity_ui_ = createActivityUI();
    QComboBox* tracker_ui_ = {};
    QLabel* count_label_ = {};
    QLineEdit* line_edit_ = new QLineEdit{ this };
    IconToolButton* btn_ = new IconToolButton(this);

private slots:
    void recount() override;

    void refreshPref(int key) override;
    void onActivityIndexChanged(int index);
    void onTrackerIndexChanged(int index);
};
