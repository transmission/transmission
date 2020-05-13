/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <ctime> // time_t

#include <QObject>
#include <QIcon>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariant>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

#include "CustomVariantType.h"
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

Q_DECLARE_METATYPE(Peer)

typedef QList<Peer> PeerList;
Q_DECLARE_METATYPE(PeerList)

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

Q_DECLARE_METATYPE(TrackerStat)

typedef QList<TrackerStat> TrackerStatsList;
Q_DECLARE_METATYPE(TrackerStatsList)

struct TorrentFile
{
    TorrentFile() :
        wanted(true),
        index(-1),
        priority(0),
        size(0),
        have(0)
    {
    }

    bool wanted;
    int index;
    int priority;
    QString filename;
    uint64_t size;
    uint64_t have;
};

Q_DECLARE_METATYPE(TorrentFile)

typedef QList<TorrentFile> FileList;
Q_DECLARE_METATYPE(FileList)

class Torrent : public QObject
{
    Q_OBJECT

public:
    enum
    {
        UPLOAD_SPEED,
        DOWNLOAD_SPEED,
        DOWNLOAD_DIR,
        ACTIVITY,
        NAME,
        ERROR,
        ERROR_STRING,
        SIZE_WHEN_DONE,
        LEFT_UNTIL_DONE,
        HAVE_UNCHECKED,
        HAVE_VERIFIED,
        DESIRED_AVAILABLE,
        TOTAL_SIZE,
        PIECE_SIZE,
        PIECE_COUNT,
        PEERS_GETTING_FROM_US,
        PEERS_SENDING_TO_US,
        WEBSEEDS_SENDING_TO_US,
        PERCENT_DONE,
        METADATA_PERCENT_DONE,
        PERCENT_VERIFIED,
        DATE_ACTIVITY,
        DATE_ADDED,
        DATE_STARTED,
        DATE_CREATED,
        PEERS_CONNECTED,
        ETA,
        DOWNLOADED_EVER,
        UPLOADED_EVER,
        FAILED_EVER,
        TRACKERSTATS,
        MIME_ICON,
        SEED_RATIO_LIMIT,
        SEED_RATIO_MODE,
        SEED_IDLE_LIMIT,
        SEED_IDLE_MODE,
        DOWN_LIMIT,
        DOWN_LIMITED,
        UP_LIMIT,
        UP_LIMITED,
        HONORS_SESSION_LIMITS,
        PEER_LIMIT,
        HASH_STRING,
        IS_FINISHED,
        IS_PRIVATE,
        IS_STALLED,
        COMMENT,
        CREATOR,
        MANUAL_ANNOUNCE_TIME,
        PEERS,
        BANDWIDTH_PRIORITY,
        QUEUE_POSITION,
        EDIT_DATE,
        //
        PROPERTY_COUNT
    };

public:
    Torrent(Prefs const&, int id);

    int getBandwidthPriority() const
    {
        return getInt(BANDWIDTH_PRIORITY);
    }

    int id() const
    {
        return myId;
    }

    QString name() const
    {
        return getString(NAME);
    }

    bool hasName() const
    {
        return !myValues[NAME].isNull();
    }

    QString creator() const
    {
        return getString(CREATOR);
    }

    QString comment() const
    {
        return getString(COMMENT);
    }

    QString getPath() const
    {
        return getString(DOWNLOAD_DIR);
    }

    QString getError() const;

    QString hashString() const
    {
        return getString(HASH_STRING);
    }

    bool hasError() const
    {
        return getInt(ERROR) != TR_STAT_OK;
    }

    bool isDone() const
    {
        return getSize(LEFT_UNTIL_DONE) == 0;
    }

    bool isSeed() const
    {
        return haveVerified() >= totalSize();
    }

    bool isPrivate() const
    {
        return getBool(IS_PRIVATE);
    }

    bool getSeedRatio(double& setme) const;

    uint64_t haveVerified() const
    {
        return getSize(HAVE_VERIFIED);
    }

    uint64_t haveUnverified() const
    {
        return getSize(HAVE_UNCHECKED);
    }

    uint64_t desiredAvailable() const
    {
        return getSize(DESIRED_AVAILABLE);
    }

    uint64_t haveTotal() const
    {
        return haveVerified() + haveUnverified();
    }

    uint64_t totalSize() const
    {
        return getSize(TOTAL_SIZE);
    }

    uint64_t sizeWhenDone() const
    {
        return getSize(SIZE_WHEN_DONE);
    }

    uint64_t leftUntilDone() const
    {
        return getSize(LEFT_UNTIL_DONE);
    }

    uint64_t pieceSize() const
    {
        return getSize(PIECE_SIZE);
    }

    bool hasMetadata() const
    {
        return getDouble(METADATA_PERCENT_DONE) >= 1.0;
    }

    int pieceCount() const
    {
        return getInt(PIECE_COUNT);
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
        return getDouble(METADATA_PERCENT_DONE);
    }

    uint64_t downloadedEver() const
    {
        return getSize(DOWNLOADED_EVER);
    }

    uint64_t uploadedEver() const
    {
        return getSize(UPLOADED_EVER);
    }

