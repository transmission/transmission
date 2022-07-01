// This file Copyright © 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FilterBar.h"

#include <cstdint> // uint64_t
#include <map>
#include <unordered_map>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStandardItemModel>

#include "Application.h"
#include "FilterBarComboBox.h"
#include "FilterBarComboBoxDelegate.h"
#include "Filters.h"
#include "Prefs.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "Utils.h"

/***
****
***/

FilterBarComboBox* FilterBar::createCombo(QStandardItemModel* model)
{
    auto* c = new FilterBarComboBox(this);
    auto* delegate = new FilterBarComboBoxDelegate(this, c);
    c->setItemDelegate(delegate);
    delegate->setSeparator(model, model->index(1, 0));
    c->setModel(model);
    return c;
}

/***
****
***/

FilterBar::FilterBar(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent)
    : QWidget(parent)
    , prefs_(prefs)
    , torrents_(torrents)
    , filter_(filter)
    , is_bootstrapping_(true)
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(3, 3, 3, 3);

    count_label_ = new QLabel(tr("Show:"), this);
    h->addWidget(count_label_);

    activity_combo_ = createCombo(torrents_.activityFilterModel());
    h->addWidget(activity_combo_);

    tracker_combo_ = createCombo(torrents_.trackerFilterModel());
    h->addWidget(tracker_combo_);

    path_combo_ = createCombo(torrents_.pathFilterModel());
    h->addWidget(path_combo_);

    h->addStretch();

    line_edit_ = new QLineEdit(this);
    line_edit_->setClearButtonEnabled(true);
    line_edit_->setPlaceholderText(tr("Search..."));
    line_edit_->setMaximumWidth(250);
    h->addWidget(line_edit_, 1);
    connect(line_edit_, &QLineEdit::textChanged, this, &FilterBar::onTextChanged);

    // listen for changes from the other players
    connect(&prefs_, &Prefs::changed, this, &FilterBar::refreshPref);
    connect(activity_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterBar::onActivityIndexChanged);
    connect(path_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterBar::onPathIndexChanged);
    connect(tracker_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterBar::onTrackerIndexChanged);
    connect(&torrents_, &TorrentModel::filterChanged, this, &FilterBar::refreshPref);

    is_bootstrapping_ = false;

    // initialize our state
    for (int const key : { Prefs::FILTER_MODE, Prefs::FILTER_TRACKERS })
    {
        refreshPref(key);
    }
}

/***
****
***/

void FilterBar::clear()
{
    activity_combo_->setCurrentIndex(0);
    tracker_combo_->setCurrentIndex(0);
    line_edit_->clear();
}

/***
****
***/

void FilterBar::refreshPref(int key)
{
    switch (key)
    {
    case Prefs::FILTER_MODE:
        {
            auto const m = prefs_.get<FilterMode>(key);
            QAbstractItemModel const* const model = activity_combo_->model();
            QModelIndexList indices = model->match(model->index(0, 0), TorrentModel::ActivityRole, m.mode());
            activity_combo_->setCurrentIndex(indices.isEmpty() ? 0 : indices.first().row());
            break;
        }

    case Prefs::FILTER_TRACKERS:
        {
            auto tracker_model_ = torrents_.trackerFilterModel();
            auto const display_name = prefs_.getString(key);
            auto rows = tracker_model_->findItems(display_name);
            if (!rows.isEmpty())
            {
                tracker_combo_->setCurrentIndex(rows.front()->row());
            }
            else // hm, we don't seem to have this tracker anymore...
            {
                bool const is_bootstrapping = tracker_model_->rowCount() <= 2;

                if (!is_bootstrapping)
                {
                    prefs_.set(key, QString());
                }
            }

            break;
        }
    }
}

void FilterBar::onTextChanged(QString const& str)
{
    if (!is_bootstrapping_)
    {
        prefs_.set(Prefs::FILTER_TEXT, str.trimmed());
    }
}

void FilterBar::onPathIndexChanged(int i)
{
    if (!is_bootstrapping_)
    {
        auto const display_name = path_combo_->itemData(i, TorrentModel::PathRole).toString();
        prefs_.set(Prefs::FILTER_PATH, display_name);
    }
}

void FilterBar::onTrackerIndexChanged(int i)
{
    if (!is_bootstrapping_)
    {
        auto const display_name = tracker_combo_->itemData(i, TorrentModel::TrackerRole).toString();
        prefs_.set(Prefs::FILTER_TRACKERS, display_name);
    }
}

void FilterBar::onActivityIndexChanged(int i)
{
    if (!is_bootstrapping_)
    {
        auto const mode = FilterMode(activity_combo_->itemData(i, TorrentModel::ActivityRole).toInt());
        prefs_.set(Prefs::FILTER_MODE, mode);
    }
}
