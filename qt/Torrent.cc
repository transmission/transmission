/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cassert>
#include <iostream>

#include <QApplication>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_new0, tr_strdup */
#include <libtransmission/variant.h>

#include "Application.h"
#include "Prefs.h"
#include "Torrent.h"
#include "Utils.h"

struct Property
{
    int id;
    tr_quark key;
    int type;
};

Property constexpr myProperties[] =
{
    { Torrent::UPLOAD_SPEED, TR_KEY_rateUpload, QVariant::ULongLong } /* Bps */,
    { Torrent::DOWNLOAD_SPEED, TR_KEY_rateDownload, QVariant::ULongLong }, /* Bps */
    { Torrent::DOWNLOAD_DIR, TR_KEY_downloadDir, QVariant::String },
    { Torrent::ACTIVITY, TR_KEY_status, QVariant::Int },
    { Torrent::NAME, TR_KEY_name, QVariant::String },
    { Torrent::ERROR, TR_KEY_error, QVariant::Int },
    { Torrent::ERROR_STRING, TR_KEY_errorString, QVariant::String },
    { Torrent::SIZE_WHEN_DONE, TR_KEY_sizeWhenDone, QVariant::ULongLong },
    { Torrent::LEFT_UNTIL_DONE, TR_KEY_leftUntilDone, QVariant::ULongLong },
    { Torrent::HAVE_UNCHECKED, TR_KEY_haveUnchecked, QVariant::ULongLong },
    { Torrent::HAVE_VERIFIED, TR_KEY_haveValid, QVariant::ULongLong },
    { Torrent::DESIRED_AVAILABLE, TR_KEY_desiredAvailable, QVariant::ULongLong },
    { Torrent::TOTAL_SIZE, TR_KEY_totalSize, QVariant::ULongLong },
    { Torrent::PIECE_SIZE, TR_KEY_pieceSize, QVariant::ULongLong },
    { Torrent::PIECE_COUNT, TR_KEY_pieceCount, QVariant::Int },
    { Torrent::PEERS_GETTING_FROM_US, TR_KEY_peersGettingFromUs, QVariant::Int },
    { Torrent::PEERS_SENDING_TO_US, TR_KEY_peersSendingToUs, QVariant::Int },
    { Torrent::WEBSEEDS_SENDING_TO_US, TR_KEY_webseedsSendingToUs, QVariant::Int },
    { Torrent::PERCENT_DONE, TR_KEY_percentDone, QVariant::Double },
    { Torrent::METADATA_PERCENT_DONE, TR_KEY_metadataPercentComplete, QVariant::Double },
    { Torrent::PERCENT_VERIFIED, TR_KEY_recheckProgress, QVariant::Double },
    { Torrent::DATE_ACTIVITY, TR_KEY_activityDate, QVariant::DateTime },
    { Torrent::DATE_ADDED, TR_KEY_addedDate, QVariant::DateTime },
    { Torrent::DATE_STARTED, TR_KEY_startDate, QVariant::DateTime },
    { Torrent::DATE_CREATED, TR_KEY_dateCreated, QVariant::DateTime },
    { Torrent::PEERS_CONNECTED, TR_KEY_peersConnected, QVariant::Int },
    { Torrent::ETA, TR_KEY_eta, QVariant::Int },
    { Torrent::DOWNLOADED_EVER, TR_KEY_downloadedEver, QVariant::ULongLong },
    { Torrent::UPLOADED_EVER, TR_KEY_uploadedEver, QVariant::ULongLong },
    { Torrent::FAILED_EVER, TR_KEY_corruptEver, QVariant::ULongLong },
    { Torrent::TRACKERSTATS, TR_KEY_trackerStats, CustomVariantType::TrackerStatsList },
    { Torrent::MIME_ICON, TR_KEY_NONE, QVariant::Icon },
    { Torrent::SEED_RATIO_LIMIT, TR_KEY_seedRatioLimit, QVariant::Double },
    { Torrent::SEED_RATIO_MODE, TR_KEY_seedRatioMode, QVariant::Int },
    { Torrent::SEED_IDLE_LIMIT, TR_KEY_seedIdleLimit, QVariant::Int },
    { Torrent::SEED_IDLE_MODE, TR_KEY_seedIdleMode, QVariant::Int },
    { Torrent::DOWN_LIMIT, TR_KEY_downloadLimit, QVariant::Int }, /* KB/s */
    { Torrent::DOWN_LIMITED, TR_KEY_downloadLimited, QVariant::Bool },
    { Torrent::UP_LIMIT, TR_KEY_uploadLimit, QVariant::Int }, /* KB/s */
    { Torrent::UP_LIMITED, TR_KEY_uploadLimited, QVariant::Bool },
    { Torrent::HONORS_SESSION_LIMITS, TR_KEY_honorsSessionLimits, QVariant::Bool },
    { Torrent::PEER_LIMIT, TR_KEY_peer_limit, QVariant::Int },
    { Torrent::HASH_STRING, TR_KEY_hashString, QVariant::String },
    { Torrent::IS_FINISHED, TR_KEY_isFinished, QVariant::Bool },
    { Torrent::IS_PRIVATE, TR_KEY_isPrivate, QVariant::Bool },
    { Torrent::IS_STALLED, TR_KEY_isStalled, QVariant::Bool },
    { Torrent::COMMENT, TR_KEY_comment, QVariant::String },
    { Torrent::CREATOR, TR_KEY_creator, QVariant::String },
    { Torrent::MANUAL_ANNOUNCE_TIME, TR_KEY_manualAnnounceTime, QVariant::DateTime },
    { Torrent::PEERS, TR_KEY_peers, CustomVariantType::PeerList },
    { Torrent::BANDWIDTH_PRIORITY, TR_KEY_bandwidthPriority, QVariant::Int },
    { Torrent::QUEUE_POSITION, TR_KEY_queuePosition, QVariant::Int },
    { Torrent::EDIT_DATE, TR_KEY_editDate, QVariant::Int },
};

