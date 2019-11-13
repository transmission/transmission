/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cassert>
#include <iostream>
#include <type_traits>

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

Torrent::Torrent(Prefs const& prefs, int id) :
    prefs_(prefs),
    id_(id),
    icon_(Utils::getFileIcon())
{
}

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
    TR_KEY_editDate,
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
        if ((isLimited = prefs_.getBool(Prefs::RATIO_ENABLED)))
        {
            ratio = prefs_.getDouble(Prefs::RATIO);
        }

        break;

    default: // TR_RATIOLIMIT_UNLIMITED:
        isLimited = false;
        break;
    }

    return isLimited;
}

bool Torrent::hasFileSubstring(QString const& substr) const
{
    for (auto const& file : files_)
    {
        if (file.filename.contains(substr, Qt::CaseInsensitive))
        {
            return true;
        }
    }

    return false;
}

bool Torrent::hasTrackerSubstring(QString const& substr) const
{
    for (auto const& s : trackers())
    {
        if (s.displayName.contains(substr, Qt::CaseInsensitive))
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
    if (!hasETA())
    {
        return -1;
    }
    else if (!that.hasETA())
    {
        return 1;
    }
    else if (eta() < that.eta())
    {
        return -1;
    }
    else if (eta() > that.eta())
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

int Torrent::compareTracker(Torrent const& that) const
{
    Q_UNUSED(that)

    // FIXME
    return 0;
}

/***
****
***/

void Torrent::updateMimeIcon()
{
    auto const& files = files_;

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

    icon_ = icon;
}

/***
****
***/

namespace
{

template<typename T>
bool change(T& setme, T const& value)
{
    bool changed = false;

    if (setme != value)
    {
        setme = value;
        changed = true;
    }

    return changed;
}

bool change(double& setme, double const& value)
{
    bool changed = false;

    if (!qFuzzyCompare(setme, value))
    {
        setme = value;
        changed = true;
    }

    return changed;
}

bool change(QString& setme, tr_variant* v)
{
    char const* str;
    size_t len;
    return tr_variantGetStr(v, &str, &len) && change(setme, QString::fromUtf8(str, len));
}

bool change(double& setme, tr_variant* v)
{
    double value;
    return tr_variantGetReal(v, &value) && change(setme, value);
}

bool change(bool& setme, tr_variant* v)
{
    bool value;
    return tr_variantGetBool(v, &value) && change(setme, value);
}

template<typename T, typename std::enable_if<
    std::is_same<T, Bps_t>::value ||
    std::is_same<T, KBps_t>::value ||
    std::is_same<T, bytes_t>::value ||
    std::is_same<T, int64_t>::value ||
    std::is_same<T, int>::value ||
    std::is_same<T, seconds_t>::value ||
    std::is_same<T, time_t>::value ||
    std::is_same<T, tr_idlelimit>::value ||
    std::is_same<T, tr_ratiolimit>::value ||
    std::is_same<T, tr_stat_errtype>::value ||
    std::is_same<T, tr_torrent_activity>::value ||
    std::is_same<T, tr_tracker_state>::value ||
    std::is_same<T, uint64_t>::value,
    int>::type = 0>
bool change(T& setme, tr_variant* v)
{
    int64_t i;
    return tr_variantGetInt(v, &i) && change(setme, T(i));
}

bool change(TrackerStat& setme, tr_variant* v)
{
    bool changed = false;

    size_t pos = 0;
    tr_quark key;
    tr_variant* value;
    while (tr_variantDictChild(v, pos++, &key, &value))
    {
        switch (key)
        {
#define CASE(type, name) case TR_KEY_ ## name: \
    changed |= change(setme.name, value); break;

            FOREACH_TRACKER_RPC_FIELD(CASE)
#undef CASE
        case TR_KEY_downloadCount:
        case TR_KEY_host:
            // not used
            break;

        default:
            std::cerr << __FILE__ << ':' << __LINE__ << "unhandled type: " << tr_quark_get_string(key, nullptr) << std::endl;
            assert(false && "unhandled type");
        }
    }

    return changed;
}

bool change(TorrentFile& setme, tr_variant* v)
{
    bool changed = false;

    size_t pos = 0;
    tr_quark key;
    tr_variant* value;
    while (tr_variantDictChild(v, pos++, &key, &value))
    {
        switch (key)
        {
#define CASE(type, key, field) case key: \
    changed |= change(setme.field, value); break;

            FOREACH_FILE_RPC_FIELD(CASE)
#undef CASE
        default:
            std::cerr << __FILE__ << ':' << __LINE__ << "unhandled type: " << tr_quark_get_string(key, nullptr) << std::endl;
            assert(false && "unhandled type");
        }
    }

    return changed;
}

bool change(Peer& setme, tr_variant* v)
{
    bool changed = false;

    size_t pos = 0;
    tr_quark key;
    tr_variant* value;
    while (tr_variantDictChild(v, pos++, &key, &value))
    {
        switch (key)
        {
#define CASE(type, name) case TR_KEY_ ## name: \
    changed |= change(setme.name, value); break;

            FOREACH_PEER_RPC_FIELD(CASE)
#undef CASE
        case TR_KEY_clientIsChoked:
        case TR_KEY_clientIsInterested:
        case TR_KEY_isDownloadingFrom:
        case TR_KEY_isIncoming:
        case TR_KEY_isUTP:
        case TR_KEY_isUploadingTo:
        case TR_KEY_peerIsChoked:
        case TR_KEY_peerIsInterested:
        case TR_KEY_port:
            break;

        default:
            std::cerr << __FILE__ << ':' << __LINE__ << "unhandled type: " << tr_quark_get_string(key, nullptr) << std::endl;
            assert(false && "unhandled type");
        }
    }

    return changed;
}

template<typename T>
bool change(QVector<T>& setme, tr_variant* v)
{
    bool changed = false;

    int const n = tr_variantListSize(v);
    if (setme.size() != n)
    {
        setme.resize(n);
        changed = true;
    }

    for (int i = 0; i < n; ++i)
    {
        changed |= change(setme[i], tr_variantListChild(v, i));
    }

    return changed;
}

} // namespace

Torrent::fields_t Torrent::update(tr_quark const* keys, tr_variant** values, size_t n)
{
    // static bool lookup_initialized = false;
    // static int key_to_property_index[TR_N_KEYS];
    fields_t changed = {};

    for (size_t i = 0; i < n; ++i)
    {
        auto const& key = keys[i];
        auto const& value = values[i];

        switch (key)
        {
#define CASE(type, rpc, name) case TR_KEY_ ## rpc: \
    if (change(name ## _, value)) changed |= TF_ ## name; break;

            FOREACH_TORRENT_RPC_FIELD(CASE)
#undef CASE
#define CASE(key, name) case TR_KEY_ ## key: \
    if (change(name ## _, value)) changed |= TF_ ## name; break;

            CASE(fileStats, files)
            CASE(trackerStats, trackers)
#undef CASE
        default:
            std::cerr << __FILE__ << ':' << __LINE__ << "unhandled type: " << tr_quark_get_string(key, nullptr) << std::endl;
            assert(false && "unhandled type");
        }
    }

    // derived fields
    if (changed)
    {
        if (changed & TF_name)
        {
            updateMimeIcon();
        }

        if (changed & TF_trackers)
        {
            auto& cache = qApp->faviconCache();
            for (auto& tracker : trackers_)
            {
                auto const url = QUrl(tracker.announce);
                auto const key = cache.add(url);
                tracker.displayName = FaviconCache::getDisplayName(key);
            }
        }

        if (changed & TF_files)
        {
            for (auto n = files_.size(), i = 0; i < n; ++i)
            {
                files_[i].index = i;
            }
        }
    }

    return changed;
}

QString Torrent::activityString() const
{
    QString str;

    switch (activity())
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
    QString s = errorString_;

    switch (error_)
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
