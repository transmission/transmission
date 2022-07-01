// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cassert>
#include <string_view>
#include <utility>

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include <QSortFilterProxyModel>
#include <QStandardItemModel>

#include "Application.h"
#include "FaviconCache.h"
#include "Filters.h"
#include "IconCache.h"
#include "Prefs.h"
#include "Speed.h"
#include "Torrent.h"
#include "TorrentDelegate.h"
#include "TorrentModel.h"
#include "VariantHelpers.h"

using ::trqt::variant_helpers::getValue;

/***
****
***/

namespace
{

class PathProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    static QAbstractItemModel* create(QObject* parent, QAbstractItemModel* sourceModel)
    {
        auto* proxy = new PathProxy(parent);
        proxy->setSourceModel(sourceModel);
        return proxy;
    }

protected:
    bool filterAcceptsRow(int source_row, QModelIndex const& source_parent) const override
    {
        Q_UNUSED(source_parent);
        return source_row > 0; // skip "All" item
    }
};

struct TorrentIdLessThan
{
    bool operator()(Torrent const* left, Torrent const* right) const
    {
        return left->id() < right->id();
    }

    bool operator()(int left_id, Torrent const* right) const
    {
        return left_id < right->id();
    }

    bool operator()(Torrent const* left, int right_id) const
    {
        return left->id() < right_id;
    }
};

template<typename Iter>
auto getIds(Iter it, Iter end)
{
    torrent_ids_t ids;

    for (; it != end; ++it)
    {
        ids.insert((*it)->id());
    }

    return ids;
}

QString getCountString(size_t n)
{
    return QStringLiteral("%L1").arg(n);
}

auto constexpr ActivityFields = FilterMode::TorrentFields;

Torrent::fields_t constexpr PathFields = {
    uint64_t(1) << Torrent::DOWNLOAD_DIR,
};

Torrent::fields_t constexpr TrackerFields = {
    uint64_t(1) << Torrent::TRACKER_STATS,
};

} // namespace

/***
****
***/

TorrentModel::TorrentModel(Prefs const& prefs)
    : prefs_(prefs)
    , activity_model_(createActivityModel())
    , path_model_(createFilterModel(PathRole, 0))
    , tracker_model_(createFilterModel(TrackerRole, 0))
    , path_proxy_(PathProxy::create(this, path_model_))
{
    connect(this, &TorrentModel::modelReset, this, &TorrentModel::recountAllSoon);
    connect(this, &TorrentModel::rowsInserted, this, &TorrentModel::recountAllSoon);
    connect(this, &TorrentModel::rowsRemoved, this, &TorrentModel::recountAllSoon);
    connect(this, &TorrentModel::torrentsChanged, this, &TorrentModel::onTorrentsChanged);
    connect(&trApp->faviconCache(), &FaviconCache::pixmapReady, this, &TorrentModel::recountTrackersSoon);
    connect(&recount_timer_, &QTimer::timeout, this, &TorrentModel::recount);

    recountAllSoon();
}

TorrentModel::~TorrentModel()
{
    clear();
}

void TorrentModel::clear()
{
    beginResetModel();
    qDeleteAll(torrents_);
    torrents_.clear();
    endResetModel();
}

int TorrentModel::rowCount(QModelIndex const& parent) const
{
    Q_UNUSED(parent)

    return torrents_.size();
}

QVariant TorrentModel::data(QModelIndex const& index, int role) const
{
    auto const* const t = (index.isValid() && index.row() < rowCount()) ? torrents_.at(index.row()) : nullptr;

    if (t != nullptr)
    {
        switch (role)
        {
        case Qt::DisplayRole:
            return t->name();

        case Qt::DecorationRole:
            return t->getMimeTypeIcon();

        case TorrentRole:
            return QVariant::fromValue(t);

        default:
            break;
        }
    }

    return {};
}

/***
****
***/

