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

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_new0, tr_strdup */
#include <libtransmission/variant.h>

#include "Application.h"
#include "Prefs.h"
#include "Torrent.h"
#include "Utils.h"

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
    id_(id),
    icon_(Utils::getFileIcon()),
    prefs_(prefs)
{
}

/***
****
***/

namespace
{

template<typename T>
bool change(T& setme, T const& value)
{
    bool const changed = setme != value;

    if (changed)
    {
        setme = value;
    }

    return changed;
}

bool change(double& setme, double const& value)
{
    bool const changed = !qFuzzyCompare(setme + 1, value + 1);

    if (changed)
    {
        setme = value;
    }

    return changed;
}

bool change(double& setme, tr_variant const* value)
{
    double d;
    return tr_variantGetReal(value, &d) && change(setme, d);
}

bool change(int& setme, tr_variant const* value)
{
    int64_t i;
    return tr_variantGetInt(value, &i) && change(setme, static_cast<int>(i));
}

bool change(uint64_t& setme, tr_variant const* value)
{
    int64_t i;
    return tr_variantGetInt(value, &i) && change(setme, static_cast<uint64_t>(i));
}

bool change(time_t& setme, tr_variant const* value)
{
    int64_t i;
    return tr_variantGetInt(value, &i) && change(setme, static_cast<time_t>(i));
}

bool change(Speed& setme, tr_variant const* value)
{
    int64_t i;
    return tr_variantGetInt(value, &i) && change(setme, Speed::fromBps(i));
}

bool change(bool& setme, tr_variant const* value)
{
    bool b;
    return tr_variantGetBool(value, &b) && change(setme, b);
}

bool change(QString& setme, tr_variant const* value)
{
    bool changed = false;
    char const* str;
    size_t len;

    if (!tr_variantGetStr(value, &str, &len))
    {
        return changed;
    }

    if (len == 0)
    {
        changed = !setme.isEmpty();
        setme.clear();
        return changed;
    }

    return change(setme, QString::fromUtf8(str, len));
}

bool change(Peer& setme, tr_variant const* value)
{
    bool changed = false;

    size_t pos = 0;
    tr_quark key;
    tr_variant* child;
    while (tr_variantDictChild(const_cast<tr_variant*>(value), pos++, &key, &child))
    {
        switch (key)
        {
#define HANDLE_KEY(key) case TR_KEY_ ## key: \
    changed = change(setme.key, child) || changed; break;

            HANDLE_KEY(address)
            HANDLE_KEY(clientIsChoked)
            HANDLE_KEY(clientIsInterested)
            HANDLE_KEY(clientName)
            HANDLE_KEY(flagStr)
            HANDLE_KEY(isDownloadingFrom)
            HANDLE_KEY(isEncrypted)
            HANDLE_KEY(isIncoming)
            HANDLE_KEY(isUploadingTo)
            HANDLE_KEY(peerIsChoked)
            HANDLE_KEY(peerIsInterested)
            HANDLE_KEY(port)
            HANDLE_KEY(progress)
            HANDLE_KEY(rateToClient)
            HANDLE_KEY(rateToPeer)
#undef HANDLE_KEY
        default:
            break;
        }
    }

    return changed;
}

bool change(TorrentFile& setme, tr_variant const* value)
{
    bool changed = false;

    size_t pos = 0;
    tr_quark key;
    tr_variant* child;
    while (tr_variantDictChild(const_cast<tr_variant*>(value), pos++, &key, &child))
    {
        switch (key)
        {
#define HANDLE_KEY(key) case TR_KEY_ ## key: \
    changed = change(setme.key, child) || changed; break;

            HANDLE_KEY(have)
            HANDLE_KEY(priority)
            HANDLE_KEY(wanted)
#undef HANDLE_KEY
#define HANDLE_KEY(key, field) case TR_KEY_ ## key: \
    changed = change(setme.field, child) || changed; break;

            HANDLE_KEY(bytesCompleted, have)
            HANDLE_KEY(length, size)
            HANDLE_KEY(name, filename)
#undef HANDLE_KEY
        default:
            break;
        }
    }

    return changed;
}

bool change(TrackerStat& setme, tr_variant const* value)
{
    bool changed = false;

    size_t pos = 0;
    tr_quark key;
    tr_variant* child;
    while (tr_variantDictChild(const_cast<tr_variant*>(value), pos++, &key, &child))
    {
        switch (key)
        {
#define HANDLE_KEY(key) case TR_KEY_ ## key: \
    changed = change(setme.key, child) || changed; break;
            HANDLE_KEY(announce);
            HANDLE_KEY(announceState);
            HANDLE_KEY(downloadCount);
            HANDLE_KEY(hasAnnounced);
            HANDLE_KEY(hasScraped);
            HANDLE_KEY(host);
            HANDLE_KEY(id);
            HANDLE_KEY(isBackup);
            HANDLE_KEY(lastAnnouncePeerCount);
            HANDLE_KEY(lastAnnounceResult);
            HANDLE_KEY(lastAnnounceStartTime);
            HANDLE_KEY(lastAnnounceSucceeded);
            HANDLE_KEY(lastAnnounceTime);
            HANDLE_KEY(lastAnnounceTimedOut);
            HANDLE_KEY(lastScrapeResult);
            HANDLE_KEY(lastScrapeStartTime);
            HANDLE_KEY(lastScrapeSucceeded);
            HANDLE_KEY(lastScrapeTime);
            HANDLE_KEY(lastScrapeTimedOut);
            HANDLE_KEY(leecherCount);
            HANDLE_KEY(nextAnnounceTime);
            HANDLE_KEY(nextScrapeTime);
            HANDLE_KEY(scrapeState);
            HANDLE_KEY(seederCount);
            HANDLE_KEY(tier);

#undef HANDLE_KEY
        default:
            break;
        }
    }

    return changed;
}

template<typename T>
bool change(QVector<T>& setme, tr_variant const* value)
{
    bool changed = false;

    int const n = tr_variantListSize(value);
    if (setme.size() != n)
    {
        setme.resize(n);
        changed = true;
    }

    for (int i = 0; i < n; ++i)
    {
        changed = change(setme[i], tr_variantListChild(const_cast<tr_variant*>(value), i)) || changed;
    }

    return changed;
}

} // anonymous namespace

