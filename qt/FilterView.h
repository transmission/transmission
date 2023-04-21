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
#include <QListView>

#include "FaviconCache.h"
#include "Torrent.h"
#include "Typedefs.h"
#include "FilterUI.h"
#include "IconToolButton.h"

class QString;

class Prefs;
class TorrentFilter;
class TorrentModel;

class FilterView : public FilterUI
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(FilterView)

public:
    FilterView(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent = nullptr);

public slots:
    void clear();

protected:
    QSize sizeHint() const override;

private:
    QListView* createTrackerUI(QStandardItemModel*);
    QListView* createActivityUI();

    QListView* const activity_ui_ = createActivityUI();
    QListView* tracker_ui_ = {};
    QLineEdit* line_edit_ = new QLineEdit{ this };
    IconToolButton* btn_ = new IconToolButton(this);

private slots:
    void recount() override;

    void refreshPref(int key) override;
    void onActivityIndexChanged(QModelIndex);
    void onTrackerIndexChanged(QModelIndex);
};
