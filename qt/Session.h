// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstdint> // int64_t
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QNetworkReply>
#include <QTimer>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

#include "RpcClient.h"
#include "RpcQueue.h"
#include "Torrent.h"
#include "Typedefs.h"

class AddData;
class Prefs;
struct tr_variant;

class Session : public QObject
{
    Q_OBJECT

public:
    Session(QString config_dir, Prefs& prefs, RpcClient& rpc);
    Session(Session&&) = delete;
    Session(Session const&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(Session const&) = delete;
    ~Session() override;

    void stop();
    void restart();

    [[nodiscard]] constexpr auto const& get_remote_url() const noexcept
    {
        return rpc_.url();
    }

    [[nodiscard]] constexpr auto const& get_stats() const noexcept
    {
        return stats_;
    }

    [[nodiscard]] constexpr auto const& get_cumulative_stats() const noexcept
    {
        return cumulative_stats_;
    }

    [[nodiscard]] constexpr auto const& session_version() const noexcept
    {
        return session_version_;
    }

    [[nodiscard]] constexpr auto blocklist_size() const noexcept
    {
        return blocklist_size_;
    }

    enum PortTestIpProtocol : uint8_t
    {
        PORT_TEST_IPV4,
        PORT_TEST_IPV6,
        NUM_PORT_TEST_IP_PROTOCOL
    };

    void set_blocklist_size(int64_t i);
    void update_blocklist();
    void port_test(PortTestIpProtocol ip_protocol);
    void copy_magnet_link_to_clipboard(int torrent_id);

    [[nodiscard]] bool port_test_pending(PortTestIpProtocol ip_protocol) const noexcept;

    /** returns true if the transmission session is being run inside this client */
    [[nodiscard]] constexpr auto is_server() const noexcept
    {
        return session_ != nullptr;
    }

    /** returns true if is_local() is true or if the remote address is the localhost */
    [[nodiscard]] auto is_local() const noexcept
    {
        return !session_id_.isEmpty() ? is_definitely_local_session_ : rpc_.is_local();
    }

    RpcResponseFuture exec(tr_quark method, tr_variant* args);

    using Tag = RpcQueue::Tag;
    Tag torrent_set(torrent_ids_t const& torrent_ids, tr_quark key, bool val);
    Tag torrent_set(torrent_ids_t const& torrent_ids, tr_quark key, int val);
    Tag torrent_set(torrent_ids_t const& torrent_ids, tr_quark key, double val);
    Tag torrent_set(torrent_ids_t const& torrent_ids, tr_quark key, QString const& val);
    Tag torrent_set(torrent_ids_t const& torrent_ids, tr_quark key, std::vector<int> const& val);
    Tag torrent_set(torrent_ids_t const& torrent_ids, tr_quark key, QStringList const& val);

    void torrent_set_location(torrent_ids_t const& torrent_ids, QString const& path, bool do_move);
    void torrent_rename_path(torrent_ids_t const& torrent_ids, QString const& oldpath, QString const& newname);
    void add_torrent(AddData const& add_me, tr_variant* args_dict);
    void init_torrents(torrent_ids_t const& ids = {});
    void pause_torrents(torrent_ids_t const& torrent_ids = {});
    void start_torrents(torrent_ids_t const& torrent_ids = {});
    void start_torrents_now(torrent_ids_t const& torrent_ids = {});
    void refresh_detail_info(torrent_ids_t const& torrent_ids);
    void refresh_active_torrents();
    void refresh_all_torrents();
    void add_newly_created_torrent(QString const& filename, QString const& local_path);
    void verify_torrents(torrent_ids_t const& torrent_ids);
    void reannounce_torrents(torrent_ids_t const& torrent_ids);
    void refresh_extra_stats(torrent_ids_t const& torrent_ids);

    enum class TorrentProperties : uint8_t
    {
        MainInfo,
        MainStats,
        MainAll,
        DetailInfo,
        DetailStat,
        Rename
    };

public slots:
    void add_torrent(AddData const& add_me);
    void launch_web_interface() const;
    void queue_move_bottom(torrent_ids_t const& torrentIds = {});
    void queue_move_down(torrent_ids_t const& torrentIds = {});
    void queue_move_top(torrent_ids_t const& torrentIds = {});
    void queue_move_up(torrent_ids_t const& torrentIds = {});
    void refresh_session_info();
    void refresh_session_stats();
    void remove_torrents(torrent_ids_t const& torrent_ids, bool delete_files = false);
    void update_pref(int key);

signals:
    void source_changed();
    void port_tested(std::optional<bool> status, PortTestIpProtocol ip_protocol);
    void stats_updated();
    void session_updated();
    void blocklist_updated(int);
    void torrents_updated(tr_variant* torrent_list, bool complete_list);
    void torrents_removed(tr_variant* torrent_list);
    void session_called(Tag);
    void data_read_progress();
    void data_send_progress();
    void network_response(QNetworkReply::NetworkError code, QString const& message);
    void http_authentication_required();

private slots:
    void on_duplicates_timer();

private:
    void start();

    void update_stats(tr_variant* args_dict);
    void update_info(tr_variant* args_dict);

    Tag torrent_set_impl(tr_variant* args);
    void session_set(tr_quark key, tr_variant val);
    void pump_requests();
    void send_torrent_request(tr_quark method, torrent_ids_t const& torrent_ids);
    void refresh_torrents(torrent_ids_t const& ids, TorrentProperties props);

    static void update_stats(tr_variant* args_dict, tr_session_stats* stats);

    void add_optional_ids(tr_variant::Map& params, torrent_ids_t const& torrent_ids) const;
    void add_optional_ids(tr_variant* args_dict, torrent_ids_t const& torrent_ids) const;

    QString const config_dir_;
    Prefs& prefs_;

    int64_t blocklist_size_ = -1;
    std::array<bool, NUM_PORT_TEST_IP_PROTOCOL> port_test_pending_ = {};
    tr_session* session_ = {};
    QStringList idle_json_;
    tr_session_stats stats_ = EmptyStats;
    tr_session_stats cumulative_stats_ = EmptyStats;
    QString session_version_;
    QString session_id_;
    bool is_definitely_local_session_ = true;
    RpcClient& rpc_;

    static inline torrent_ids_t const RecentlyActiveIDs = { -1 };

    std::map<QString, QString> duplicates_;
    QTimer duplicates_timer_;

    static auto constexpr EmptyStats = tr_session_stats{
        .ratio = TR_RATIO_NA,
        .uploadedBytes = 0,
        .downloadedBytes = 0,
        .filesAdded = 0,
        .sessionCount = 0,
        .secondsActive = 0,
    };
};
