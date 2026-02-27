// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <bitset>
#include <cstddef> // size_t
#include <cstdint> // uint64_t
#include <ctime> // time_t
#include <optional>
#include <vector>

#include <QIcon>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

#include <libtransmission/transmission.h>

#include <libtransmission/crypto-utils.h>
#include "libtransmission/tr-macros.h"
#include <libtransmission/quark.h>

#include "IconCache.h"
#include "Speed.h"

class QPixmap;

class Prefs;
struct tr_variant;

struct Peer
{
    bool client_is_choked = {};
    bool client_is_interested = {};
    bool is_downloading_from = {};
    bool is_encrypted = {};
    bool is_incoming = {};
    bool is_uploading_to = {};
    bool peer_is_choked = {};
    bool peer_is_interested = {};
    QString address;
    QString client_name;
    QString flags;
    int port = {};
    Speed rate_to_client;
    Speed rate_to_peer;
    double progress = {};
};

using PeerList = std::vector<Peer>;

struct TrackerStat
{
    [[nodiscard]] QPixmap get_favicon() const;

    bool has_announced = {};
    bool has_scraped = {};
    bool is_backup = {};
    bool last_announce_succeeded = {};
    bool last_announce_timed_out = {};
    bool last_scrape_succeeded = {};
    bool last_scrape_timed_out = {};
    int announce_state = {};
    int download_count = {};
    int id = {};
    int last_announce_peer_count = {};
    int last_announce_start_time = {};
    int last_announce_time = {};
    int last_scrape_start_time = {};
    int last_scrape_time = {};
    int leecher_count = {};
    int next_announce_time = {};
    int next_scrape_time = {};
    int scrape_state = {};
    int seeder_count = {};
    int tier = {};
    QString announce;
    QString last_announce_result;
    QString last_scrape_result;
    QString sitename;
};

using TrackerStatsList = std::vector<TrackerStat>;

struct TorrentFile
{
    bool wanted = true;
    int index = -1;
    int priority = 0;
    QString filename;
    uint64_t size = 0;
    uint64_t have = 0;
};

using FileList = std::vector<TorrentFile>;

class TorrentHash
{
private:
    tr_sha1_digest_t data_ = {};
    QString data_str_;

public:
    TorrentHash() = default;

    explicit TorrentHash(tr_sha1_digest_t const& data)
        : data_{ data }
    {
        auto const hashstr = tr_sha1_to_string(data_);
        data_str_ = QString::fromUtf8(std::data(hashstr), std::size(hashstr));
    }

    explicit TorrentHash(std::string_view const str)
        : TorrentHash{ tr_sha1_from_string(str).value_or(tr_sha1_digest_t{}) }
    {
    }

    [[nodiscard]] constexpr auto operator==(TorrentHash const& that) const
    {
        return data_ == that.data_;
    }

    [[nodiscard]] constexpr auto operator!=(TorrentHash const& that) const
    {
        return !(*this == that);
    }

    [[nodiscard]] auto operator<(TorrentHash const& that) const
    {
        return data_ < that.data_;
    }

    [[nodiscard]] constexpr auto& to_string() const noexcept
    {
        return data_str_;
    }
};

class Torrent : public QObject
{
    Q_OBJECT

public:
    Torrent(Prefs const& prefs, int id);
    ~Torrent() override = default;
    Torrent(Torrent&&) = delete;
    Torrent(Torrent const&) = delete;
    Torrent& operator=(Torrent&&) = delete;
    Torrent& operator=(Torrent const&) = delete;

    [[nodiscard]] constexpr auto get_bandwidth_priority() const noexcept
    {
        return bandwidth_priority_;
    }

    [[nodiscard]] constexpr auto id() const noexcept
    {
        return id_;
    }

    [[nodiscard]] constexpr auto const& name() const noexcept
    {
        return name_;
    }

    bool has_name() const
    {
        return !name_.isEmpty();
    }

