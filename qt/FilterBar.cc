// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FilterBar.h"

#include <cstdint> // uint64_t
#include <unordered_map>
#include <utility>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStandardItemModel>

#include "Application.h"
#include "FilterBarComboBox.h"
#include "FilterBarComboBoxDelegate.h"
#include "Filters.h"
#include "IconCache.h"
#include "NativeIcon.h"
#include "Prefs.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "Utils.h"

// NOLINTNEXTLINE(performance-enum-size)
enum
{
    ACTIVITY_ROLE = FilterBarComboBox::UserRole,
    TRACKER_ROLE
};

/***
****
***/

FilterBarComboBox* FilterBar::create_activity_combo()
{
    auto* c = new FilterBarComboBox{ this };
    auto* delegate = new FilterBarComboBoxDelegate{ this, c };
    c->setItemDelegate(delegate);

    auto* model = new QStandardItemModel{ this };

    auto* row = new QStandardItem{ tr("All") };
    row->setData(QVariant::fromValue(ShowMode::ShowAll), ACTIVITY_ROLE);
    model->appendRow(row);

    model->appendRow(new QStandardItem{}); // separator
    FilterBarComboBoxDelegate::set_separator(model, model->index(1, 0));

    auto add_row = [model](auto const show_mode, QString const& label, std::optional<icons::Type> const type)
    {
        auto* new_row = type ? new QStandardItem{ icons::icon(*type), label } : new QStandardItem{ label };
        new_row->setData(QVariant::fromValue(show_mode), ACTIVITY_ROLE);
        model->appendRow(new_row);
    };
    add_row(ShowMode::ShowActive, tr("Active"), icons::Type::TorrentStateActive);
    add_row(ShowMode::ShowSeeding, tr("Seeding"), icons::Type::TorrentStateSeeding);
    add_row(ShowMode::ShowDownloading, tr("Downloading"), icons::Type::TorrentStateDownloading);
    add_row(ShowMode::ShowPaused, tr("Paused"), icons::Type::TorrentStatePaused);
    add_row(ShowMode::ShowFinished, tr("Finished"), {});
    add_row(ShowMode::ShowVerifying, tr("Verifying"), icons::Type::TorrentStateVerifying);
    add_row(ShowMode::ShowError, tr("Error"), icons::Type::TorrentStateError);

    c->setModel(model);
    return c;
}

/***
****
***/

namespace
{

[[nodiscard]] auto get_count_string(size_t n)
{
    return QStringLiteral("%L1").arg(n);
}

Torrent::fields_t constexpr TrackerFields = {
    static_cast<uint64_t>(1) << Torrent::TRACKER_STATS,
};

[[nodiscard]] auto display_name(QString const& sitename)
{
    auto name = sitename;

    if (!name.isEmpty())
    {
        name.front() = name.front().toTitleCase();
    }

    return name;
}

} // namespace