void TorrentModel::removeTorrents(tr_variant* list)
{
    torrents_t torrents;
    torrents.reserve(tr_variantListSize(list));

    int i = 0;
    tr_variant const* child = nullptr;
    while ((child = tr_variantListChild(list, i++)) != nullptr)
    {
        if (auto const id = getValue<int>(child); id)
        {
            if (auto* const torrent = getTorrentFromId(*id); torrent != nullptr)
            {
                torrents.push_back(torrent);
            }
        }
    }

    if (!torrents.empty())
    {
        rowsRemove(torrents);
    }
}

void TorrentModel::updateTorrents(tr_variant* torrents, bool is_complete_list)
{
    auto const old = is_complete_list ? torrents_ : torrents_t{};
    auto added = torrent_ids_t{};
    auto changed = torrent_ids_t{};
    auto completed = torrent_ids_t{};
    auto edited = torrent_ids_t{};
    auto instantiated = torrents_t{};
    auto needinfo = torrent_ids_t{};
    auto processed = torrents_t{};
    auto changed_fields = Torrent::fields_t{};

    auto const now = time(nullptr);
    auto const recently_added = [&now](auto const& tor)
    {
        static auto constexpr MaxAge = 60;
        auto const date = tor->dateAdded();
        return (date != 0) && (difftime(now, date) < MaxAge);
    };

    // build a list of the property keys
    tr_variant* const first_child = tr_variantListChild(torrents, 0);
    bool const table = tr_variantIsList(first_child);
    std::vector<tr_quark> keys;
    if (table)
    {
        // In 'table' format, the first entry in 'torrents' is an array of keys.
        // All the other entries are an array of the values for one torrent.
        auto sv = std::string_view{};
        size_t i = 0;
        keys.reserve(tr_variantListSize(first_child));
        while (tr_variantGetStrView(tr_variantListChild(first_child, i++), &sv))
        {
            keys.push_back(tr_quark_new(sv));
        }
    }
    else if (first_child != nullptr)
    {
        // In 'object' format, every entry is an object with the same set of properties
        auto key = tr_quark{};
        tr_variant* value = nullptr;
        for (size_t i = 0; tr_variantDictChild(first_child, i, &key, &value); ++i)
        {
            keys.push_back(key);
        }
    }

    // Find the position of TR_KEY_id so we can do torrent lookup
    auto const id_it = std::find(std::begin(keys), std::end(keys), TR_KEY_id);
    if (id_it == std::end(keys)) // no ids provided; we can't proceed
    {
        return;
    }

    auto const id_pos = std::distance(std::begin(keys), id_it);

    // Loop through the torrent records...
    std::vector<tr_variant*> values;
    values.reserve(keys.size());
    size_t tor_index = table ? 1 : 0;
    processed.reserve(tr_variantListSize(torrents));
    tr_variant* v = nullptr;
    while ((v = tr_variantListChild(torrents, tor_index++)))
    {
        // Build an array of values
        values.clear();
        if (table)
        {
            // In table mode, v is already a list of values
            size_t i = 0;
            tr_variant* val = nullptr;
            while ((val = tr_variantListChild(v, i++)))
            {
                values.push_back(val);
            }
        }
        else
        {
            // In object mode, v is an object of torrent property key/vals
            size_t i = 0;
            auto key = tr_quark{};
            tr_variant* value = nullptr;
            while (tr_variantDictChild(v, i++, &key, &value))
            {
                values.push_back(value);
            }
        }

        // Find the torrent id
        auto const id = getValue<int>(values[id_pos]);
        if (!id)
        {
            continue;
        }

        Torrent* tor = getTorrentFromId(*id);
        bool is_new = false;

        if (tor == nullptr)
        {
            tor = new Torrent(prefs_, *id);
            instantiated.push_back(tor);
            is_new = true;
        }

        auto const fields = tor->update(keys.data(), values.data(), keys.size());

        if (fields.any())
        {
            changed_fields |= fields;
            changed.insert(*id);
        }

        if (fields.test(Torrent::EDIT_DATE))
        {
            edited.insert(*id);
        }

        if (is_new && !tor->hasName())
        {
            needinfo.insert(*id);
        }

        if (recently_added(tor) && tor->hasName() && !already_added_.count(*id))
        {
            added.insert(*id);
            already_added_.insert(*id);
        }

        if (fields.test(Torrent::LEFT_UNTIL_DONE) && (tor->leftUntilDone() == 0) && (tor->downloadedEver() > 0))
        {
            completed.insert(*id);
        }

        processed.push_back(tor);
    }

    // model upkeep

    if (!instantiated.empty())
    {
        rowsAdd(instantiated);
    }

    if (!edited.empty())
    {
        emit torrentsEdited(edited);
    }

    if (!changed.empty())
    {
        rowsEmitChanged(changed);
    }

    // emit signals

    if (!added.empty())
    {
        emit torrentsAdded(added);
    }

    if (!needinfo.empty())
    {
        emit torrentsNeedInfo(needinfo);
    }

    if (!changed.empty())
    {
        emit torrentsChanged(changed, changed_fields);
    }

    if (!completed.empty())
    {
        emit torrentsCompleted(completed);
    }

    // model upkeep

    if (is_complete_list)
    {
        std::sort(processed.begin(), processed.end(), TorrentIdLessThan());
        torrents_t removed;
        removed.reserve(old.size());
        std::set_difference(old.begin(), old.end(), processed.begin(), processed.end(), std::back_inserter(removed));
        rowsRemove(removed);
    }
}