    [[nodiscard]] constexpr auto const& creator() const noexcept
    {
        return creator_;
    }

    [[nodiscard]] constexpr auto const& comment() const noexcept
    {
        return comment_;
    }

    [[nodiscard]] constexpr auto const& get_path() const noexcept
    {
        return download_dir_;
    }

    QString get_error() const;

    [[nodiscard]] constexpr auto const& tracker_list() const noexcept
    {
        return tracker_list_;
    }

    [[nodiscard]] constexpr auto const& hash() const noexcept
    {
        return hash_;
    }

    [[nodiscard]] constexpr auto has_error() const noexcept
    {
        return error_ != tr_stat::Error::Ok;
    }

    [[nodiscard]] constexpr auto left_until_done() const noexcept
    {
        return left_until_done_;
    }

    [[nodiscard]] constexpr auto is_done() const noexcept
    {
        return left_until_done() == 0;
    }

    [[nodiscard]] constexpr auto have_verified() const noexcept
    {
        return have_verified_;
    }

    [[nodiscard]] constexpr auto total_size() const noexcept
    {
        return total_size_;
    }

    [[nodiscard]] constexpr auto is_seed() const noexcept
    {
        return have_verified() >= total_size();
    }

    [[nodiscard]] constexpr auto is_private() const noexcept
    {
        return is_private_;
    }

    std::optional<double> get_seed_ratio_limit() const;

    [[nodiscard]] constexpr auto have_unverified() const noexcept
    {
        return have_unchecked_;
    }

    [[nodiscard]] constexpr auto desired_available() const noexcept
    {
        return desired_available_;
    }

    [[nodiscard]] constexpr auto have_total() const noexcept
    {
        return have_verified() + have_unverified();
    }

    [[nodiscard]] constexpr auto size_when_done() const noexcept
    {
        return size_when_done_;
    }

    [[nodiscard]] constexpr auto piece_size() const noexcept
    {
        return piece_size_;
    }

    [[nodiscard]] constexpr auto metadata_percent_done() const noexcept
    {
        return metadata_percent_complete_;
    }

    [[nodiscard]] constexpr auto has_metadata() const noexcept
    {
        return metadata_percent_done() >= 1.0;
    }

    [[nodiscard]] constexpr auto piece_count() const noexcept
    {
        return piece_count_;
    }

    [[nodiscard]] constexpr auto downloaded_ever() const noexcept
    {
        return downloaded_ever_;
    }

    [[nodiscard]] constexpr auto uploaded_ever() const noexcept
    {
        return uploaded_ever_;
    }

    [[nodiscard]] constexpr auto ratio() const noexcept
    {
        auto const numerator = static_cast<double>(uploaded_ever());
        auto const denominator = size_when_done();
        return denominator > 0U ? numerator / denominator : double{};
    }

    [[nodiscard]] constexpr double percent_complete() const noexcept
    {
        return total_size() != 0 ? have_total() / static_cast<double>(total_size()) : 0;
    }

    [[nodiscard]] constexpr double percent_done() const noexcept
    {
        auto const l = left_until_done();
        auto const s = size_when_done();
        return s ? static_cast<double>(s - l) / static_cast<double>(s) : 0.0;
    }

    [[nodiscard]] constexpr auto failed_ever() const noexcept
    {
        return failed_ever_;
    }

    int compare_seed_progress(Torrent const& that) const;
    int compare_ratio(Torrent const& that) const;
    int compare_eta(Torrent const& that) const;

    [[nodiscard]] constexpr auto get_eta() const noexcept
    {
        return eta_;
    }

    [[nodiscard]] constexpr auto has_eta() const noexcept
    {
        return get_eta() >= 0;
    }

    [[nodiscard]] constexpr auto last_activity() const noexcept
    {
        return activity_date_;
    }

    [[nodiscard]] constexpr auto last_started() const noexcept
    {
        return start_date_;
    }

