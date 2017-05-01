/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <iostream>

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include "Speed.h"
#include "Torrent.h"
#include "TorrentDelegate.h"
#include "TorrentModel.h"

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

} // namespace

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

void TorrentModel::addTorrent(Torrent* t)
{
    torrents_t::iterator const torrentIt = qLowerBound(myTorrents.begin(), myTorrents.end(), t, TorrentIdLessThan());
    int const row = torrentIt == myTorrents.end() ? myTorrents.size() : torrentIt - myTorrents.begin();

    beginInsertRows(QModelIndex(), row, row);
    myTorrents.insert(torrentIt, t);
    endInsertRows();
}

void TorrentModel::addTorrents(torrents_t&& torrents, QSet<int>& addIds)
{
    if (myTorrents.isEmpty())
    {
        qSort(torrents.begin(), torrents.end(), TorrentIdLessThan());

        beginInsertRows(QModelIndex(), 0, torrents.size() - 1);
        myTorrents.swap(torrents);
        endInsertRows();

        addIds += getIds();
    }
    else
    {
        for (Torrent* const tor : torrents)
        {
            addTorrent(tor);
            addIds.insert(tor->id());
        }
    }
}

TorrentModel::TorrentModel(Prefs const& prefs) :
    myPrefs(prefs)
{
}

TorrentModel::~TorrentModel()
{
    clear();
}

/***
****
***/

Torrent* TorrentModel::getTorrentFromId(int id)
{
    torrents_t::const_iterator const torrentIt = qBinaryFind(myTorrents.begin(), myTorrents.end(), id, TorrentIdLessThan());
    return torrentIt == myTorrents.end() ? nullptr : *torrentIt;
}

Torrent const* TorrentModel::getTorrentFromId(int id) const
{
    torrents_t::const_iterator const torrentIt = qBinaryFind(myTorrents.begin(), myTorrents.end(), id, TorrentIdLessThan());
    return torrentIt == myTorrents.end() ? nullptr : *torrentIt;
}

/***
****
***/

void TorrentModel::onTorrentChanged(int torrentId)
{
    torrents_t::iterator const torrentIt = qBinaryFind(myTorrents.begin(), myTorrents.end(), torrentId, TorrentIdLessThan());

    if (torrentIt == myTorrents.end())
    {
        return;
    }

    int const row = torrentIt - myTorrents.begin();
    QModelIndex const qmi(index(row, 0));

    emit dataChanged(qmi, qmi);
}

void TorrentModel::removeTorrents(tr_variant* torrents)
{
    int i = 0;
    tr_variant* child;

    while ((child = tr_variantListChild(torrents, i++)) != nullptr)
    {
        int64_t intVal;

        if (tr_variantGetInt(child, &intVal))
        {
            removeTorrent(intVal);
        }
    }
}

void TorrentModel::updateTorrents(tr_variant* torrents, bool isCompleteList)
{
    torrents_t newTorrents;
    QSet<int> oldIds;
    QSet<int> addIds;
    QSet<int> newIds;

    if (isCompleteList)
    {
        oldIds = getIds();
    }

    if (tr_variantIsList(torrents))
    {
        size_t i(0);
        tr_variant* child;

        while ((child = tr_variantListChild(torrents, i++)) != nullptr)
        {
            int64_t id;

            if (tr_variantDictFindInt(child, TR_KEY_id, &id))
            {
                if (isCompleteList)
                {
                    newIds.insert(id);
                }

                Torrent* tor = getTorrentFromId(id);

                if (tor == nullptr)
                {
                    tor = new Torrent(myPrefs, id);
                    tor->update(child);

                    if (!tor->hasMetadata())
                    {
                        tor->setMagnet(true);
                    }

                    newTorrents.append(tor);
                    connect(tor, SIGNAL(torrentChanged(int)), this, SLOT(onTorrentChanged(int)));
                }
                else
                {
                    tor->update(child);

                    if (tor->isMagnet() && tor->hasMetadata())
                    {
                        addIds.insert(tor->id());
                        tor->setMagnet(false);
                    }
                }
            }
        }
    }

    if (!newTorrents.isEmpty())
    {
        addTorrents(std::move(newTorrents), addIds);
    }

    if (!addIds.isEmpty())
    {
        emit torrentsAdded(addIds);
    }

    if (isCompleteList)
    {
        QSet<int> removedIds(oldIds);
        removedIds -= newIds;

        for (int const id : removedIds)
        {
            removeTorrent(id);
        }
    }
}

void TorrentModel::removeTorrent(int id)
{
    torrents_t::iterator const torrentIt = qBinaryFind(myTorrents.begin(), myTorrents.end(), id, TorrentIdLessThan());

    if (torrentIt == myTorrents.end())
    {
        return;
    }

    Torrent* const tor = *torrentIt;
    int const row = torrentIt - myTorrents.begin();

    beginRemoveRows(QModelIndex(), row, row);
    myTorrents.remove(row);
    endRemoveRows();

    delete tor;
}

void TorrentModel::getTransferSpeed(Speed& uploadSpeed, size_t& uploadPeerCount, Speed& downloadSpeed,
    size_t& downloadPeerCount)
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

QSet<int> TorrentModel::getIds() const
{
    QSet<int> ids;

    ids.reserve(myTorrents.size());

    for (Torrent const* const tor : myTorrents)
    {
        ids.insert(tor->id());
    }

    return ids;
}

bool TorrentModel::hasTorrent(QString const& hashString) const
{
    for (Torrent const* const tor : myTorrents)
    {
        if (tor->hashString() == hashString)
        {
            return true;
        }
    }

    return false;
}