/***
****
***/

std::optional<int> TorrentModel::getRow(int id) const
{
    std::optional<int> row;

    auto const [begin, end] = std::equal_range(torrents_.begin(), torrents_.end(), id, TorrentIdLessThan());
    if (begin != end)
    {
        row = std::distance(torrents_.begin(), begin);
        assert(torrents_[*row]->id() == id);
    }

    return row;
}

Torrent* TorrentModel::getTorrentFromId(int id)
{
    auto const row = getRow(id);
    return row ? torrents_[*row] : nullptr;
}

Torrent const* TorrentModel::getTorrentFromId(int id) const
{
    auto const row = getRow(id);
    return row ? torrents_[*row] : nullptr;
}

/***
****
***/

std::vector<TorrentModel::span_t> TorrentModel::getSpans(torrent_ids_t const& ids) const
{
    // ids -> rows
    std::vector<int> rows;
    rows.reserve(ids.size());
    for (auto const& id : ids)
    {
        auto const row = getRow(id);
        if (row)
        {
            rows.push_back(*row);
        }
    }

    std::sort(rows.begin(), rows.end());

    // rows -> spans
    std::vector<span_t> spans;
    spans.reserve(rows.size());
    span_t span;
    bool in_span = false;
    for (auto const& row : rows)
    {
        if (in_span)
        {
            if (span.second + 1 == row)
            {
                span.second = row;
            }
            else
            {
                spans.push_back(span);
                in_span = false;
            }
        }

        if (!in_span)
        {
            span.first = span.second = row;
            in_span = true;
        }
    }

    if (in_span)
    {
        spans.push_back(span);
    }

    return spans;
}

/***
****
***/

void TorrentModel::rowsEmitChanged(torrent_ids_t const& ids)
{
    for (auto const& [first, last] : getSpans(ids))
    {
        emit dataChanged(index(first), index(last));
    }
}

void TorrentModel::rowsAdd(torrents_t const& torrents)
{
    auto const compare = TorrentIdLessThan();

    if (torrents_.empty())
    {
        beginInsertRows(QModelIndex(), 0, torrents.size() - 1);
        torrents_ = torrents;
        std::sort(torrents_.begin(), torrents_.end(), TorrentIdLessThan());
        endInsertRows();
    }
    else
    {
        for (auto const& tor : torrents)
        {
            auto const it = std::lower_bound(torrents_.begin(), torrents_.end(), tor, compare);
            auto const row = static_cast<int>(std::distance(torrents_.begin(), it));

            beginInsertRows(QModelIndex(), row, row);
            torrents_.insert(it, tor);
            endInsertRows();
        }
    }
}

