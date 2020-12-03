/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <array>
#include <bitset>
#include <ctime> // time_t

#include <QIcon>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <libtransmission/transmission.h>
#include <libtransmission/crypto-utils.h>
#include <libtransmission/quark.h>

#include "FaviconCache.h"
#include "IconCache.h"
#include "Macros.h"
#include "Speed.h"

#ifdef ERROR
#undef ERROR
#endif

class QPixmap;

class Prefs;

extern "C"
{
struct tr_variant;
}

struct Peer
{
    bool client_is_choked;
    bool client_is_interested;
    bool is_downloading_from;
    bool is_encrypted;
    bool is_incoming;
    bool is_uploading_to;
    bool peer_is_choked;
    bool peer_is_interested;
    QString address;
    QString client_name;
    QString flags;
    int port;
    Speed rate_to_client;
    Speed rate_to_peer;
    double progress;
};

using PeerList = QVector<Peer>;

struct TrackerStat
{
    QPixmap getFavicon() const;

    bool has_announced;
    bool has_scraped;
    bool is_backup;
    bool last_announce_succeeded;
    bool last_announce_timed_out;
    bool last_scrape_succeeded;
    bool last_scrape_timed_out;
    int announce_state;
    int download_count;
    int id;
    int last_announce_peer_count;
    int last_announce_start_time;
    int last_announce_time;
    int last_scrape_start_time;
    int last_scrape_time;
    int leecher_count;
    int next_announce_time;
    int next_scrape_time;
    int scrape_state;
    int seeder_count;
    int tier;
    FaviconCache::Key favicon_key;
    QString announce;
    QString last_announce_result;
    QString last_scrape_result;
};

using TrackerStatsList = QVector<TrackerStat>;

struct TorrentFile
{
    bool wanted = true;
    int index = -1;
    int priority = 0;
    QString filename;
    uint64_t size = 0;
    uint64_t have = 0;
};

using FileList = QVector<TorrentFile>;

class TorrentHash
{
private:
    std::array<uint8_t, SHA_DIGEST_LENGTH> data_ = {};

public:
    TorrentHash() {}

    explicit TorrentHash(char const* str)
    {
        tr_hex_to_sha1(data_.data(), str);
    }

    explicit TorrentHash(QString const& str)
    {
        tr_hex_to_sha1(data_.data(), str.toUtf8().constData());
    }

    bool operator ==(TorrentHash const& that) const
    {
        return data_ == that.data_;
    }

    bool operator !=(TorrentHash const& that) const
    {
        return data_ != that.data_;
    }

    bool operator <(TorrentHash const& that) const
    {
        return data_ < that.data_;
    }

    QString toString() const
    {
        char str[SHA_DIGEST_LENGTH * 2 + 1];
        tr_sha1_to_hex(str, data_.data());
        return QString::fromUtf8(str, SHA_DIGEST_LENGTH * 2);
    }
};

class Torrent : public QObject
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(Torrent)