void FilterBar::refresh_trackers()
{
    // NOLINTNEXTLINE(performance-enum-size)
    enum
    {
        ROW_TOTALS = 0,
        ROW_SEPARATOR,
        ROW_FIRST_TRACKER
    };

    auto torrents_per_sitename = std::unordered_map<QString, int>{};
    for (auto const& tor : torrents_.torrents())
    {
        for (auto const& sitename : tor->sitenames())
        {
            ++torrents_per_sitename[sitename];
        }
    }

    // update the "All" row
    auto const num_trackers = torrents_per_sitename.size();
    auto* item = tracker_model_->item(ROW_TOTALS);
    item->setData(static_cast<int>(num_trackers), FilterBarComboBox::CountRole);
    item->setData(get_count_string(num_trackers), FilterBarComboBox::CountStringRole);

    auto update_tracker_item = [](QStandardItem* i, auto const& it)
    {
        auto const& [sitename, count] = *it;
        auto const name = display_name(sitename);

        i->setData(name, Qt::DisplayRole);
        i->setData(name, TRACKER_ROLE);
        i->setData(get_count_string(static_cast<size_t>(count)), FilterBarComboBox::CountStringRole);
        i->setData(trApp->find_favicon(sitename), Qt::DecorationRole);
        i->setData(static_cast<int>(count), FilterBarComboBox::CountRole);

        return i;
    };

    auto new_trackers = small::map<QString, int>{ torrents_per_sitename.begin(), torrents_per_sitename.end() };
    auto old_it = sitename_counts_.cbegin();
    auto new_it = new_trackers.cbegin();
    auto const old_end = sitename_counts_.cend();
    auto const new_end = new_trackers.cend();
    bool any_added = false;
    int row = ROW_FIRST_TRACKER;

    while ((old_it != old_end) || (new_it != new_end))
    {
        if ((old_it == old_end) || ((new_it != new_end) && (old_it->first > new_it->first)))
        {
            tracker_model_->insertRow(row, update_tracker_item(new QStandardItem{ 1 }, new_it));
            any_added = true;
            ++new_it;
            ++row;
        }
        else if ((new_it == new_end) || ((old_it != old_end) && (old_it->first < new_it->first)))
        {
            tracker_model_->removeRow(row);
            ++old_it;
        }
        else // update
        {
            update_tracker_item(tracker_model_->item(row), new_it);
            ++old_it;
            ++new_it;
            ++row;
        }
    }

    if (any_added) // the one added might match our filter...
    {
        refresh_pref(Prefs::FILTER_TRACKERS);
    }

    sitename_counts_.swap(new_trackers);
}

FilterBarComboBox* FilterBar::create_tracker_combo(QStandardItemModel* model)
{
    auto* c = new FilterBarComboBox{ this };
    auto* delegate = new FilterBarComboBoxDelegate{ this, c };
    c->setItemDelegate(delegate);

    auto* row = new QStandardItem{ tr("All") };
    row->setData(QString{}, TRACKER_ROLE);
    int const count = torrents_.rowCount();
    row->setData(count, FilterBarComboBox::CountRole);
    row->setData(get_count_string(static_cast<size_t>(count)), FilterBarComboBox::CountStringRole);
    model->appendRow(row);

    model->appendRow(new QStandardItem{}); // separator
    FilterBarComboBoxDelegate::set_separator(model, model->index(1, 0));

    c->setModel(model);
    return c;
}

/***
****
***/