/***
****
***/

bool Torrent::getSeedRatio(double& setmeRatio) const
{
    bool isLimited;

    switch (seedRatioMode())
    {
    case TR_RATIOLIMIT_SINGLE:
        isLimited = true;
        setmeRatio = seedRatioLimit();
        break;

    case TR_RATIOLIMIT_GLOBAL:
        if ((isLimited = prefs_.getBool(Prefs::RATIO_ENABLED)))
        {
            setmeRatio = prefs_.getDouble(Prefs::RATIO);
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

bool Torrent::update(tr_quark const* keys, tr_variant const* const* values, size_t n)
{
    bool changed = false;

    for (size_t pos = 0; pos < n; ++pos)
    {
        tr_quark key = keys[pos];
        tr_variant const* child = values[pos];
        bool field_changed = false;

        switch (key)
        {
#define HANDLE_KEY(key) case TR_KEY_ ## key: \
    field_changed = change(key ## _, child); break;

            HANDLE_KEY(activityDate)
            HANDLE_KEY(addedDate)
            HANDLE_KEY(bandwidthPriority)
            HANDLE_KEY(comment)
            HANDLE_KEY(creator)
            HANDLE_KEY(dateCreated)
            HANDLE_KEY(desiredAvailable)
            HANDLE_KEY(downloadDir)
            HANDLE_KEY(downloadLimit) // KB/s
            HANDLE_KEY(downloadLimited)
            HANDLE_KEY(downloadedEver)
            HANDLE_KEY(editDate)
            HANDLE_KEY(error)
            HANDLE_KEY(errorString)
            HANDLE_KEY(eta)
            HANDLE_KEY(files)
            HANDLE_KEY(hashString)
            HANDLE_KEY(haveUnchecked)
            HANDLE_KEY(honorsSessionLimits)
            HANDLE_KEY(isFinished)
            HANDLE_KEY(isPrivate)
            HANDLE_KEY(isStalled)
            HANDLE_KEY(leftUntilDone)
            HANDLE_KEY(manualAnnounceTime)
            HANDLE_KEY(metadataPercentComplete)
            HANDLE_KEY(name)
            HANDLE_KEY(peers)
            HANDLE_KEY(peersConnected)
            HANDLE_KEY(peersGettingFromUs)
            HANDLE_KEY(peersSendingToUs)
            HANDLE_KEY(percentDone)
            HANDLE_KEY(pieceCount)
            HANDLE_KEY(pieceSize)
            HANDLE_KEY(queuePosition)
            HANDLE_KEY(recheckProgress)
            HANDLE_KEY(seedIdleLimit)
            HANDLE_KEY(seedIdleMode)
            HANDLE_KEY(seedRatioLimit)
            HANDLE_KEY(seedRatioMode)
            HANDLE_KEY(sizeWhenDone)
            HANDLE_KEY(startDate)
            HANDLE_KEY(status)
            HANDLE_KEY(totalSize)
            HANDLE_KEY(trackerStats)
            HANDLE_KEY(uploadLimit) // KB/s
            HANDLE_KEY(uploadLimited)
            HANDLE_KEY(uploadedEver)
            HANDLE_KEY(webseedsSendingToUs)
#undef HANDLE_KEY
#define HANDLE_KEY(key, field) case TR_KEY_ ## key: \
    field_changed = change(field ## _, child); break;

            HANDLE_KEY(corruptEver, failedEver)
            HANDLE_KEY(fileStats, files)
            HANDLE_KEY(haveValid, haveVerified)
            HANDLE_KEY(peer_limit, peerLimit)
            HANDLE_KEY(rateDownload, downloadSpeed)
            HANDLE_KEY(rateUpload, uploadSpeed)
            HANDLE_KEY(trackers, trackerStats)
#undef HANDLE_KEY
        default:
            break;
        }

        changed = changed || field_changed;

        if (field_changed)
        {
            switch (key)
            {
            case TR_KEY_editDate:
                // FIXME
                break;

            case TR_KEY_name:
                {
                    updateMimeIcon();
                    break;
                }

            case TR_KEY_files:
                {
                    updateMimeIcon();
                    for (int i = 0; i < files_.size(); ++i)
                    {
                        files_[i].index = i;
                    }

                    break;
                }

            case TR_KEY_trackers:
                {
                    // rebuild trackers_
                    QStringList urls;
                    urls.reserve(trackerStats_.size());
                    for (auto const& t : trackerStats_)
                    {
                        urls.append(t.announce);
                    }

                    trackers_.swap(urls);

                    // rebuild trackerDisplayNames
                    QStringList displayNames;
                    displayNames.reserve(trackers_.size());
                    for (auto const& tracker : trackers_)
                    {
                        auto const url = QUrl(tracker);
                        auto const key = qApp->faviconCache().add(url);
                        displayNames.append(FaviconCache::getDisplayName(key));
                    }

                    displayNames.removeDuplicates();
                    trackerDisplayNames_.swap(displayNames);
                    break;
                }
            }
        }
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
    auto s = errorString_;

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
