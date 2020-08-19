/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

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

struct TorrentIdLessThan
{
    bool operator ()(Torrent* left, Torrent* right) const
    {
        return left->id() < right->id();
    }

    bool operator ()(int left_id, Torrent* right) const
    {
        return left_id < right->id();
    }

    bool operator ()(Torrent* left, int right_id) const
    {
        return left->id() < right_id;
    }
};

template<typename Iter>
auto getIds(Iter it, Iter end)
{
    torrent_ids_t ids;

    for ( ; it != end; ++it)
    {
        ids.insert((*it)->id());
    }

    return ids;
}

} // namespace

/***
****
***/

TorrentModel::TorrentModel(Prefs const& prefs) :
    prefs_(prefs)
{
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
    QVariant var;

    Torrent const* t = torrents_.value(index.row(), nullptr);

    if (t != nullptr)
    {
        switch (role)
        {
        case Qt::DisplayRole:
            var.setValue(t->name());
            break;

        case Qt::DecorationRole:
            var.setValue(t->getMimeTypeIcon());
            break;

        case TorrentRole:
            var = QVariant::fromValue(t);
            break;

        default:
            // std::cerr << "Unhandled role: " << role << std::endl;
            break;
        }
    }

    return var;
}

/***
****
***/

void TorrentModel::removeTorrents(tr_variant* list)
{
    torrents_t torrents;
    torrents.reserve(tr_variantListSize(list));

    int i = 0;
    tr_variant* child;
    while ((child = tr_variantListChild(list, i++)) != nullptr)
    {
        auto const id = getValue<int>(child);
        if (id)
        {
            auto* torrent = getTorrentFromId(*id);
            if (torrent != nullptr)
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
    auto const recently_added = [now](auto const& tor)
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
        char const* str;
        size_t len;
        size_t i = 0;
        keys.reserve(tr_variantListSize(first_child));
        while (tr_variantGetStr(tr_variantListChild(first_child, i++), &str, &len))
        {
            keys.push_back(tr_quark_new(str, len));
        }
    }
    else
    {
        // In 'object' format, every entry is an object with the same set of properties
        size_t i = 0;
        tr_quark key;
        tr_variant* value;
        while (first_child && tr_variantDictChild(first_child, i++, &key, &value))
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
    tr_variant* v;
    processed.reserve(tr_variantListSize(torrents));
    while ((v = tr_variantListChild(torrents, tor_index++)))
    {
        // Build an array of values
        values.clear();
        if (table)
        {
            // In table mode, v is already a list of values
            size_t i = 0;
            tr_variant* val;
            while ((val = tr_variantListChild(v, i++)))
            {
                values.push_back(val);
            }
        }
        else
        {
            // In object mode, v is an object of torrent property key/vals
            size_t i = 0;
            tr_quark key;
            tr_variant* value;
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

    auto const it = std::equal_range(torrents_.begin(), torrents_.end(), id, TorrentIdLessThan());
    if (it.first != it.second)
    {
        row = std::distance(torrents_.begin(), it.first);
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
    for (auto const& span : getSpans(ids))
    {
        emit dataChanged(index(span.first), index(span.second));
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
            auto* const it = std::lower_bound(torrents_.begin(), torrents_.end(), tor, compare);
            auto const row = std::distance(torrents_.begin(), it);

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
        auto const& span = *it;

        beginRemoveRows(QModelIndex(), span.first, span.second);
        auto const n = span.second + 1 - span.first;
        torrents_.remove(span.first, n);
        endRemoveRows();
    }

    qDeleteAll(torrents);
}

/***
****
***/

bool TorrentModel::hasTorrent(QString const& hash_string) const
{
    auto test = [hash_string](auto const& tor) { return tor->hashString() == hash_string; };
    return std::any_of(torrents_.cbegin(), torrents_.cend(), test);
}