/***
****
***/

// unchanging fields needed by the main window
Torrent::KeyList const Torrent::mainInfoKeys{
    TR_KEY_addedDate,
    TR_KEY_downloadDir,
    TR_KEY_hashString,
    TR_KEY_id, // must be in every req
    TR_KEY_name,
    TR_KEY_totalSize,
    TR_KEY_trackers,
};

// changing fields needed by the main window
Torrent::KeyList const Torrent::mainStatKeys{
    TR_KEY_downloadedEver,
    TR_KEY_error,
    TR_KEY_errorString,
    TR_KEY_eta,
    TR_KEY_haveUnchecked,
    TR_KEY_haveValid,
    TR_KEY_id, // must be in every req
    TR_KEY_isFinished,
    TR_KEY_leftUntilDone,
    TR_KEY_manualAnnounceTime,
    TR_KEY_metadataPercentComplete,
    TR_KEY_peersConnected,
    TR_KEY_peersGettingFromUs,
    TR_KEY_peersSendingToUs,
    TR_KEY_percentDone,
    TR_KEY_queuePosition,
    TR_KEY_rateDownload,
    TR_KEY_rateUpload,
    TR_KEY_recheckProgress,
    TR_KEY_seedRatioLimit,
    TR_KEY_seedRatioMode,
    TR_KEY_sizeWhenDone,
    TR_KEY_status,
    TR_KEY_uploadedEver,
    TR_KEY_webseedsSendingToUs
};

Torrent::KeyList const Torrent::allMainKeys = Torrent::mainInfoKeys + Torrent::mainStatKeys;

// unchanging fields needed by the details dialog
Torrent::KeyList const Torrent::detailInfoKeys{
    TR_KEY_comment,
    TR_KEY_creator,
    TR_KEY_dateCreated,
    TR_KEY_files,
    TR_KEY_id, // must be in every req
    TR_KEY_isPrivate,
    TR_KEY_pieceCount,
    TR_KEY_pieceSize,
    TR_KEY_trackers
};

