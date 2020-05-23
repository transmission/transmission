/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <ctime> // time_t

#include <QIcon>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

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
    bool clientIsChoked;
    bool clientIsInterested;
    bool isDownloadingFrom;
    bool isEncrypted;
    bool isIncoming;
    bool isUploadingTo;
    bool peerIsChoked;
    bool peerIsInterested;
    QString address;
    QString clientName;
    QString flagStr;
    int port;
    Speed rateToClient;
    Speed rateToPeer;
    double progress;
};

using PeerList = QVector<Peer>;

struct TrackerStat
{
    QPixmap getFavicon() const;

    bool hasAnnounced;
    bool hasScraped;
    bool isBackup;
    bool lastAnnounceSucceeded;
    bool lastAnnounceTimedOut;
    bool lastScrapeSucceeded;
    bool lastScrapeTimedOut;
    int announceState;
    int downloadCount;
    int id;
    int lastAnnouncePeerCount;
    int lastAnnounceStartTime;
    int lastAnnounceTime;
    int lastScrapeStartTime;
    int lastScrapeTime;
    int leecherCount;
    int nextAnnounceTime;
    int nextScrapeTime;
    int scrapeState;
    int seederCount;
    int tier;
    QString announce;
    QString host;
    QString lastAnnounceResult;
    QString lastScrapeResult;
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

class Torrent : public QObject
{
    Q_OBJECT

public:
    Torrent(Prefs const&, int id);

    int getBandwidthPriority() const
    {
        return bandwidthPriority_;
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
        return downloadDir_;
    }

    QString getError() const;

    QString const& hashString() const
    {
        return hashString_;
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
        return isPrivate_;
    }

    bool getSeedRatio(double& setmeRatio) const;

    uint64_t haveVerified() const
    {
        return haveVerified_;
    }

    uint64_t haveUnverified() const
    {
        return haveUnchecked_;
    }

    uint64_t desiredAvailable() const
    {
        return desiredAvailable_;
    }

    uint64_t haveTotal() const
    {
        return haveVerified() + haveUnverified();
    }

    uint64_t totalSize() const
    {
        return totalSize_;
    }

    uint64_t sizeWhenDone() const
    {
        return sizeWhenDone_;
    }

    uint64_t leftUntilDone() const
    {
        return leftUntilDone_;
    }

    uint64_t pieceSize() const
    {
        return pieceSize_;
    }

    bool hasMetadata() const
    {
        return metadataPercentDone() >= 1.0;
    }

    int pieceCount() const
    {
        return pieceCount_;
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
        return s ? double(s - l) / s : 0.0;
    }

    double metadataPercentDone() const
    {
        return metadataPercentComplete_;
    }

    uint64_t downloadedEver() const
    {
        return downloadedEver_;
    }

    uint64_t uploadedEver() const
    {
        return uploadedEver_;
    }

    uint64_t failedEver() const
    {
        return failedEver_;
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
        return activityDate_;
    }

    time_t lastStarted() const
    {
        return startDate_;
    }

    time_t dateAdded() const
    {
        return addedDate_;
    }

    time_t dateCreated() const
    {
        return dateCreated_;
    }

    time_t manualAnnounceTime() const
    {
        return manualAnnounceTime_;
    }

    bool canManualAnnounceAt(time_t t) const
    {
        return isReadyToTransfer() && (manualAnnounceTime() <= t);
    }

    int peersWeAreDownloadingFrom() const
    {
        return peersSendingToUs_;
    }

    int webseedsWeAreDownloadingFrom() const
    {
        return webseedsSendingToUs_;
    }

    int peersWeAreUploadingTo() const
    {
        return peersGettingFromUs_;
    }

    bool isUploading() const
    {
        return peersWeAreUploadingTo() > 0;
    }

    int connectedPeers() const
    {
        return peersConnected_;
    }

    int connectedPeersAndWebseeds() const
    {
        return connectedPeers() + webseedsWeAreDownloadingFrom();
    }

    Speed const& downloadSpeed() const
    {
        return downloadSpeed_;
    }

    Speed const& uploadSpeed() const
    {
        return uploadSpeed_;
    }

