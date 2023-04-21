// This file Copyright © 2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "FilterView.h"

#include <cstdint> // uint64_t
#include <map>
#include <utility>

#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>
#include <QListView>

#include "Application.h"
#include "FaviconCache.h"
#include "Filters.h"
#include "Prefs.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "FilterUIDelegate.h"

enum
{
    ACTIVITY_ROLE = FilterUI::UserRole,
    TRACKER_ROLE
};

QSize FilterView::sizeHint() const
{
    return {qMax(activity_ui_->sizeHintForColumn(0), tracker_ui_->sizeHintForColumn(0)), 1};
}

QListView* FilterView::createActivityUI()
{
    auto* lv = new QListView(this);
    auto* delegate = new FilterUIDelegate(this, lv);
    lv->setItemDelegate(delegate);

    auto* model = FilterUI::createActivityModel(this);

    lv->setModel(model);
    lv->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lv->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lv->setSelectionBehavior(QAbstractItemView::SelectRows);

    int max_height = 0;
    for (int i = 0; i < model->rowCount(); i++) {
        max_height += lv->sizeHintForRow(i);
    }
    lv->setMaximumHeight(max_height + 2*style()->pixelMetric(QStyle::PM_DefaultFrameWidth));

    return lv;
}

namespace
{

QString getCountString(size_t n)
{
    return QStringLiteral("%L1").arg(n);
}

Torrent::fields_t constexpr TrackerFields = {
    static_cast<uint64_t>(1) << Torrent::TRACKER_STATS,
};

auto constexpr ActivityFields = FilterMode::TorrentFields;

} // namespace

QListView* FilterView::createTrackerUI(QStandardItemModel* model)
{
    auto* lv = new QListView(this);
    auto* delegate = new FilterUIDelegate(this, lv);
    lv->setItemDelegate(delegate);

    auto* row = new QStandardItem(tr("All"));
    row->setData(QString(), TRACKER_ROLE);
    int const count = torrents_.rowCount();
    row->setData(count, CountRole);
    row->setData(getCountString(static_cast<size_t>(count)), CountStringRole);
    model->appendRow(row);

    model->appendRow(new QStandardItem); // separator
    FilterUIDelegate::setSeparator(model, model->index(1, 0));

    lv->setModel(model);
    lv->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lv->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lv->setSelectionBehavior(QAbstractItemView::SelectRows);

    return lv;
}

FilterView::FilterView(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent)
    : FilterUI(prefs, torrents, filter, parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(1, 1, 1, 1);

    auto* h = new QHBoxLayout();

    h->addWidget(btn_);
    connect(btn_, &IconToolButton::clicked, this, &FilterView::toggleUI);

    line_edit_->setClearButtonEnabled(true);
    line_edit_->setPlaceholderText(tr("Search…"));
    h->addWidget(line_edit_, 1);
    connect(line_edit_, &QLineEdit::textChanged, this, &FilterView::onTextChanged);

    v->addLayout(h);

    tracker_ui_ = createTrackerUI(tracker_model_);
    v->addWidget(tracker_ui_);

    v->addWidget(activity_ui_);
    setMinimumWidth(activity_ui_->sizeHintForColumn(0) + style()->pixelMetric(QStyle::PM_DefaultFrameWidth) + style()->pixelMetric(QStyle::PM_ScrollBarExtent));

    // listen for changes from the other players
    connect(activity_ui_, &QListView::clicked, this, &FilterView::onActivityIndexChanged);
    connect(tracker_ui_, &QListView::clicked, this, &FilterView::onTrackerIndexChanged);

    recountAllSoon();
    is_bootstrapping_ = false; // NOLINT cppcoreguidelines-prefer-member-initializer

    // initialize our state
    for (int const key : { Prefs::FILTER_MODE, Prefs::FILTER_TRACKERS })
    {
        refreshPref(key);
    }
}

void FilterView::clear()
{
    activity_ui_->clearSelection();
    tracker_ui_->clearSelection();
    line_edit_->clear();
}

void FilterView::refreshPref(int key)
{
    switch (key)
    {
    case Prefs::FILTER_MODE:
        {
            auto const modes = prefs_.get<QList<FilterMode>>(key);

            if (modes.count() == 1) {
                QAbstractItemModel const* const model = activity_ui_->model();
                QModelIndexList indices;
                indices = model->match(model->index(0, 0), ACTIVITY_ROLE, modes.first().mode());
                activity_ui_->setCurrentIndex(indices.first());
            }
            break;
        }

    case Prefs::FILTER_TRACKERS:
        {
            auto const display_names = prefs_.get<QStringList>(key);

            if (display_names.count() == 1) {
                const auto& display_name = display_names.first();
                auto rows = tracker_model_->findItems(display_name);
                if (!rows.isEmpty())
                {
                    tracker_ui_->setCurrentIndex(rows.front()->index());
                }
                else
                {
                    bool const is_bootstrapping = tracker_model_->rowCount() <= 2;

                    if (!is_bootstrapping)
                    {
                        prefs_.set(key, QString());
                    }
                }
            }
            break;
        }
    }
}

void FilterView::onTrackerIndexChanged(QModelIndex i)
{
    if (!is_bootstrapping_)
    {
        auto *const display_names = new QStringList;
        for (QModelIndex const display_index : tracker_ui_->selectionModel()->selectedIndexes()) {
            display_names->append(tracker_model_->data(display_index, TRACKER_ROLE).toString());
        }
        prefs_.set(Prefs::FILTER_TRACKERS, *display_names);
    }
}

void FilterView::onActivityIndexChanged(QModelIndex i)
{
    if (!is_bootstrapping_)
    {
        auto *const modes = new QList<FilterMode>;
        for (QModelIndex const mode_index : activity_ui_->selectionModel()->selectedIndexes()) {
            modes->append(FilterMode(activity_ui_->model()->data(mode_index, ACTIVITY_ROLE).toInt()));
        }
        prefs_.set(Prefs::FILTER_MODE, *modes);
    }
}

void FilterView::recount()
{
    QAbstractItemModel* model = activity_ui_->model();

    decltype(pending_) pending = {};
    std::swap(pending_, pending);

    if (pending[ACTIVITY])
    {
        auto const torrents_per_mode = filter_.countTorrentsPerMode();

        for (int row = 0, n = model->rowCount(); row < n; ++row)
        {
            auto const index = model->index(row, 0);
            auto const mode = index.data(ACTIVITY_ROLE).toInt();
            auto const count = torrents_per_mode[mode];
            model->setData(index, count, CountRole);
            model->setData(index, getCountString(static_cast<size_t>(count)), CountStringRole);
        }
    }

    if (pending[TRACKERS])
    {
        refreshTrackers();
    }
}