void TorrentModel::rowsRemove(torrents_t const& torrents)
{
    // must walk in reverse to avoid invalidating row numbers
    auto const& spans = getSpans(getIds(torrents.begin(), torrents.end()));
    for (auto it = spans.rbegin(), end = spans.rend(); it != end; ++it)
    {
        auto const& [first, last] = *it;

        beginRemoveRows(QModelIndex(), first, last);
        torrents_.erase(torrents_.begin() + first, torrents_.begin() + last + 1);
        endRemoveRows();
    }

    qDeleteAll(torrents);
}

/***
****
***/

bool TorrentModel::hasTorrent(TorrentHash const& hash) const
{
    auto test = [hash](auto const& tor)
    {
        return tor->hash() == hash;
    };
    return std::any_of(torrents_.cbegin(), torrents_.cend(), test);
}

/***
****
***/

void TorrentModel::recountSoon(Pending const& pending)
{
    pending_ |= pending;

    if (!recount_timer_.isActive())
    {
        recount_timer_.setSingleShot(true);
        recount_timer_.start(800);
    }
}

std::array<int, FilterMode::NUM_MODES> TorrentModel::countTorrentsPerMode() const
{
    std::array<int, FilterMode::NUM_MODES> torrent_counts = {};

    for (auto const& tor : torrents_)
    {
        for (int mode = 0; mode < FilterMode::NUM_MODES; ++mode)
        {
            if (FilterMode::test(*tor, mode))
            {
                ++torrent_counts[mode];
            }
        }
    }

    return torrent_counts;
}

void TorrentModel::recount()
{
    decltype(pending_) pending = {};
    std::swap(pending_, pending);

    if (pending[ACTIVITY])
    {
        refreshActivity();
    }

    if (pending[PATHS])
    {
        refreshPaths();
    }

    if (pending[TRACKERS])
    {
        refreshTrackers();
    }
}

void TorrentModel::onTorrentsChanged(torrent_ids_t const& ids, Torrent::fields_t const& changed_fields)
{
    Q_UNUSED(ids)

    if ((changed_fields & TrackerFields).any())
    {
        recountTrackersSoon();
    }

    if ((changed_fields & PathFields).any())
    {
        recountPathsSoon();
    }

    if ((changed_fields & ActivityFields).any())
    {
        recountActivitySoon();
    }
}

/***
****
***/

void TorrentModel::refreshFilter(Map& map, QStandardItemModel* model, Counts& counts, MapUpdate update, int key)
{
    enum
    {
        ROW_TOTALS = 0,
        ROW_SEPARATOR,
        ROW_FIRST_TRACKER
    };

    // update the "All" row
    auto const num = counts.size();
    auto* item = model->item(ROW_TOTALS);
    item->setData(int(num), TorrentModel::CountRole);
    item->setData(getCountString(num), TorrentModel::CountStringRole);

    auto new_map = Map(counts.begin(), counts.end());
    auto old_it = map.cbegin();
    auto new_it = new_map.cbegin();
    auto const old_end = map.cend();
    auto const new_end = new_map.cend();
    bool any_added = false;
    int row = ROW_FIRST_TRACKER;

    while ((old_it != old_end) || (new_it != new_end))
    {
        if ((old_it == old_end) || ((new_it != new_end) && (old_it->first > new_it->first)))
        {
            model->insertRow(row, update(new QStandardItem(1), new_it));
            any_added = true;
            ++new_it;
            ++row;
        }
        else if ((new_it == new_end) || ((old_it != old_end) && (old_it->first < new_it->first)))
        {
            model->removeRow(row);
            ++old_it;
        }
        else // update
        {
            update(model->item(row), new_it);
            ++old_it;
            ++new_it;
            ++row;
        }
    }

    if (any_added) // the one added might match our filter...
    {
        emit filterChanged(key);
    }

    map.swap(new_map);
}