// changing fields needed by the details dialog
Torrent::KeyList const Torrent::detailStatKeys{
    TR_KEY_activityDate,
    TR_KEY_bandwidthPriority,
    TR_KEY_corruptEver,
    TR_KEY_desiredAvailable,
    TR_KEY_downloadedEver,
    TR_KEY_downloadLimit,
    TR_KEY_downloadLimited,
    TR_KEY_fileStats,
    TR_KEY_honorsSessionLimits,
    TR_KEY_id, // must be in every req
    TR_KEY_peer_limit,
    TR_KEY_peers,
    TR_KEY_seedIdleLimit,
    TR_KEY_seedIdleMode,
    TR_KEY_startDate,
    TR_KEY_trackerStats,
    TR_KEY_uploadLimit,
    TR_KEY_uploadLimited
};

/***
****
***/

Torrent::Torrent(Prefs const& prefs, int id) :
    myId(id),
    myPrefs(prefs)
{
    static_assert(TR_N_ELEMENTS(myProperties) == PROPERTY_COUNT);

    static_assert(([] () constexpr
    {
        int i = 0;

        for (auto const& property : myProperties)
        {
            if (property.id != i)
            {
                return false;
            }

            ++i;
        }

        return true;
    })());

    setIcon(MIME_ICON, Utils::getFileIcon());
}

/***
****
***/

bool Torrent::setInt(int i, int value)
{
    bool changed = false;

    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::Int);

    if (myValues[i].isNull() || myValues[i].toInt() != value)
    {
        myValues[i].setValue(value);
        changed = true;
    }

    return changed;
}

bool Torrent::setBool(int i, bool value)
{
    bool changed = false;

    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::Bool);

    if (myValues[i].isNull() || myValues[i].toBool() != value)
    {
        myValues[i].setValue(value);
        changed = true;
    }

    return changed;
}

bool Torrent::setDouble(int i, double value)
{
    bool changed = false;

    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::Double);

    if (myValues[i] != value)
    {
        myValues[i].setValue(value);
        changed = true;
    }

    return changed;
}

bool Torrent::setTime(int i, time_t value)
{
    bool changed = false;

    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::DateTime);

    auto& oldval = myValues[i];
    auto const newval = qlonglong(value);

    if (oldval != newval)
    {
        oldval = newval;
        changed = true;
    }

    return changed;
}

bool Torrent::setSize(int i, qulonglong value)
{
    bool changed = false;

    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::ULongLong);

    if (myValues[i].isNull() || myValues[i].toULongLong() != value)
    {
        myValues[i].setValue(value);
        changed = true;
    }

    return changed;
}

bool Torrent::setString(int i, char const* value, size_t len)
{
    bool changed = false;

    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::String);

    auto& oldval = myValues[i];
    auto const newval = QString::fromUtf8(value, len);

    if (oldval != newval)
    {
        oldval = newval;
        changed = true;
    }

    return changed;
}

bool Torrent::setIcon(int i, QIcon const& value)
{
    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::Icon);

    myValues[i].setValue(value);
    return true;
}

int Torrent::getInt(int i) const
{
    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::Int);
    // assert(!myValues[i].isNull());

    return myValues[i].toInt();
}

time_t Torrent::getTime(int i) const
{
    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::DateTime);
    // assert((i == DATE_ADDED) || !myValues[i].isNull());

    return time_t(myValues[i].toLongLong());
}

bool Torrent::getBool(int i) const
{
    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::Bool);
    // assert(!myValues[i].isNull());

    return myValues[i].toBool();
}

qulonglong Torrent::getSize(int i) const
{
    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::ULongLong);
    // assert(!myValues[i].isNull());

    return myValues[i].toULongLong();
}

double Torrent::getDouble(int i) const
{
    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::Double);
    // assert(!myValues[i].isNull());

    return myValues[i].toDouble();
}

QString Torrent::getString(int i) const
{
    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::String);
    // assert((i == HASH_STRING) || !myValues[i].isNull());

    return myValues[i].toString();
}

QIcon Torrent::getIcon(int i) const
{
    assert(0 <= i && i < PROPERTY_COUNT);
    assert(myProperties[i].type == QVariant::Icon);
    // assert(!myValues[i].isNull());

    return myValues[i].value<QIcon>();
}

/***
****
***/

