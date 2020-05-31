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
#define HANDLE_KEY(key, field) case TR_KEY_ ## key: \
    changed = change(setme.field, child) || changed; break;

            HANDLE_KEY(address, address)
            HANDLE_KEY(clientIsChoked, client_is_choked)
            HANDLE_KEY(clientIsInterested, client_is_interested)
            HANDLE_KEY(clientName, client_name)
            HANDLE_KEY(flagStr, flags)
            HANDLE_KEY(isDownloadingFrom, is_downloading_from)
            HANDLE_KEY(isEncrypted, is_encrypted)
            HANDLE_KEY(isIncoming, is_incoming)
            HANDLE_KEY(isUploadingTo, is_uploading_to)
            HANDLE_KEY(peerIsChoked, peer_is_choked)
            HANDLE_KEY(peerIsInterested, peer_is_interested)
            HANDLE_KEY(port, port)
            HANDLE_KEY(progress, progress)
            HANDLE_KEY(rateToClient, rate_to_client)
            HANDLE_KEY(rateToPeer, rate_to_peer)
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
#define HANDLE_KEY(key, field) case TR_KEY_ ## key: \
    changed = change(setme.field, child) || changed; break;
            HANDLE_KEY(announce, announce)
            HANDLE_KEY(announceState, announce_state)
            HANDLE_KEY(downloadCount, download_count)
            HANDLE_KEY(hasAnnounced, has_announced)
            HANDLE_KEY(hasScraped, has_scraped)
            HANDLE_KEY(host, host);
            HANDLE_KEY(id, id);
            HANDLE_KEY(isBackup, is_backup);
            HANDLE_KEY(lastAnnouncePeerCount, last_announce_peer_count);
            HANDLE_KEY(lastAnnounceResult, last_announce_result);
            HANDLE_KEY(lastAnnounceStartTime, last_announce_start_time)
            HANDLE_KEY(lastAnnounceSucceeded, last_announce_succeeded)
            HANDLE_KEY(lastAnnounceTime, last_announce_time)
            HANDLE_KEY(lastAnnounceTimedOut, last_announce_timed_out)
            HANDLE_KEY(lastScrapeResult, last_scrape_result)
            HANDLE_KEY(lastScrapeStartTime, last_scrape_start_time)
            HANDLE_KEY(lastScrapeSucceeded, last_scrape_succeeded)
            HANDLE_KEY(lastScrapeTime, last_scrape_time)
            HANDLE_KEY(lastScrapeTimedOut, last_scrape_timed_out)
            HANDLE_KEY(leecherCount, leecher_count)
            HANDLE_KEY(nextAnnounceTime, next_announce_time)
            HANDLE_KEY(nextScrapeTime, next_scrape_time)
            HANDLE_KEY(scrapeState, scrape_state)
            HANDLE_KEY(seederCount, seeder_count)
            HANDLE_KEY(tier, tier)

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
    bool const have_a(hasETA());
    bool const have_b(that.hasETA());

    if (have_a && have_b)
    {
        return getETA() - that.getETA();
    }

    if (have_a)
    {
        return 1;
    }

    if (have_b)
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
#define HANDLE_KEY(key, field) case TR_KEY_ ## key: \
    field_changed = change(field ## _, child); break;

            HANDLE_KEY(activityDate, activity_date)
            HANDLE_KEY(addedDate, added_date)
            HANDLE_KEY(bandwidthPriority, bandwidth_priority)
            HANDLE_KEY(comment, comment)
            HANDLE_KEY(corruptEver, failed_ever)
            HANDLE_KEY(creator, creator)
            HANDLE_KEY(dateCreated, date_created)
            HANDLE_KEY(desiredAvailable, desired_available)
            HANDLE_KEY(downloadDir, download_dir)
            HANDLE_KEY(downloadLimit, download_limit) // KB/s
            HANDLE_KEY(downloadLimited, download_limited)
            HANDLE_KEY(downloadedEver, downloaded_ever)
            HANDLE_KEY(editDate, edit_date)
            HANDLE_KEY(error, error)
            HANDLE_KEY(errorString, error_string)
            HANDLE_KEY(eta, eta)
            HANDLE_KEY(fileStats, files)
            HANDLE_KEY(files, files)
            HANDLE_KEY(hashString, hash_string)
            HANDLE_KEY(haveUnchecked, have_unchecked)
            HANDLE_KEY(haveValid, have_verified)
            HANDLE_KEY(honorsSessionLimits, honors_session_limits)
            HANDLE_KEY(isFinished, is_finished)
            HANDLE_KEY(isPrivate, is_private)
            HANDLE_KEY(isStalled, is_stalled)
            HANDLE_KEY(leftUntilDone, left_until_done)
            HANDLE_KEY(manualAnnounceTime, manual_announce_time)
            HANDLE_KEY(metadataPercentComplete, metadata_percent_complete)
            HANDLE_KEY(name, name)
            HANDLE_KEY(peer_limit, peer_limit)
            HANDLE_KEY(peers, peers)
            HANDLE_KEY(peersConnected, peers_connected)
            HANDLE_KEY(peersGettingFromUs, peers_getting_from_us)
            HANDLE_KEY(peersSendingToUs, peers_sending_to_us)
            HANDLE_KEY(percentDone, percent_done)
            HANDLE_KEY(pieceCount, piece_count)
            HANDLE_KEY(pieceSize, piece_size)
            HANDLE_KEY(queuePosition, queue_position)
            HANDLE_KEY(rateDownload, download_speed)
            HANDLE_KEY(rateUpload, upload_speed)
            HANDLE_KEY(recheckProgress, recheck_progress)
            HANDLE_KEY(seedIdleLimit, seed_idle_limit)
            HANDLE_KEY(seedIdleMode, seed_idle_mode)
            HANDLE_KEY(seedRatioLimit, seed_ratio_limit)
            HANDLE_KEY(seedRatioMode, seed_ratio_mode)
            HANDLE_KEY(sizeWhenDone, size_when_done)
            HANDLE_KEY(startDate, start_date)
            HANDLE_KEY(status, status)
            HANDLE_KEY(totalSize, total_size)
            HANDLE_KEY(trackerStats, tracker_stats)
            HANDLE_KEY(trackers, tracker_stats)
            HANDLE_KEY(uploadLimit, upload_limit) // KB/s
            HANDLE_KEY(uploadLimited, upload_limited)
            HANDLE_KEY(uploadedEver, uploaded_ever)
            HANDLE_KEY(webseedsSendingToUs, webseeds_sending_to_us)
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
                    urls.reserve(tracker_stats_.size());
                    for (auto const& t : tracker_stats_)
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
                    tracker_display_names_.swap(displayNames);
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
    auto s = error_string_;

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