public:
    Torrent(Prefs const&, int id);

    int getBandwidthPriority() const
    {
        return bandwidth_priority_;
    }

    int id() const
    {
        return id_;
    }

    QString const& name() const
    {
        return name_;
    }

    bool hasName() const
    {
        return !name_.isEmpty();
    }

    QString const& creator() const
    {
        return creator_;
    }

    QString const& comment() const
    {
        return comment_;
    }

    QString const& getPath() const
    {
        return download_dir_;
    }

    QString getError() const;

    TorrentHash const& hash() const
    {
        return hash_;
    }

    bool hasError() const
    {
        return error_ != TR_STAT_OK;
    }

    bool isDone() const
    {
        return leftUntilDone() == 0;
    }

    bool isSeed() const
    {
        return haveVerified() >= totalSize();
    }

    bool isPrivate() const
    {
        return is_private_;
    }

    bool getSeedRatio(double& setmeRatio) const;

    uint64_t haveVerified() const
    {
        return have_verified_;
    }

    uint64_t haveUnverified() const
    {
        return have_unchecked_;
    }

    uint64_t desiredAvailable() const
    {
        return desired_available_;
    }

    uint64_t haveTotal() const
    {
        return haveVerified() + haveUnverified();
    }

    uint64_t totalSize() const
    {
        return total_size_;
    }

    uint64_t sizeWhenDone() const
    {
        return size_when_done_;
    }

    uint64_t leftUntilDone() const
    {
        return left_until_done_;
    }

    uint64_t pieceSize() const
    {
        return piece_size_;
    }

    bool hasMetadata() const
    {
        return metadataPercentDone() >= 1.0;
    }

    int pieceCount() const
    {
        return piece_count_;
    }

    double ratio() const
    {
        auto const u = uploadedEver();
        auto const d = downloadedEver();
        auto const t = totalSize();
        return double(u) / (d ? d : t);
    }

    double percentComplete() const
    {
        return totalSize() != 0 ? haveTotal() / static_cast<double>(totalSize()) : 0;
    }

    double percentDone() const
    {
        auto const l = leftUntilDone();
        auto const s = sizeWhenDone();
        return s ? static_cast<double>(s - l) / static_cast<double>(s) : 0.0;
    }

    double metadataPercentDone() const
    {
        return metadata_percent_complete_;
    }

    uint64_t downloadedEver() const
    {
        return downloaded_ever_;
    }

    uint64_t uploadedEver() const
    {
        return uploaded_ever_;
    }

    uint64_t failedEver() const
    {
        return failed_ever_;
    }

    int compareSeedRatio(Torrent const&) const;
    int compareRatio(Torrent const&) const;
    int compareETA(Torrent const&) const;

    bool hasETA() const
    {
        return getETA() >= 0;
    }

    int getETA() const
    {
        return eta_;
    }

    time_t lastActivity() const
    {
        return activity_date_;
    }

    time_t lastStarted() const
    {
        return start_date_;
    }

    time_t dateAdded() const
    {
        return added_date_;
    }

    time_t dateCreated() const
    {
        return date_created_;
    }

    time_t dateEdited() const
    {
        return edit_date_;
    }

    time_t manualAnnounceTime() const
    {
        return manual_announce_time_;
    }

    bool canManualAnnounceAt(time_t t) const
    {
        return isReadyToTransfer() && (manualAnnounceTime() <= t);
    }

    int peersWeAreDownloadingFrom() const
    {
        return peers_sending_to_us_;
    }

    int webseedsWeAreDownloadingFrom() const
    {
        return webseeds_sending_to_us_;
    }

    int peersWeAreUploadingTo() const
    {
        return peers_getting_from_us_;
    }

    bool isUploading() const
    {
        return peersWeAreUploadingTo() > 0;
    }

    int connectedPeers() const
    {
        return peers_connected_;
    }

    int connectedPeersAndWebseeds() const
    {
        return connectedPeers() + webseedsWeAreDownloadingFrom();
    }

    Speed const& downloadSpeed() const
    {
        return download_speed_;
    }

    Speed const& uploadSpeed() const
    {
        return upload_speed_;
    }

    double getVerifyProgress() const
    {
        return recheck_progress_;
    }

    bool includesTracker(FaviconCache::Key const& key) const;

    FaviconCache::Keys const& trackerKeys() const
    {
        return tracker_keys_;
    }

    Speed uploadLimit() const
    {
        return Speed::fromKBps(upload_limit_);
    }

    Speed downloadLimit() const
    {
        return Speed::fromKBps(download_limit_);
    }

    bool uploadIsLimited() const
    {
        return upload_limited_;
    }

    bool downloadIsLimited() const
    {
        return download_limited_;
    }

    bool honorsSessionLimits() const
    {
        return honors_session_limits_;
    }

    int peerLimit() const
    {
        return peer_limit_;
    }

    double seedRatioLimit() const
    {
        return seed_ratio_limit_;
    }

    tr_ratiolimit seedRatioMode() const
    {
        return static_cast<tr_ratiolimit>(seed_ratio_mode_);
    }

    int seedIdleLimit() const
    {
        return seed_idle_limit_;
    }

    tr_idlelimit seedIdleMode() const
    {
        return static_cast<tr_idlelimit>(seed_idle_mode_);
    }

    TrackerStatsList const& trackerStats() const
    {
        return tracker_stats_;
    }

    PeerList const& peers() const
    {
        return peers_;
    }

    FileList const& files() const
    {
        return files_;
    }

    int queuePosition() const
    {
        return queue_position_;
    }

    bool isStalled() const
    {
        return is_stalled_;
    }

    QString activityString() const;

    tr_torrent_activity getActivity() const
    {
        return static_cast<tr_torrent_activity>(status_);
    }

    bool isFinished() const
    {
        return is_finished_;
    }

    bool isPaused() const
    {
        return getActivity() == TR_STATUS_STOPPED;
    }

    bool isWaitingToVerify() const
    {
        return getActivity() == TR_STATUS_CHECK_WAIT;
    }

    bool isVerifying() const
    {
        return getActivity() == TR_STATUS_CHECK;
    }

    bool isDownloading() const
    {
        return getActivity() == TR_STATUS_DOWNLOAD;
    }

    bool isWaitingToDownload() const
    {
        return getActivity() == TR_STATUS_DOWNLOAD_WAIT;
    }

    bool isSeeding() const
    {
        return getActivity() == TR_STATUS_SEED;
    }

    bool isWaitingToSeed() const
    {
        return getActivity() == TR_STATUS_SEED_WAIT;
    }

    bool isReadyToTransfer() const
    {
        return getActivity() == TR_STATUS_DOWNLOAD || getActivity() == TR_STATUS_SEED;
    }

    bool isQueued() const
    {
        return isWaitingToDownload() || isWaitingToSeed();
    }

    QIcon getMimeTypeIcon() const;

    enum Field
    {
        ACTIVITY_DATE,
        ADDED_DATE,
        BANDWIDTH_PRIORITY,
        COMMENT,
        CREATOR,
        DATE_CREATED,
        DESIRED_AVAILABLE,
        DOWNLOADED_EVER,
        DOWNLOAD_DIR,
        DOWNLOAD_LIMIT,
        DOWNLOAD_LIMITED,
        DOWNLOAD_SPEED,
        EDIT_DATE,
        ERROR,
        ERROR_STRING,
        ETA,
        FAILED_EVER,
        FILE_COUNT,
        FILES,
        HASH,
        HAVE_UNCHECKED,
        HAVE_VERIFIED,
        HONORS_SESSION_LIMITS,
        ICON,
        IS_FINISHED,
        IS_PRIVATE,
        IS_STALLED,
        LEFT_UNTIL_DONE,
        MANUAL_ANNOUNCE_TIME,
        METADATA_PERCENT_COMPLETE,
        NAME,
        PEERS,
        PEERS_CONNECTED,
        PEERS_GETTING_FROM_US,
        PEERS_SENDING_TO_US,
        PEER_LIMIT,
        PERCENT_DONE,
        PIECE_COUNT,
        PIECE_SIZE,
        PRIMARY_MIME_TYPE,
        QUEUE_POSITION,
        RECHECK_PROGRESS,
        SEED_IDLE_LIMIT,
        SEED_IDLE_MODE,
        SEED_RATIO_LIMIT,
        SEED_RATIO_MODE,
        SIZE_WHEN_DONE,
        START_DATE,
        STATUS,
        TOTAL_SIZE,
        TRACKER_STATS,
        UPLOADED_EVER,
        UPLOAD_LIMIT,
        UPLOAD_LIMITED,
        UPLOAD_SPEED,
        WEBSEEDS_SENDING_TO_US,

        N_FIELDS
    };
    using fields_t = std::bitset<N_FIELDS>;

    fields_t update(tr_quark const* keys, tr_variant const* const* values, size_t n);

