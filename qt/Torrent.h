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
#include <QVector>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

#include "CustomVariantType.h"
#include "Typedefs.h"

#ifdef ERROR
#undef ERROR
#endif

class QPixmap;

class Prefs;

extern "C"
{
struct tr_variant;
}

/***
****  PEER
***/

#define FOREACH_PEER_RPC_FIELD(EACH) \
    EACH(QString, address) \
    EACH(QString, clientName) \
    EACH(QString, flagStr) \
    EACH(double, progress) \
    EACH(Bps_t, rateToClient) \
    EACH(Bps_t, rateToPeer) \
    EACH(bool, isEncrypted)

struct Peer
{
    // rpc fields
#define GENERATE_STRUCT(type, key) type key = {};
    FOREACH_PEER_RPC_FIELD(GENERATE_STRUCT)
#undef GENERATE_STRUCT
};

/***
****  TRACKER
***/

#define FOREACH_TRACKER_RPC_FIELD(EACH) \
    EACH(QString, announce) \
    EACH(QString, lastAnnounceResult) \
    EACH(QString, lastScrapeResult) \
    EACH(QString, scrape) \
    EACH(int64_t, id) \
    EACH(time_t, lastAnnounceStartTime) \
    EACH(time_t, lastAnnounceTime) \
    EACH(time_t, lastScrapeStartTime) \
    EACH(time_t, lastScrapeTime) \
    EACH(time_t, nextAnnounceTime) \
    EACH(time_t, nextScrapeTime) \
    EACH(int, lastAnnouncePeerCount) \
    EACH(int, leecherCount) \
    EACH(int, seederCount) \
    EACH(int, tier) \
    EACH(tr_tracker_state, announceState) \
    EACH(tr_tracker_state, scrapeState) \
    EACH(bool, hasAnnounced) \
    EACH(bool, hasScraped) \
    EACH(bool, isBackup) \
    EACH(bool, lastAnnounceSucceeded) \
    EACH(bool, lastAnnounceTimedOut) \
    EACH(bool, lastScrapeSucceeded) \
    EACH(bool, lastScrapeTimedOut)

struct TrackerStat
{
    // rpc fields
#define GENERATE_STRUCT(type, key) type key = {};
    FOREACH_TRACKER_RPC_FIELD(GENERATE_STRUCT)
#undef GENERATE_STRUCT

    // derived fields
    QString displayName;
    QPixmap getFavicon() const;
};

/***
****  FILE
***/

// type, key, field
#define FOREACH_FILE_RPC_FIELD(EACH) \
    EACH(QString, TR_KEY_name, filename) \
    EACH(uint64_t, TR_KEY_bytesCompleted, have) \
    EACH(uint64_t, TR_KEY_length, size) \
    EACH(uint64_t, TR_KEY_wanted, wanted) \
    EACH(int, TR_KEY_priority, priority)

struct TorrentFile
{
    // rpc fields
#define GENERATE_STRUCT(type, key, field) type field = {};
    FOREACH_FILE_RPC_FIELD(GENERATE_STRUCT)
#undef GENERATE_STRUCT

    // derived fields
    int index = -1;
};

using FileList = QVector<TorrentFile>;

/***
****  TORRENT
***/

// type, rpc, name
#define FOREACH_TORRENT_RPC_FIELD(EACH) \
    EACH(QString, comment, comment) \
    EACH(QString, creator, creator) \
    EACH(QString, downloadDir, path) \
    EACH(QString, errorString, errorString) \
    EACH(QString, hashString, hashString) \
    EACH(QString, name, name) \
    EACH(QVector<Peer>, peers, peers) \
    EACH(QVector<TorrentFile>, files, files) \
    EACH(QVector<TrackerStat>, trackers, trackers) \
    EACH(double, metadataPercentComplete, metadataProgress) \
    EACH(double, recheckProgress, verifyProgress) \
    EACH(double, seedRatioLimit, seedRatioLimit) \
    EACH(uint64_t, pieceSize, pieceSize) \
    EACH(bytes_t, corruptEver, failedEver) \
    EACH(bytes_t, desiredAvailable, desiredAvailable) \
    EACH(bytes_t, downloadedEver, downloadedEver) \
    EACH(bytes_t, haveUnchecked, haveUnverified) \
    EACH(bytes_t, haveValid, haveVerified) \
    EACH(bytes_t, leftUntilDone, leftUntilDone) \
    EACH(bytes_t, sizeWhenDone, sizeWhenDone) \
    EACH(bytes_t, totalSize, totalSize) \
    EACH(bytes_t, uploadedEver, uploadedEver) \
    EACH(Bps_t, rateDownload, downloadSpeed) \
    EACH(Bps_t, rateUpload, uploadSpeed) \
    EACH(KBps_t, downloadLimit, downloadLimit) \
    EACH(KBps_t, uploadLimit, uploadLimit) \
    EACH(time_t, activityDate, activityDate) \
    EACH(time_t, addedDate, addedDate) \
    EACH(time_t, dateCreated, creationDate) \
    EACH(time_t, editDate, editDate) \
    EACH(time_t, manualAnnounceTime, manualAnnounceTime) \
    EACH(time_t, startDate, startDate) \
    EACH(int, bandwidthPriority, bandwidthPriority) \
    EACH(int, id, id) \
    EACH(int, peer_limit, peerLimit) \
    EACH(int, peersConnected, connectedPeers) \
    EACH(int, peersGettingFromUs, peersWeAreUploadingTo) \
    EACH(int, peersSendingToUs, peersWeAreDownloadingFrom) \
    EACH(int, pieceCount, pieceCount) \
    EACH(int, queuePosition, queuePosition) \
    EACH(int, seedIdleLimit, seedIdleLimit) \
    EACH(int, webseedsSendingToUs, webseedsWeAreDownloadingFrom) \
    EACH(tr_idlelimit, seedIdleMode, seedIdleMode) \
    EACH(tr_ratiolimit, seedRatioMode, seedRatioMode) \
    EACH(tr_torrent_activity, status, activity) \
    EACH(tr_stat_errtype, error, error) \
    EACH(seconds_t, eta, eta) \
    EACH(bool, downloadLimited, downloadIsLimited) \
    EACH(bool, honorsSessionLimits, honorsSessionLimits) \
    EACH(bool, isFinished, isFinished) \
    EACH(bool, isPrivate, isPrivate) \
    EACH(bool, isStalled, isStalled) \
    EACH(bool, uploadLimited, uploadIsLimited)