bool Torrent::getSeedRatio(double& ratio) const
{
    bool isLimited;

    switch (seedRatioMode())
    {
    case TR_RATIOLIMIT_SINGLE:
        isLimited = true;
        ratio = seedRatioLimit();
        break;

    case TR_RATIOLIMIT_GLOBAL:
        if ((isLimited = myPrefs.getBool(Prefs::RATIO_ENABLED)))
        {
            ratio = myPrefs.getDouble(Prefs::RATIO);
        }

        break;

    default: // TR_RATIOLIMIT_UNLIMITED:
        isLimited = false;
        break;
    }

    return isLimited;
}

bool Torrent::hasTrackerSubstring(QString const& substr) const
{
    for (auto const& s : trackers())
    {
        if (s.contains(substr, Qt::CaseInsensitive))
        {
            return true;
        }
    }

    return false;
}

int Torrent::compareSeedRatio(Torrent const& that) const
{
    double a;
    double b;
    bool const has_a = getSeedRatio(a);
    bool const has_b = that.getSeedRatio(b);

    if (!has_a && !has_b)
    {
        return 0;
    }

    if (!has_a || !has_b)
    {
        return has_a ? -1 : 1;
    }

    if (a < b)
    {
        return -1;
    }

    if (a > b)
    {
        return 1;
    }

    return 0;
}

int Torrent::compareRatio(Torrent const& that) const
{
    double const a = ratio();
    double const b = that.ratio();

    if (static_cast<int>(a) == TR_RATIO_INF && static_cast<int>(b) == TR_RATIO_INF)
    {
        return 0;
    }

    if (static_cast<int>(a) == TR_RATIO_INF)
    {
        return 1;
    }

    if (static_cast<int>(b) == TR_RATIO_INF)
    {
        return -1;
    }

    if (a < b)
    {
        return -1;
    }

    if (a > b)
    {
        return 1;
    }

    return 0;
}

int Torrent::compareETA(Torrent const& that) const
{
    bool const haveA(hasETA());
    bool const haveB(that.hasETA());

    if (haveA && haveB)
    {
        return getETA() - that.getETA();
    }

    if (haveA)
    {
        return 1;
    }

    if (haveB)
    {
        return -1;
    }

    return 0;
}

/***
****
***/

void Torrent::updateMimeIcon()
{
    FileList const& files(myFiles);

    QIcon icon;

    if (files.size() > 1)
    {
        icon = Utils::getFolderIcon();
    }
    else if (files.size() == 1)
    {
        icon = Utils::guessMimeIcon(files.at(0).filename);
    }
    else
    {
        icon = Utils::guessMimeIcon(name());
    }

    setIcon(MIME_ICON, icon);
}

/***
****
***/