    [[nodiscard]] constexpr auto date_added() const noexcept
    {
        return added_date_;
    }

    [[nodiscard]] constexpr auto date_created() const noexcept
    {
        return date_created_;
    }

    [[nodiscard]] constexpr auto date_edited() const noexcept
    {
        return edit_date_;
    }

    [[nodiscard]] constexpr auto manual_announce_time() const noexcept
    {
        return manual_announce_time_;
    }

    [[nodiscard]] constexpr auto peers_we_are_downloading_from() const noexcept
    {
        return peers_sending_to_us_;
    }

    [[nodiscard]] constexpr auto webseeds_we_are_downloading_from() const noexcept
    {
        return webseeds_sending_to_us_;
    }

    [[nodiscard]] constexpr auto peers_we_are_uploading_to() const noexcept
    {
        return peers_getting_from_us_;
    }

    [[nodiscard]] constexpr auto is_uploading() const noexcept
    {
        return peers_we_are_uploading_to() > 0;
    }

    [[nodiscard]] constexpr auto connected_peers() const noexcept
    {
        return peers_connected_;
    }

    [[nodiscard]] constexpr auto connected_peers_and_webseeds() const noexcept
    {
        return connected_peers() + webseeds_we_are_downloading_from();
    }

    [[nodiscard]] constexpr auto const& download_speed() const noexcept
    {
        return download_speed_;
    }

    [[nodiscard]] constexpr auto const& upload_speed() const noexcept
    {
        return upload_speed_;
    }

    [[nodiscard]] constexpr auto get_verify_progress() const noexcept
    {
        return recheck_progress_;
    }

    bool includes_tracker(QString const& sitename) const;

    [[nodiscard]] constexpr auto const& labels() const noexcept
    {
        return labels_;
    }

    [[nodiscard]] constexpr auto const& sitenames() const noexcept
    {
        return sitenames_;
    }

    [[nodiscard]] constexpr auto upload_limit() const
    {
        return Speed{ upload_limit_, Speed::Units::KByps };
    }

    [[nodiscard]] constexpr auto download_limit() const
    {
        return Speed{ download_limit_, Speed::Units::KByps };
    }

    [[nodiscard]] constexpr auto upload_is_limited() const noexcept
    {
        return upload_limited_;
    }

    [[nodiscard]] constexpr auto download_is_limited() const noexcept
    {
        return download_limited_;
    }

    [[nodiscard]] constexpr auto honors_session_limits() const noexcept
    {
        return honors_session_limits_;
    }

    [[nodiscard]] constexpr auto peer_limit() const noexcept
    {
        return peer_limit_;
    }

    [[nodiscard]] constexpr auto seed_ratio_limit() const noexcept
    {
        return seed_ratio_limit_;
    }

    [[nodiscard]] constexpr auto seed_ratio_mode() const noexcept
    {
        return static_cast<tr_ratiolimit>(seed_ratio_mode_);
    }

    [[nodiscard]] constexpr auto seed_idle_limit() const noexcept
    {
        return seed_idle_limit_;
    }

    [[nodiscard]] constexpr auto seed_idle_mode() const noexcept
    {
        return static_cast<tr_idlelimit>(seed_idle_mode_);
    }

    [[nodiscard]] constexpr auto const& tracker_stats() const noexcept
    {
        return tracker_stats_;
    }

    [[nodiscard]] constexpr auto const& peers() const noexcept
    {
        return peers_;
    }

    [[nodiscard]] constexpr auto const& files() const noexcept
    {
        return files_;
    }

    [[nodiscard]] auto constexpr queue_position() const noexcept
    {
        return queue_position_;
    }

    [[nodiscard]] auto constexpr is_stalled() const noexcept
    {
        return is_stalled_;
    }

    QString activity_string() const;

    [[nodiscard]] auto constexpr get_activity() const noexcept
    {
        return static_cast<tr_torrent_activity>(status_);
    }