    double getVerifyProgress() const
    {
        return recheckProgress_;
    }

    bool hasTrackerSubstring(QString const& substr) const;

    Speed uploadLimit() const
    {
        return Speed::fromKBps(uploadLimit_);
    }

    Speed downloadLimit() const
    {
        return Speed::fromKBps(downloadLimit_);
    }

    bool uploadIsLimited() const
    {
        return uploadLimited_;
    }

    bool downloadIsLimited() const
    {
        return downloadLimited_;
    }

    bool honorsSessionLimits() const
    {
        return honorsSessionLimits_;
    }

    int peerLimit() const
    {
        return peerLimit_;
    }

    double seedRatioLimit() const
    {
        return seedRatioLimit_;
    }

    tr_ratiolimit seedRatioMode() const
    {
        return static_cast<tr_ratiolimit>(seedRatioMode_);
    }

    int seedIdleLimit() const
    {
        return seedIdleLimit_;
    }

    tr_idlelimit seedIdleMode() const
    {
        return static_cast<tr_idlelimit>(seedIdleMode_);
    }

    TrackerStatsList const& trackerStats() const
    {
        return trackerStats_;
    }

    QStringList const& trackers() const
    {
        return trackers_;
    }

    QStringList const& trackerDisplayNames() const
    {
        return trackerDisplayNames_;
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
        return queuePosition_;
    }

    bool isStalled() const
    {
        return isStalled_;
    }

    QString activityString() const;

    tr_torrent_activity getActivity() const
    {
        return static_cast<tr_torrent_activity>(status_);
    }

    bool isFinished() const
    {
        return isFinished_;
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

    bool update(tr_quark const* keys, tr_variant const* const* values, size_t n);

    QIcon getMimeTypeIcon() const
    {
        return icon_;
    }

    using KeyList = QSet<tr_quark>;
    static KeyList const allMainKeys;
    static KeyList const detailInfoKeys;
    static KeyList const detailStatKeys;
    static KeyList const mainInfoKeys;
    static KeyList const mainStatKeys;

private:
    void updateMimeIcon();

private:
    int const id_;

    bool downloadLimited_ = {};
    bool honorsSessionLimits_ = {};
    bool isFinished_ = {};
    bool isPrivate_ = {};
    bool isStalled_ = {};
    bool uploadLimited_ = {};

    time_t activityDate_ = {};
    time_t addedDate_ = {};
    time_t dateCreated_ = {};
    time_t editDate_ = {};
    time_t manualAnnounceTime_ = {};
    time_t startDate_ = {};

    int bandwidthPriority_ = {};
    int downloadLimit_ = {};
    int error_ = {};
    int eta_ = {};
    int peerLimit_ = {};
    int peersConnected_ = {};
    int peersGettingFromUs_ = {};
    int peersSendingToUs_ = {};
    int pieceCount_ = {};
    int queuePosition_ = {};
    int seedIdleLimit_ = {};
    int seedIdleMode_ = {};
    int seedRatioMode_ = {};
    int status_ = {};
    int uploadLimit_ = {};
    int webseedsSendingToUs_ = {};

    uint64_t desiredAvailable_ = {};
    uint64_t downloadedEver_ = {};
    uint64_t failedEver_ = {};
    uint64_t haveUnchecked_ = {};
    uint64_t haveVerified_ = {};
    uint64_t leftUntilDone_ = {};
    uint64_t pieceSize_ = {};
    uint64_t sizeWhenDone_ = {};
    uint64_t totalSize_ = {};
    uint64_t uploadedEver_ = {};

    double metadataPercentComplete_ = {};
    double percentDone_ = {};
    double recheckProgress_ = {};
    double seedRatioLimit_ = {};

    QString comment_;
    QString creator_;
    QString downloadDir_;
    QString errorString_;
    QString hashString_;
    QString name_;

    QIcon icon_;

    PeerList peers_;
    FileList files_;

    QStringList trackers_;
    QStringList trackerDisplayNames_;
    TrackerStatsList trackerStats_;

    Speed uploadSpeed_;
    Speed downloadSpeed_;

    Prefs const& prefs_;
};

Q_DECLARE_METATYPE(Torrent const*)