    uint64_t failedEver() const
    {
        return getSize(FAILED_EVER);
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
        return getInt(ETA);
    }

    time_t lastActivity() const
    {
        return getTime(DATE_ACTIVITY);
    }

    time_t lastStarted() const
    {
        return getTime(DATE_STARTED);
    }

    time_t dateAdded() const
    {
        return getTime(DATE_ADDED);
    }

    time_t dateCreated() const
    {
        return getTime(DATE_CREATED);
    }

    time_t manualAnnounceTime() const
    {
        return getTime(MANUAL_ANNOUNCE_TIME);
    }

    bool canManualAnnounceAt(time_t t) const
    {
        return isReadyToTransfer() && (manualAnnounceTime() <= t);
    }

    int peersWeAreDownloadingFrom() const
    {
        return getInt(PEERS_SENDING_TO_US);
    }

    int webseedsWeAreDownloadingFrom() const
    {
        return getInt(WEBSEEDS_SENDING_TO_US);
    }

    int peersWeAreUploadingTo() const
    {
        return getInt(PEERS_GETTING_FROM_US);
    }

    bool isUploading() const
    {
        return peersWeAreUploadingTo() > 0;
    }

    int connectedPeers() const
    {
        return getInt(PEERS_CONNECTED);
    }

    int connectedPeersAndWebseeds() const
    {
        return connectedPeers() + getInt(WEBSEEDS_SENDING_TO_US);
    }

    Speed downloadSpeed() const
    {
        return Speed::fromBps(getSize(DOWNLOAD_SPEED));
    }

    Speed uploadSpeed() const
    {
        return Speed::fromBps(getSize(UPLOAD_SPEED));
    }

    double getVerifyProgress() const
    {
        return getDouble(PERCENT_VERIFIED);
    }

    bool hasTrackerSubstring(QString const& substr) const;

    Speed uploadLimit() const
    {
        return Speed::fromKBps(getInt(UP_LIMIT));
    }

    Speed downloadLimit() const
    {
        return Speed::fromKBps(getInt(DOWN_LIMIT));
    }

    bool uploadIsLimited() const
    {
        return getBool(UP_LIMITED);
    }

    bool downloadIsLimited() const
    {
        return getBool(DOWN_LIMITED);
    }

    bool honorsSessionLimits() const
    {
        return getBool(HONORS_SESSION_LIMITS);
    }

    int peerLimit() const
    {
        return getInt(PEER_LIMIT);
    }

    double seedRatioLimit() const
    {
        return getDouble(SEED_RATIO_LIMIT);
    }

    tr_ratiolimit seedRatioMode() const
    {
        return static_cast<tr_ratiolimit>(getInt(SEED_RATIO_MODE));
    }

    int seedIdleLimit() const
    {
        return getInt(SEED_IDLE_LIMIT);
    }

    tr_idlelimit seedIdleMode() const
    {
        return static_cast<tr_idlelimit>(getInt(SEED_IDLE_MODE));
    }

    TrackerStatsList trackerStats() const
    {
        return myValues[TRACKERSTATS].value<TrackerStatsList>();
    }

    QStringList const& trackers() const
    {
        return trackers_;
    }

    QStringList const& trackerDisplayNames() const
    {
        return trackerDisplayNames_;
    }

    PeerList peers() const
    {
        return myValues[PEERS].value<PeerList>();
    }

    FileList const& files() const
    {
        return myFiles;
    }

    int queuePosition() const
    {
        return getInt(QUEUE_POSITION);
    }

    bool isStalled() const
    {
        return getBool(IS_STALLED);
    }

    QString activityString() const;

    tr_torrent_activity getActivity() const
    {
        return static_cast<tr_torrent_activity>(getInt(ACTIVITY));
    }

    bool isFinished() const
    {
        return getBool(IS_FINISHED);
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

    bool update(tr_quark const* keys, tr_variant** values, size_t n);

    QIcon getMimeTypeIcon() const
    {
        return getIcon(MIME_ICON);
    }

    using KeyList = QSet<tr_quark>;
    static KeyList const allMainKeys;
    static KeyList const detailInfoKeys;
    static KeyList const detailStatKeys;
    static KeyList const mainInfoKeys;
    static KeyList const mainStatKeys;

private:
    int getInt(int key) const;
    bool getBool(int key) const;
    QIcon getIcon(int key) const;
    double getDouble(int key) const;
    qulonglong getSize(int key) const;
    QString getString(int key) const;
    time_t getTime(int key) const;

    bool setInt(int key, int value);
    bool setBool(int key, bool value);
    bool setIcon(int key, QIcon const&);
    bool setDouble(int key, double);
    bool setString(int key, char const*, size_t len);
    bool setSize(int key, qulonglong);
    bool setTime(int key, time_t);

    QStringList trackers_;
    QStringList trackerDisplayNames_;

    char const* getMimeTypeString() const;
    void updateMimeIcon();

private:
    int const myId;
    Prefs const& myPrefs;
    QVariant myValues[PROPERTY_COUNT];
    FileList myFiles;
};

Q_DECLARE_METATYPE(Torrent const*)