    [[nodiscard]] auto constexpr is_finished() const noexcept
    {
        return is_finished_;
    }

    [[nodiscard]] auto constexpr is_paused() const noexcept
    {
        return get_activity() == TR_STATUS_STOPPED;
    }

    [[nodiscard]] auto constexpr is_waiting_to_verify() const noexcept
    {
        return get_activity() == TR_STATUS_CHECK_WAIT;
    }

    [[nodiscard]] auto constexpr is_verifying() const noexcept
    {
        return get_activity() == TR_STATUS_CHECK;
    }

    [[nodiscard]] auto constexpr is_downloading() const noexcept
    {
        return get_activity() == TR_STATUS_DOWNLOAD;
    }

    [[nodiscard]] auto constexpr is_waiting_to_download() const noexcept
    {
        return get_activity() == TR_STATUS_DOWNLOAD_WAIT;
    }

    [[nodiscard]] auto constexpr is_seeding() const noexcept
    {
        return get_activity() == TR_STATUS_SEED;
    }

    [[nodiscard]] auto constexpr is_waiting_to_seed() const noexcept
    {
        return get_activity() == TR_STATUS_SEED_WAIT;
    }

    [[nodiscard]] auto constexpr is_ready_to_transfer() const noexcept
    {
        return get_activity() == TR_STATUS_DOWNLOAD || get_activity() == TR_STATUS_SEED;
    }

    [[nodiscard]] auto constexpr is_queued() const noexcept
    {
        return is_waiting_to_download() || is_waiting_to_seed();
    }

    [[nodiscard]] auto constexpr can_manual_announce_at(time_t t) const noexcept
    {
        return is_ready_to_transfer() && (manual_announce_time() <= t);
    }

    QIcon get_mime_type_icon() const;

    enum Field : uint8_t
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
        TORRENT_ERROR,
        TORRENT_ERROR_STRING,
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
        LABELS,
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
        TRACKER_LIST,
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
    tr_torrent_id_t const id_;

    bool download_limited_ = {};
    bool honors_session_limits_ = {};
    bool is_finished_ = {};
    bool is_private_ = {};
    bool is_stalled_ = {};
    bool upload_limited_ = {};

    tr_stat::Error error_ = tr_stat::Error::Ok;

    time_t activity_date_ = {};
    time_t added_date_ = {};
    time_t date_created_ = {};
    time_t edit_date_ = {};
    time_t manual_announce_time_ = {};
    time_t start_date_ = {};

    int bandwidth_priority_ = {};
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
    int webseeds_sending_to_us_ = {};

    uint64_t desired_available_ = {};
    uint64_t download_limit_ = {};
    uint64_t downloaded_ever_ = {};
    uint64_t failed_ever_ = {};
    uint64_t file_count_ = {};
    uint64_t have_unchecked_ = {};
    uint64_t have_verified_ = {};
    uint64_t left_until_done_ = {};
    uint64_t piece_size_ = {};
    uint64_t size_when_done_ = {};
    uint64_t total_size_ = {};
    uint64_t upload_limit_ = {};
    uint64_t uploaded_ever_ = {};

    double metadata_percent_complete_ = {};
    double percent_done_ = {};
    double recheck_progress_ = {};
    double seed_ratio_limit_ = {};

    QString comment_;
    QString creator_;
    QString download_dir_;
    QString error_string_;
    QString name_;
    QString primary_mime_type_;
    QString tracker_list_;

    // mutable because it's a lazy lookup
    mutable QIcon icon_ = IconCache::get().file_icon();

    PeerList peers_;
    FileList files_;

    QStringList labels_;
    std::vector<QString> sitenames_;
    TrackerStatsList tracker_stats_;

    Speed upload_speed_;
    Speed download_speed_;

    Prefs const& prefs_;

    TorrentHash hash_;
};

Q_DECLARE_METATYPE(Torrent const*)
