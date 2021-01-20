/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cassert>
#include <iterator>
#include <set>

#include <QApplication>
#include <QString>
#include <QUrl>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_new0, tr_strdup */
#include <libtransmission/variant.h>

#include "Application.h"
#include "IconCache.h"
#include "Prefs.h"
#include "Torrent.h"
#include "Utils.h"
#include "VariantHelpers.h"

using ::trqt::variant_helpers::change;

Torrent::Torrent(Prefs const& prefs, int id) :
    id_(id),
    prefs_(prefs)
{
}

/***
****
***/

bool Torrent::getSeedRatio(double& setmeRatio) const
{
    bool is_limited;

    switch (seedRatioMode())
    {
    case TR_RATIOLIMIT_SINGLE:
        is_limited = true;
        setmeRatio = seedRatioLimit();
        break;

    case TR_RATIOLIMIT_GLOBAL:
        if ((is_limited = prefs_.getBool(Prefs::RATIO_ENABLED)))
        {
            setmeRatio = prefs_.getDouble(Prefs::RATIO);
        }

        break;

    default: // TR_RATIOLIMIT_UNLIMITED:
        is_limited = false;
        break;
    }

    return is_limited;
}

bool Torrent::includesTracker(FaviconCache::Key const& key) const
{
    return std::binary_search(std::begin(tracker_keys_), std::end(tracker_keys_), key);
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

QIcon Torrent::getMimeTypeIcon() const
{
    if (icon_.isNull())
    {
        icon_ = IconCache::get().getMimeTypeIcon(primary_mime_type_, file_count_ > 1);
    }

    return icon_;
}

/***
****
***/

Torrent::fields_t Torrent::update(tr_quark const* keys, tr_variant const* const* values, size_t n)
{
    auto changed = fields_t{};

    for (size_t pos = 0; pos < n; ++pos)
    {
        tr_quark key = keys[pos];
        tr_variant const* child = values[pos];
        bool field_changed = false;

        switch (key)
        {
#define HANDLE_KEY(key, field, bit) case TR_KEY_ ## key: \
    field_changed = change(field ## _, child); \
    changed.set(bit, field_changed); \
    break;

            HANDLE_KEY(activityDate, activity_date, ACTIVITY_DATE)
            HANDLE_KEY(addedDate, added_date, ADDED_DATE)
            HANDLE_KEY(bandwidthPriority, bandwidth_priority, BANDWIDTH_PRIORITY)
            HANDLE_KEY(corruptEver, failed_ever, FAILED_EVER)
            HANDLE_KEY(dateCreated, date_created, DATE_CREATED)
            HANDLE_KEY(desiredAvailable, desired_available, DESIRED_AVAILABLE)
            HANDLE_KEY(downloadLimit, download_limit, DOWNLOAD_LIMIT) // KB/s
            HANDLE_KEY(downloadLimited, download_limited, DOWNLOAD_LIMITED)
            HANDLE_KEY(downloadedEver, downloaded_ever, DOWNLOADED_EVER)
            HANDLE_KEY(editDate, edit_date, EDIT_DATE)
            HANDLE_KEY(error, error, ERROR)
            HANDLE_KEY(eta, eta, ETA)
            HANDLE_KEY(fileStats, files, FILES)
            HANDLE_KEY(files, files, FILES)
            HANDLE_KEY(file_count, file_count, FILE_COUNT)
            HANDLE_KEY(hashString, hash, HASH)
            HANDLE_KEY(haveUnchecked, have_unchecked, HAVE_UNCHECKED)
            HANDLE_KEY(haveValid, have_verified, HAVE_VERIFIED)
            HANDLE_KEY(honorsSessionLimits, honors_session_limits, HONORS_SESSION_LIMITS)
            HANDLE_KEY(isFinished, is_finished, IS_FINISHED)
            HANDLE_KEY(isPrivate, is_private, IS_PRIVATE)
            HANDLE_KEY(isStalled, is_stalled, IS_STALLED)
            HANDLE_KEY(leftUntilDone, left_until_done, LEFT_UNTIL_DONE)
            HANDLE_KEY(manualAnnounceTime, manual_announce_time, MANUAL_ANNOUNCE_TIME)
            HANDLE_KEY(metadataPercentComplete, metadata_percent_complete, METADATA_PERCENT_COMPLETE)
            HANDLE_KEY(name, name, NAME)
            HANDLE_KEY(peer_limit, peer_limit, PEER_LIMIT)
            HANDLE_KEY(peers, peers, PEERS)
            HANDLE_KEY(peersConnected, peers_connected, PEERS_CONNECTED)
            HANDLE_KEY(peersGettingFromUs, peers_getting_from_us, PEERS_GETTING_FROM_US)
            HANDLE_KEY(peersSendingToUs, peers_sending_to_us, PEERS_SENDING_TO_US)
            HANDLE_KEY(percentDone, percent_done, PERCENT_DONE)
            HANDLE_KEY(pieceCount, piece_count, PIECE_COUNT)
            HANDLE_KEY(pieceSize, piece_size, PIECE_SIZE)
            HANDLE_KEY(primary_mime_type, primary_mime_type, PRIMARY_MIME_TYPE)
            HANDLE_KEY(queuePosition, queue_position, QUEUE_POSITION)
            HANDLE_KEY(rateDownload, download_speed, DOWNLOAD_SPEED)
            HANDLE_KEY(rateUpload, upload_speed, UPLOAD_SPEED)
            HANDLE_KEY(recheckProgress, recheck_progress, RECHECK_PROGRESS)
            HANDLE_KEY(seedIdleLimit, seed_idle_limit, SEED_IDLE_LIMIT)
            HANDLE_KEY(seedIdleMode, seed_idle_mode, SEED_IDLE_MODE)
            HANDLE_KEY(seedRatioLimit, seed_ratio_limit, SEED_RATIO_LIMIT)
            HANDLE_KEY(seedRatioMode, seed_ratio_mode, SEED_RATIO_MODE)
            HANDLE_KEY(sizeWhenDone, size_when_done, SIZE_WHEN_DONE)
            HANDLE_KEY(startDate, start_date, START_DATE)
            HANDLE_KEY(status, status, STATUS)
            HANDLE_KEY(totalSize, total_size, TOTAL_SIZE)
            HANDLE_KEY(trackerStats, tracker_stats, TRACKER_STATS)
            HANDLE_KEY(trackers, tracker_stats, TRACKER_STATS)
            HANDLE_KEY(uploadLimit, upload_limit, UPLOAD_LIMIT) // KB/s
            HANDLE_KEY(uploadLimited, upload_limited, UPLOAD_LIMITED)
            HANDLE_KEY(uploadedEver, uploaded_ever, UPLOADED_EVER)
            HANDLE_KEY(webseedsSendingToUs, webseeds_sending_to_us, WEBSEEDS_SENDING_TO_US)
#undef HANDLE_KEY

#define HANDLE_KEY(key, field, bit) case TR_KEY_ ## key: \
    field_changed = change(field ## _, child); \
    if (field_changed) \
    { \
        field ## _ = trApp->intern(field ## _); \
    } \
    changed.set(bit, field_changed); \
    break;

            HANDLE_KEY(comment, comment, COMMENT)
            HANDLE_KEY(creator, creator, CREATOR)
            HANDLE_KEY(downloadDir, download_dir, DOWNLOAD_DIR)
            HANDLE_KEY(errorString, error_string, ERROR_STRING)

#undef HANDLE_KEY
        default:
            break;
        }

        if (field_changed)
        {
            switch (key)
            {
            case TR_KEY_file_count:
            case TR_KEY_primary_mime_type:
                {
                    icon_ = {};
                    break;
                }

            case TR_KEY_files:
                {
                    for (int i = 0; i < files_.size(); ++i)
                    {
                        files_[i].index = i;
                    }

                    break;
                }

            case TR_KEY_trackers:
                {
                    std::set<FaviconCache::Key> tmp;
                    for (auto const& ts : tracker_stats_)
                    {
                        tmp.insert(ts.favicon_key);
                    }

                    tracker_keys_ = FaviconCache::Keys(std::begin(tmp), std::end(tmp));
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
    return trApp->faviconCache().find(favicon_key);
}