/* Generate the bit number for each field.
   This is just a plain vanilla [0...n) enum used for generating
   the TorrentFields enums bitfield offsets a few lines down. */
enum TorrentFieldNumbers
{
#define GENERATE_FIELD_NUMBERS(type, rpc, name) TFN_ ## name,
    FOREACH_TORRENT_RPC_FIELD(GENERATE_FIELD_NUMBERS)
#undef GENERATE_FIELD_NUMBERS
};

enum TorrentFields
{
#define GENERATE_BITFIELD(type, rpc, name) TF_ ## name = (1ull << TFN_ ## name),
    FOREACH_TORRENT_RPC_FIELD(GENERATE_BITFIELD)
#undef GENERATE_BITFIELD
};

class Torrent : public QObject
{
    Q_OBJECT

public:
    Torrent(Prefs const&, int id);

#define GENERATE_GETTER(type, rpc, name) const type& name() const { return name ## _; }
    FOREACH_TORRENT_RPC_FIELD(GENERATE_GETTER)
#undef GENERATE_GETTER

    bool hasName() const
    {
        return !name_.isEmpty();
    }

    QString getError() const;

    bool hasError() const
    {
        return error_ != TR_STAT_OK;
    }

    bool isDone() const
    {
        return leftUntilDone_ == bytes_t(0);
    }

    bool isSeed() const
    {
        return haveVerified_ >= totalSize_;
    }

    bool getSeedRatio(double& setme) const;

    auto haveTotal() const
    {
        return haveVerified_ + haveUnverified_;
    }

    bool hasMetadata() const
    {
        return metadataProgress_ >= 1.0;
    }

    double ratio() const
    {
        return double(uploadedEver_.value()) / std::max(downloadedEver_, totalSize_).value();
    }

    double percentComplete() const
    {
        auto const have = haveTotal().value();
        auto const total = totalSize().value();
        return total != 0 ? have / double(total) : 0;
    }

    double percentDone() const
    {
        auto const l = leftUntilDone_.value();
        auto const s = sizeWhenDone_.value();
        return s ? double(s - l) / s : 0.0;
    }

    int compareTracker(Torrent const&) const;
    int compareSeedRatio(Torrent const&) const;
    int compareRatio(Torrent const&) const;
    int compareETA(Torrent const&) const;

    bool hasETA() const
    {
        return eta() >= 0_s;
    }

    bool canManualAnnounceAt(time_t t) const
    {
        return isReadyToTransfer() && (manualAnnounceTime() <= t);
    }

    bool isUploading() const
    {
        return peersWeAreUploadingTo_ > 0;
    }

    auto connectedPeersAndWebseeds() const
    {
        return connectedPeers_ + webseedsWeAreDownloadingFrom_;
    }

    bool hasFileSubstring(QString const& substr) const;
    bool hasTrackerSubstring(QString const& substr) const;

    QString activityString() const;

    bool isPaused() const
    {
        return activity_ == TR_STATUS_STOPPED;
    }

    bool isWaitingToVerify() const
    {
        return activity_ == TR_STATUS_CHECK_WAIT;
    }

    bool isVerifying() const
    {
        return activity_ == TR_STATUS_CHECK;
    }

    bool isDownloading() const
    {
        return activity_ == TR_STATUS_DOWNLOAD;
    }

    bool isWaitingToDownload() const
    {
        return activity_ == TR_STATUS_DOWNLOAD_WAIT;
    }

    bool isSeeding() const
    {
        return activity_ == TR_STATUS_SEED;
    }

    bool isWaitingToSeed() const
    {
        return activity_ == TR_STATUS_SEED_WAIT;
    }

    bool isReadyToTransfer() const
    {
        return activity_ == TR_STATUS_DOWNLOAD || activity_ == TR_STATUS_SEED;
    }

    bool isQueued() const
    {
        return isWaitingToDownload() || isWaitingToSeed();
    }

    using fields_t = uint64_t;

    fields_t update(tr_quark const* keys, tr_variant** values, size_t n);

    QIcon icon() const
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
    Prefs const& prefs_;

    // rpc fields
#define GENERATE_FIELD(type, rpc, name) type name ## _ = {};
    FOREACH_TORRENT_RPC_FIELD(GENERATE_FIELD)
#undef GENERATE_FIELD

    // derived fields
    QIcon icon_;
    void updateMimeIcon();
};

Q_DECLARE_METATYPE(Torrent const*)