void TorrentModel::refreshTrackers()
{
    auto torrents_per_sitename = Counts{};
    for (auto const& tor : torrents_)
    {
        for (auto const& sitename : tor->sitenames())
        {
            ++torrents_per_sitename[sitename];
        }
    }

    auto update_tracker_item = [](QStandardItem* i, auto const& it)
    {
        auto const& [sitename, count] = *it;
        auto const display_name = FaviconCache::getDisplayName(sitename);
        auto const icon = trApp->faviconCache().find(sitename);

        i->setData(display_name, Qt::DisplayRole);
        i->setData(display_name, TrackerRole);
        i->setData(getCountString(static_cast<size_t>(count)), TorrentModel::CountStringRole);
        i->setData(icon, Qt::DecorationRole);
        i->setData(int(count), TorrentModel::CountRole);

        return i;
    };

    refreshFilter(sitename_counts_, tracker_model_, torrents_per_sitename, update_tracker_item, Prefs::FILTER_TRACKERS);
}

void TorrentModel::refreshPaths()
{
    auto torrents_per_path = Counts{};
    for (auto const& tor : torrents_)
    {
        ++torrents_per_path[tor->getPath()];
    }

    auto update_path_item = [](QStandardItem* i, auto const& it)
    {
        auto const& displayName = it->first;
        auto const& count = it->second;
        auto const icon = IconCache::get().folderIcon();
        i->setData(displayName, Qt::DisplayRole);
        i->setData(displayName, PathRole);
        i->setData(getCountString(count), TorrentModel::CountStringRole);
        i->setData(icon, Qt::DecorationRole);
        i->setData(int(count), TorrentModel::CountRole);
        return i;
    };

    refreshFilter(path_counts_, path_model_, torrents_per_path, update_path_item, Prefs::FILTER_PATH);
}

void TorrentModel::refreshActivity()
{
    auto const torrents_per_mode = countTorrentsPerMode();
    auto* model = activity_model_;

    for (int row = 0, n = model->rowCount(); row < n; ++row)
    {
        auto const index = model->index(row, 0);
        auto const mode = index.data(ActivityRole).toInt();
        auto const count = torrents_per_mode[mode];
        model->setData(index, count, TorrentModel::CountRole);
        model->setData(index, getCountString(static_cast<size_t>(count)), TorrentModel::CountStringRole);
    }
}

QStandardItemModel* TorrentModel::createActivityModel()
{
    auto* model = new QStandardItemModel(this);

    auto* row = new QStandardItem(tr("All"));
    row->setData(FilterMode::SHOW_ALL, ActivityRole);
    model->appendRow(row);

    model->appendRow(new QStandardItem); // separator

    auto const& icons = IconCache::get();

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("system-run")), tr("Active"));
    row->setData(FilterMode::SHOW_ACTIVE, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("go-down")), tr("Downloading"));
    row->setData(FilterMode::SHOW_DOWNLOADING, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("go-up")), tr("Seeding"));
    row->setData(FilterMode::SHOW_SEEDING, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("media-playback-pause")), tr("Paused"));
    row->setData(FilterMode::SHOW_PAUSED, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("dialog-ok")), tr("Finished"));
    row->setData(FilterMode::SHOW_FINISHED, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("view-refresh")), tr("Verifying"));
    row->setData(FilterMode::SHOW_VERIFYING, ActivityRole);
    model->appendRow(row);

    row = new QStandardItem(icons.getThemeIcon(QStringLiteral("process-stop")), tr("Error"));
    row->setData(FilterMode::SHOW_ERROR, ActivityRole);
    model->appendRow(row);

    return model;
}

QStandardItemModel* TorrentModel::createFilterModel(int role, int count)
{
    auto* model = new QStandardItemModel(this);

    auto* row = new QStandardItem(tr("All"));
    row->setData(QString(), role);
    row->setData(count, TorrentModel::CountRole);
    row->setData(getCountString(static_cast<size_t>(count)), TorrentModel::CountStringRole);
    model->appendRow(row);

    model->appendRow(new QStandardItem); // separator
    return model;
}