FilterBar::FilterBar(Prefs& prefs, TorrentModel const& torrents, TorrentFilter const& filter, QWidget* parent)
    : QWidget{ parent }
    , prefs_{ prefs }
    , torrents_{ torrents }
    , filter_{ filter }
    , count_label_{ new QLabel{ tr("Show:"), this } }
    , is_bootstrapping_{ true }
{
    auto* h = new QHBoxLayout{ this };
    h->setContentsMargins(3, 3, 3, 3);

    h->addWidget(count_label_);
    h->addWidget(activity_combo_);
    h->addWidget(tracker_combo_);
    h->addStretch();
    h->addWidget(line_edit_, 1);

    line_edit_->setClearButtonEnabled(true);
    line_edit_->setPlaceholderText(tr("Search…"));
    line_edit_->setMaximumWidth(250);
    connect(line_edit_, &QLineEdit::textChanged, this, &FilterBar::on_text_changed);

    // listen for changes from the other players
    connect(&prefs_, &Prefs::changed, this, &FilterBar::refresh_pref);
    connect(activity_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterBar::on_activity_index_changed);
    connect(tracker_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &FilterBar::on_tracker_index_changed);
    connect(&torrents_, &TorrentModel::modelReset, this, &FilterBar::recount_all_soon);
    connect(&torrents_, &TorrentModel::rowsInserted, this, &FilterBar::recount_all_soon);
    connect(&torrents_, &TorrentModel::rowsRemoved, this, &FilterBar::recount_all_soon);
    connect(&torrents_, &TorrentModel::torrents_changed, this, &FilterBar::on_torrents_changed);
    connect(&recount_timer_, &QTimer::timeout, this, &FilterBar::recount);
    connect(trApp, &Application::favicons_changed, this, &FilterBar::recount_trackers_soon);

    recount_all_soon();
    is_bootstrapping_ = false; // NOLINT cppcoreguidelines-prefer-member-initializer

    // initialize our state
    for (int const key : { Prefs::FILTER_MODE, Prefs::FILTER_TRACKERS })
    {
        refresh_pref(key);
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

void FilterBar::refresh_pref(int key)
{
    switch (key)
    {
    case Prefs::FILTER_MODE:
        {
            auto const show_mode = prefs_.get<ShowMode>(key);
            QAbstractItemModel const* const model = activity_combo_->model();
            QModelIndexList indices = model->match(model->index(0, 0), ACTIVITY_ROLE, QVariant::fromValue(show_mode));
            activity_combo_->setCurrentIndex(indices.isEmpty() ? 0 : indices.first().row());
            break;
        }

    case Prefs::FILTER_TRACKERS:
        {
            auto const display_name = prefs_.get<QString>(key);

            if (auto rows = tracker_model_->findItems(display_name); !rows.isEmpty())
            {
                tracker_combo_->setCurrentIndex(rows.front()->row());
            }
            else // hm, we don't seem to have this tracker anymore...
            {
                bool const is_bootstrapping = tracker_model_->rowCount() <= 2;

                if (!is_bootstrapping)
                {
                    prefs_.set(key, QString{});
                }
            }

            break;
        }

    default:
        break;
    }
}

void FilterBar::on_torrents_changed(torrent_ids_t const& ids, Torrent::fields_t const& changed_fields)
{
    Q_UNUSED(ids)

    if ((changed_fields & TrackerFields).any())
    {
        recount_trackers_soon();
    }

    if ((changed_fields & ShowModeFields).any())
    {
        recount_activity_soon();
    }
}

void FilterBar::on_text_changed(QString const& str)
{
    if (!is_bootstrapping_)
    {
        prefs_.set(Prefs::FILTER_TEXT, str.trimmed());
    }
}

void FilterBar::on_tracker_index_changed(int i)
{
    if (!is_bootstrapping_)
    {
        auto const display_name = tracker_combo_->itemData(i, TRACKER_ROLE).toString();
        prefs_.set(Prefs::FILTER_TRACKERS, display_name);
    }
}

void FilterBar::on_activity_index_changed(int i)
{
    if (!is_bootstrapping_)
    {
        auto const show_mode = activity_combo_->itemData(i, ACTIVITY_ROLE).value<ShowMode>();
        prefs_.set(Prefs::FILTER_MODE, show_mode);
    }
}

/***
****
***/

void FilterBar::recount_soon(Pending const& fields)
{
    pending_ |= fields;

    if (!recount_timer_.isActive())
    {
        recount_timer_.setSingleShot(true);
        recount_timer_.start(800);
    }
}

void FilterBar::recount()
{
    QAbstractItemModel* model = activity_combo_->model();

    decltype(pending_) pending = {};
    std::swap(pending_, pending);

    if (pending[ACTIVITY])
    {
        auto const torrents_per_mode = filter_.count_torrents_per_mode();

        for (int row = 0, n = model->rowCount(); row < n; ++row)
        {
            auto const index = model->index(row, 0);
            auto const show_mode = index.data(ACTIVITY_ROLE).value<ShowMode>();
            auto const count = torrents_per_mode[static_cast<int>(show_mode)];
            model->setData(index, count, FilterBarComboBox::CountRole);
            model->setData(index, get_count_string(count), FilterBarComboBox::CountStringRole);
        }
    }

    if (pending[TRACKERS])
    {
        refresh_trackers();
    }
}