bool Torrent::update(tr_quark const* keys, tr_variant** values, size_t n)
{
    static bool lookup_initialized = false;
    static int key_to_property_index[TR_N_KEYS];
    bool changed = false;

    if (!lookup_initialized)
    {
        lookup_initialized = true;

        for (int i = 0; i < TR_N_KEYS; ++i)
        {
            key_to_property_index[i] = -1;
        }

        for (int i = 0; i < PROPERTY_COUNT; i++)
        {
            key_to_property_index[myProperties[i].key] = i;
        }
    }

    for (size_t pos = 0; pos < n; ++pos)
    {
        tr_quark key = keys[pos];
        tr_variant* child = values[pos];

        int const property_index = key_to_property_index[key];

        if (property_index == -1) // we're not interested in this one
        {
            continue;
        }

        assert(myProperties[property_index].key == key);

        switch (myProperties[property_index].type)
        {
        case QVariant::Int:
            {
                int64_t val;

                if (tr_variantGetInt(child, &val))
                {
                    changed |= setInt(property_index, val);
                }

                break;
            }

        case QVariant::Bool:
            {
                bool val;

                if (tr_variantGetBool(child, &val))
                {
                    changed |= setBool(property_index, val);
                }

                break;
            }

        case QVariant::String:
            {
                char const* val;
                size_t len;

                if (tr_variantGetStr(child, &val, &len))
                {
                    bool const field_changed = setString(property_index, val, len);
                    changed |= field_changed;

                    if (field_changed && key == TR_KEY_name)
                    {
                        updateMimeIcon();
                    }
                }

                break;
            }

        case QVariant::ULongLong:
            {
                int64_t val;

                if (tr_variantGetInt(child, &val))
                {
                    changed |= setSize(property_index, val);
                }

                break;
            }

        case QVariant::Double:
            {
                double val;

                if (tr_variantGetReal(child, &val))
                {
                    changed |= setDouble(property_index, val);
                }

                break;
            }

        case QVariant::DateTime:
            {
                int64_t val;
                if (tr_variantGetInt(child, &val) && val &&
                    setTime(property_index, time_t(val)))
                {
                    changed = true;

                    if (key == TR_KEY_editDate)
                    {
                        // FIXME
                        // emit torrentEdited(*this);
                    }
                }

                break;
            }

        case CustomVariantType::PeerList:
            // handled below
            break;

        default:
            std::cerr << __FILE__ << ':' << __LINE__ << "unhandled type: " << tr_quark_get_string(key, nullptr) << std::endl;
            assert(false && "unhandled type");
        }
    }

    auto it = std::find(keys, keys + n, TR_KEY_files);
    if (it != keys + n)
    {
        tr_variant* files = values[std::distance(keys, it)];
        char const* str;
        int64_t intVal;
        int i = 0;
        tr_variant* child;

        myFiles.clear();
        myFiles.reserve(tr_variantListSize(files));

        while ((child = tr_variantListChild(files, i)) != nullptr)
        {
            TorrentFile file;
            size_t len;
            file.index = i++;

            if (tr_variantDictFindStr(child, TR_KEY_name, &str, &len))
            {
                file.filename = QString::fromUtf8(str, len);
            }

            if (tr_variantDictFindInt(child, TR_KEY_length, &intVal))
            {
                file.size = intVal;
            }

            myFiles.append(file);
        }

        updateMimeIcon();
        changed = true;
    }

    it = std::find(keys, keys + n, TR_KEY_fileStats);
    if (it != keys + n)
    {
        tr_variant* files = values[std::distance(keys, it)];
        int const n = tr_variantListSize(files);

        for (int i = 0; i < n && i < myFiles.size(); ++i)
        {
            int64_t intVal;
            bool boolVal;
            tr_variant* child = tr_variantListChild(files, i);
            TorrentFile& file(myFiles[i]);

            if (tr_variantDictFindInt(child, TR_KEY_bytesCompleted, &intVal))
            {
                file.have = intVal;
            }

            if (tr_variantDictFindBool(child, TR_KEY_wanted, &boolVal))
            {
                file.wanted = boolVal;
            }

            if (tr_variantDictFindInt(child, TR_KEY_priority, &intVal))
            {
                file.priority = intVal;
            }
        }

        changed = true;
    }

    it = std::find(keys, keys + n, TR_KEY_trackers);
    if (it != keys + n)
    {
        tr_variant* v = values[std::distance(keys, it)];

        // build the new tracker list
        QStringList trackers;
        trackers.reserve(tr_variantListSize(v));
        tr_variant* child;
        int i = 0;
        while ((child = tr_variantListChild(v, i++)) != nullptr)
        {
            char const* str;
            size_t len;
            if (tr_variantDictFindStr(child, TR_KEY_announce, &str, &len))
            {
                trackers.append(QString::fromUtf8(str, len));
            }
        }

        // update the trackers
        if (trackers_ != trackers)
        {
            QStringList displayNames;
            displayNames.reserve(trackers.size());
            for (auto const& tracker : trackers)
            {
                auto const url = QUrl(tracker);
                auto const key = qApp->faviconCache().add(url);
                displayNames.append(FaviconCache::getDisplayName(key));
            }

            displayNames.removeDuplicates();

            trackers_.swap(trackers);
            trackerDisplayNames_.swap(displayNames);
            changed = true;
        }
    }

    it = std::find(keys, keys + n, TR_KEY_trackerStats);
    if (it != keys + n)
    {
        tr_variant* trackerStats = values[std::distance(keys, it)];
        tr_variant* child;
        TrackerStatsList trackerStatsList;
        int childNum = 0;

        while ((child = tr_variantListChild(trackerStats, childNum++)) != nullptr)
        {
            bool b;
            int64_t i;
            size_t len;
            char const* str;
            TrackerStat trackerStat;

            if (tr_variantDictFindStr(child, TR_KEY_announce, &str, &len))
            {
                trackerStat.announce = QString::fromUtf8(str, len);
            }

            if (tr_variantDictFindInt(child, TR_KEY_announceState, &i))
            {
                trackerStat.announceState = i;
            }

            if (tr_variantDictFindInt(child, TR_KEY_downloadCount, &i))
            {
                trackerStat.downloadCount = i;
            }

            if (tr_variantDictFindBool(child, TR_KEY_hasAnnounced, &b))
            {
                trackerStat.hasAnnounced = b;
            }

            if (tr_variantDictFindBool(child, TR_KEY_hasScraped, &b))
            {
                trackerStat.hasScraped = b;
            }

            if (tr_variantDictFindStr(child, TR_KEY_host, &str, &len))
            {
                trackerStat.host = QString::fromUtf8(str, len);
            }

            if (tr_variantDictFindInt(child, TR_KEY_id, &i))
            {
                trackerStat.id = i;
            }

            if (tr_variantDictFindBool(child, TR_KEY_isBackup, &b))
            {
                trackerStat.isBackup = b;
            }

            if (tr_variantDictFindInt(child, TR_KEY_lastAnnouncePeerCount, &i))
            {
                trackerStat.lastAnnouncePeerCount = i;
            }

            if (tr_variantDictFindStr(child, TR_KEY_lastAnnounceResult, &str, &len))
            {
                trackerStat.lastAnnounceResult = QString::fromUtf8(str, len);
            }

            if (tr_variantDictFindInt(child, TR_KEY_lastAnnounceStartTime, &i))
            {
                trackerStat.lastAnnounceStartTime = i;
            }

            if (tr_variantDictFindBool(child, TR_KEY_lastAnnounceSucceeded, &b))
            {
                trackerStat.lastAnnounceSucceeded = b;
            }

            if (tr_variantDictFindInt(child, TR_KEY_lastAnnounceTime, &i))
            {
                trackerStat.lastAnnounceTime = i;
            }

            if (tr_variantDictFindBool(child, TR_KEY_lastAnnounceTimedOut, &b))
            {
                trackerStat.lastAnnounceTimedOut = b;
            }

            if (tr_variantDictFindStr(child, TR_KEY_lastScrapeResult, &str, &len))
            {
                trackerStat.lastScrapeResult = QString::fromUtf8(str, len);
            }

            if (tr_variantDictFindInt(child, TR_KEY_lastScrapeStartTime, &i))
            {
                trackerStat.lastScrapeStartTime = i;
            }

            if (tr_variantDictFindBool(child, TR_KEY_lastScrapeSucceeded, &b))
            {
                trackerStat.lastScrapeSucceeded = b;
            }

            if (tr_variantDictFindInt(child, TR_KEY_lastScrapeTime, &i))
            {
                trackerStat.lastScrapeTime = i;
            }

            if (tr_variantDictFindBool(child, TR_KEY_lastScrapeTimedOut, &b))
            {
                trackerStat.lastScrapeTimedOut = b;
            }

            if (tr_variantDictFindInt(child, TR_KEY_leecherCount, &i))
            {
                trackerStat.leecherCount = i;
            }

            if (tr_variantDictFindInt(child, TR_KEY_nextAnnounceTime, &i))
            {
                trackerStat.nextAnnounceTime = i;
            }

            if (tr_variantDictFindInt(child, TR_KEY_nextScrapeTime, &i))
            {
                trackerStat.nextScrapeTime = i;
            }

            if (tr_variantDictFindInt(child, TR_KEY_scrapeState, &i))
            {
                trackerStat.scrapeState = i;
            }

            if (tr_variantDictFindInt(child, TR_KEY_seederCount, &i))
            {
                trackerStat.seederCount = i;
            }

            if (tr_variantDictFindInt(child, TR_KEY_tier, &i))
            {
                trackerStat.tier = i;
            }

            trackerStatsList << trackerStat;
        }

        myValues[TRACKERSTATS].setValue(trackerStatsList);
        changed = true;
    }

    it = std::find(keys, keys + n, TR_KEY_peers);
    if (it != keys + n)
    {
        tr_variant* peers = values[std::distance(keys, it)];
        tr_variant* child;
        PeerList peerList;
        int childNum = 0;

        while ((child = tr_variantListChild(peers, childNum++)) != nullptr)
        {
            double d;
            bool b;
            int64_t i;
            size_t len;
            char const* str;
            Peer peer;

            if (tr_variantDictFindStr(child, TR_KEY_address, &str, &len))
            {
                peer.address = QString::fromUtf8(str, len);
            }

            if (tr_variantDictFindStr(child, TR_KEY_clientName, &str, &len))
            {
                peer.clientName = QString::fromUtf8(str, len);
            }

            if (tr_variantDictFindBool(child, TR_KEY_clientIsChoked, &b))
            {
                peer.clientIsChoked = b;
            }

            if (tr_variantDictFindBool(child, TR_KEY_clientIsInterested, &b))
            {
                peer.clientIsInterested = b;
            }

            if (tr_variantDictFindStr(child, TR_KEY_flagStr, &str, &len))
            {
                peer.flagStr = QString::fromUtf8(str, len);
            }

            if (tr_variantDictFindBool(child, TR_KEY_isDownloadingFrom, &b))
            {
                peer.isDownloadingFrom = b;
            }

            if (tr_variantDictFindBool(child, TR_KEY_isEncrypted, &b))
            {
                peer.isEncrypted = b;
            }

            if (tr_variantDictFindBool(child, TR_KEY_isIncoming, &b))
            {
                peer.isIncoming = b;
            }

            if (tr_variantDictFindBool(child, TR_KEY_isUploadingTo, &b))
            {
                peer.isUploadingTo = b;
            }

            if (tr_variantDictFindBool(child, TR_KEY_peerIsChoked, &b))
            {
                peer.peerIsChoked = b;
            }

            if (tr_variantDictFindBool(child, TR_KEY_peerIsInterested, &b))
            {
                peer.peerIsInterested = b;
            }

            if (tr_variantDictFindInt(child, TR_KEY_port, &i))
            {
                peer.port = i;
            }

            if (tr_variantDictFindReal(child, TR_KEY_progress, &d))
            {
                peer.progress = d;
            }

            if (tr_variantDictFindInt(child, TR_KEY_rateToClient, &i))
            {
                peer.rateToClient = Speed::fromBps(i);
            }

            if (tr_variantDictFindInt(child, TR_KEY_rateToPeer, &i))
            {
                peer.rateToPeer = Speed::fromBps(i);
            }

            peerList << peer;
        }

        myValues[PEERS].setValue(peerList);
        changed = true;
    }

    return changed;
}

