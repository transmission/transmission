/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <iostream>
#include <utility>

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include "Speed.h"
#include "Torrent.h"
#include "TorrentDelegate.h"
#include "TorrentModel.h"

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

    bool operator ()(int leftId, Torrent* right) const
    {
        return leftId < right->id();
    }

    bool operator ()(Torrent* left, int rightId) const
    {
        return left->id() < rightId;
    }
};

template<typename Iter>
QSet<int> getIds (Iter it, Iter end)
{
    QSet<int> ids;

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
    myPrefs(prefs)
{
}

TorrentModel::~TorrentModel()
{
    clear();
}

void TorrentModel::clear()
{
    beginResetModel();
    qDeleteAll(myTorrents);
    myTorrents.clear();
    endResetModel();
}

int TorrentModel::rowCount(QModelIndex const& parent) const
{
    Q_UNUSED(parent);

    return myTorrents.size();
}

QVariant TorrentModel::data(QModelIndex const& index, int role) const
{
    QVariant var;

    Torrent const* t = myTorrents.value(index.row(), nullptr);

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
            var = qVariantFromValue(t);
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
    int i = 0;
    tr_variant* child;
    QSet<Torrent*> torrents;

    while ((child = tr_variantListChild(list, i++)) != nullptr)
    {
        int64_t id;
        Torrent* torrent = nullptr;

        if (tr_variantGetInt(child, &id))
        {
            torrent = getTorrentFromId(id);
        }

        if (torrent != nullptr)
        {
            torrents.insert(torrent);
        }
    }

    if (!torrents.isEmpty())
    {
        rowsRemove(torrents);
    }
}

void TorrentModel::updateTorrents(tr_variant* torrents, bool isCompleteList)
{
    auto const old = QSet<Torrent*>::fromList(myTorrents.toList());
    auto added = QSet<int>{};
    auto changed = QSet<int>{};
    auto completed = QSet<int>{};
    auto instantiated = torrents_t{};
    auto needinfo = QSet<int>{};
    auto processed = QSet<Torrent*>{};

    auto const now = time(nullptr);
    auto const recently_added = [now](const auto& tor)
        {
            static auto constexpr max_age = 60;
            auto const date = tor->dateAdded();
            return (date != 0) && (difftime(now,date) < max_age);
        };

    size_t i = 0;
    tr_variant* child;
    while ((child = tr_variantListChild(torrents, i++)) != nullptr)
    {
        int64_t id;

        if (!tr_variantDictFindInt(child, TR_KEY_id, &id))
        {
            continue;
        }

        Torrent* tor = getTorrentFromId(id);
        std::optional<uint64_t> leftUntilDone;

        if (tor == nullptr)
        {
            tor = new Torrent(myPrefs, id);
            instantiated.push_back(tor);
        }
        else
        {
            leftUntilDone = tor->leftUntilDone();
        }

        if (tor->update(child))
        {
            changed.insert(id);
        }

        if (!tor->hasName() && !old.contains(tor))
        {
            needinfo.insert(id);
        }

        if (recently_added(tor) && tor->hasName() && !myAlreadyAdded.contains(id))
        {
            added.insert(id);
            myAlreadyAdded.insert(id);
        }

        if (leftUntilDone && (*leftUntilDone > 0) && (tor->leftUntilDone() == 0) && (tor->downloadedEver() > 0))
        {
            completed.insert(id);
        }

        processed.insert(tor);
    }

    // model upkeep

    if (!instantiated.isEmpty())
    {
        rowsAdd(instantiated);
    }
    
    if (!changed.isEmpty())
    {
        rowsEmitChanged(changed);
    }

    // emit signals

    if (!added.isEmpty())
    {
        emit torrentsAdded(added);
    }

    if (!needinfo.isEmpty())
    {
        emit torrentsNeedInfo(needinfo);
    }

    if (!changed.isEmpty())
    {
        emit torrentsChanged(changed);
    }

    if (!completed.isEmpty())
    {
        emit torrentsCompleted(completed);
    }

    // model upkeep

    if (isCompleteList)
    {
        auto const removed = old - processed;
        if (!removed.isEmpty())
        {
            rowsRemove(removed);
        }
    }
}

/***
****
***/

std::optional<int> TorrentModel::getRow(int id) const
{
    std::optional<int> row;

    auto const it = std::equal_range(myTorrents.begin(), myTorrents.end(), id, TorrentIdLessThan());
    if (it.first != it.second)
    {
        row = std::distance(myTorrents.begin(), it.first);
        assert(myTorrents[*row]->id() == id);
    }
    
    return row;
}

std::optional<int> TorrentModel::getRow(Torrent const* tor) const
{
    return getRow(tor->id());
}

Torrent* TorrentModel::getTorrentFromId(int id)
{
    auto const row = getRow(id);
    return row ? myTorrents[*row] : nullptr;
}

Torrent const* TorrentModel::getTorrentFromId(int id) const
{
    auto const row = getRow(id);
    return row ? myTorrents[*row] : nullptr;
}

/***
****
***/

std::vector<TorrentModel::span_t> TorrentModel::getSpans(QSet<int> const& ids) const
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

void TorrentModel::rowsEmitChanged(QSet<int> const& ids)
{
    for (const auto& span : getSpans(ids))
    {
        emit dataChanged(index(span.first), index(span.second));
    }
}

void TorrentModel::rowsAdd(torrents_t const& torrents)
{
    auto const compare = TorrentIdLessThan();

    if (myTorrents.isEmpty())
    {
        beginInsertRows(QModelIndex(), 0, torrents.size() - 1);
        myTorrents = torrents;
        std::sort(myTorrents.begin(), myTorrents.end(), TorrentIdLessThan());
        endInsertRows();
    }
    else for (auto const& tor : torrents)
    {
        auto const it = std::lower_bound(myTorrents.begin(), myTorrents.end(), tor, compare);
        auto const row = std::distance(myTorrents.begin(), it);

        beginInsertRows(QModelIndex(), row, row);
        myTorrents.insert(it, tor);
        endInsertRows();
    }
}

void TorrentModel::rowsRemove(QSet<Torrent*> const& torrents)
{
    // must walk in reverse to avoid invalidating row numbers
    auto const& spans = getSpans(getIds(torrents.begin(), torrents.end()));
    for (auto it = spans.rbegin(), end = spans.rend(); it != end; ++it)
    {
        auto const& span = *it;

        beginRemoveRows(QModelIndex(), span.first, span.second);
        auto const n = span.second + 1 - span.first;
        myTorrents.remove(span.first, n);
        endRemoveRows();
    }

    qDeleteAll(torrents);
}

/***
****
***/

void TorrentModel::getTransferSpeed(Speed& uploadSpeed, size_t& uploadPeerCount, Speed& downloadSpeed,
    size_t& downloadPeerCount) const
{
    Speed upSpeed;
    Speed downSpeed;
    size_t upCount = 0;
    size_t downCount = 0;

    for (Torrent const* const tor : myTorrents)
    {
        upSpeed += tor->uploadSpeed();
        upCount += tor->peersWeAreUploadingTo();
        downSpeed += tor->downloadSpeed();
        downCount += tor->webseedsWeAreDownloadingFrom();
        downCount += tor->peersWeAreDownloadingFrom();
    }

    uploadSpeed = upSpeed;
    uploadPeerCount = upCount;
    downloadSpeed = downSpeed;
    downloadPeerCount = downCount;
}

bool TorrentModel::hasTorrent(QString const& hashString) const
{
    auto test = [hashString](auto const& tor){return tor->hashString() == hashString;};
    return std::any_of(myTorrents.cbegin(), myTorrents.cend(), test);
}