private:
    int const id_;

    bool download_limited_ = {};
    bool honors_session_limits_ = {};
    bool is_finished_ = {};
    bool is_private_ = {};
    bool is_stalled_ = {};
    bool upload_limited_ = {};

    time_t activity_date_ = {};
    time_t added_date_ = {};
    time_t date_created_ = {};
    time_t edit_date_ = {};
    time_t manual_announce_time_ = {};
    time_t start_date_ = {};

    int bandwidth_priority_ = {};
    int download_limit_ = {};
    int error_ = {};
    int eta_ = {};
    int peer_limit_ = {};
    int peers_connected_ = {};
    int peers_getting_from_us_ = {};
    int peers_sending_to_us_ = {};
    int piece_count_ = {};
    int queue_position_ = {};
    int seed_idle_limit_ = {};
    int seed_idle_mode_ = {};
    int seed_ratio_mode_ = {};
    int status_ = {};
    int upload_limit_ = {};
    int webseeds_sending_to_us_ = {};

    uint64_t desired_available_ = {};
    uint64_t downloaded_ever_ = {};
    uint64_t failed_ever_ = {};
    uint64_t file_count_ = {};
    uint64_t have_unchecked_ = {};
    uint64_t have_verified_ = {};
    uint64_t left_until_done_ = {};
    uint64_t piece_size_ = {};
    uint64_t size_when_done_ = {};
    uint64_t total_size_ = {};
    uint64_t uploaded_ever_ = {};

    double metadata_percent_complete_ = {};
    double percent_done_ = {};
    double recheck_progress_ = {};
    double seed_ratio_limit_ = {};

    QString primary_mime_type_;
    QString comment_;
    QString creator_;
    QString download_dir_;
    QString error_string_;
    QString name_;

    // mutable because it's a lazy lookup
    mutable QIcon icon_ = IconCache::get().fileIcon();

    PeerList peers_;
    FileList files_;

    FaviconCache::Keys tracker_keys_;
    TrackerStatsList tracker_stats_;

    Speed upload_speed_;
    Speed download_speed_;

    Prefs const& prefs_;

    TorrentHash hash_;
};

Q_DECLARE_METATYPE(Torrent const*)