QString Torrent::activityString() const
{
    QString str;

    switch (getActivity())
    {
    case TR_STATUS_STOPPED:
        str = isFinished() ? tr("Finished") : tr("Paused");
        break;

    case TR_STATUS_CHECK_WAIT:
        str = tr("Queued for verification");
        break;

    case TR_STATUS_CHECK:
        str = tr("Verifying local data");
        break;

    case TR_STATUS_DOWNLOAD_WAIT:
        str = tr("Queued for download");
        break;

    case TR_STATUS_DOWNLOAD:
        str = tr("Downloading");
        break;

    case TR_STATUS_SEED_WAIT:
        str = tr("Queued for seeding");
        break;

    case TR_STATUS_SEED:
        str = tr("Seeding");
        break;
    }

    return str;
}

QString Torrent::getError() const
{
    QString s = getString(ERROR_STRING);

    switch (getInt(ERROR))
    {
    case TR_STAT_TRACKER_WARNING:
        s = tr("Tracker gave a warning: %1").arg(s);
        break;

    case TR_STAT_TRACKER_ERROR:
        s = tr("Tracker gave an error: %1").arg(s);
        break;

    case TR_STAT_LOCAL_ERROR:
        s = tr("Error: %1").arg(s);
        break;

    default:
        s.clear();
        break;
    }

    return s;
}

QPixmap TrackerStat::getFavicon() const
{
    return qApp->faviconCache().find(QUrl(announce));
}
