// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstdint> // int64_t
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

#include "RpcClient.h"
#include "RpcQueue.h"
#include "Torrent.h"
#include "Typedefs.h"

class AddData;
class Prefs;

extern "C"
{
    struct tr_variant;
}

class Session : public QObject
{
    Q_OBJECT

public:
    Session(QString config_dir, Prefs& prefs);
    Session(Session&&) = delete;
    Session(Session const&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(Session const&) = delete;
    ~Session() override;

    void stop();
    void restart();

    [[nodiscard]] constexpr auto const& getRemoteUrl() const noexcept
    {
        return rpc_.url();
    }

    [[nodiscard]] constexpr auto const& getStats() const noexcept
    {
        return stats_;
    }

    [[nodiscard]] constexpr auto const& getCumulativeStats() const noexcept
    {
        return cumulative_stats_;
    }

    [[nodiscard]] constexpr auto const& sessionVersion() const noexcept
    {
        return session_version_;
    }

    [[nodiscard]] constexpr auto blocklistSize() const noexcept
    {
        return blocklist_size_;
    }

    enum PortTestIpProtocol : uint8_t
    {
        PORT_TEST_IPV4,
        PORT_TEST_IPV6,
        NUM_PORT_TEST_IP_PROTOCOL
    };

    void setBlocklistSize(int64_t i);
    void updateBlocklist();
    void portTest(PortTestIpProtocol ip_protocol);
    void copyMagnetLinkToClipboard(int torrent_id);

    bool portTestPending(PortTestIpProtocol ip_protocol) const noexcept;

    /** returns true if the transmission session is being run inside this client */
    [[nodiscard]] constexpr auto isServer() const noexcept
    {
        return session_ != nullptr;
    }

    /** returns true if isServer() is true or if the remote address is the localhost */
    [[nodiscard]] auto isLocal() const noexcept
    {
        return !session_id_.isEmpty() ? is_definitely_local_session_ : rpc_.isLocal();
    }

    RpcResponseFuture exec(tr_quark method, tr_variant* args);
    RpcResponseFuture exec(std::string_view method, tr_variant* args);

    using Tag = RpcQueue::Tag;
    Tag torrentSet(torrent_ids_t const& torrent_ids, tr_quark const key, bool val);
    Tag torrentSet(torrent_ids_t const& torrent_ids, tr_quark const key, int val);
    Tag torrentSet(torrent_ids_t const& torrent_ids, tr_quark const key, double val);
    Tag torrentSet(torrent_ids_t const& torrent_ids, tr_quark const key, QString const& val);
    Tag torrentSet(torrent_ids_t const& torrent_ids, tr_quark const key, std::vector<int> const& val);
    Tag torrentSet(torrent_ids_t const& torrent_ids, tr_quark const key, QStringList const& val);

    void torrentSetLocation(torrent_ids_t const& torrent_ids, QString const& path, bool do_move);
    void torrentRenamePath(torrent_ids_t const& torrent_ids, QString const& oldpath, QString const& newname);
    void addTorrent(AddData add_me, tr_variant* args_dict);
    void initTorrents(torrent_ids_t const& ids = {});
    void pauseTorrents(torrent_ids_t const& torrent_ids = {});
    void startTorrents(torrent_ids_t const& torrent_ids = {});
    void startTorrentsNow(torrent_ids_t const& torrent_ids = {});
    void refreshDetailInfo(torrent_ids_t const& torrent_ids);
    void refreshActiveTorrents();
    void refreshAllTorrents();
    void addNewlyCreatedTorrent(QString const& filename, QString const& local_path);
    void verifyTorrents(torrent_ids_t const& torrent_ids);
    void reannounceTorrents(torrent_ids_t const& torrent_ids);
    void refreshExtraStats(torrent_ids_t const& torrent_ids);

    enum class TorrentProperties
    {
        MainInfo,
        MainStats,
        MainAll,
        DetailInfo,
        DetailStat,
        Rename
    };

    void addKeyName(TorrentProperties props, tr_quark const key)
    {
        // populate names cache with default values
        if (names_[props].empty())
        {
            getKeyNames(props);
        }

        names_[props].emplace(tr_quark_get_string_view(key));
    }

    void removeKeyName(TorrentProperties props, tr_quark const key)
    {
        // do not remove id because it must be in every torrent req
        if (key != TR_KEY_id)
        {
            names_[props].erase(tr_quark_get_string_view(key));
        }
    }

public slots:
    void addTorrent(AddData add_me);
    void launchWebInterface() const;
    void queueMoveBottom(torrent_ids_t const& torrentIds = {});
    void queueMoveDown(torrent_ids_t const& torrentIds = {});
    void queueMoveTop(torrent_ids_t const& torrentIds = {});
    void queueMoveUp(torrent_ids_t const& torrentIds = {});
    void refreshSessionInfo();
    void refreshSessionStats();
    void removeTorrents(torrent_ids_t const& torrent_ids, bool delete_files = false);
    void updatePref(int key);

signals:
    void sourceChanged();
    void portTested(std::optional<bool> status, PortTestIpProtocol ip_protocol);
    void statsUpdated();
    void sessionUpdated();
    void blocklistUpdated(int);
    void torrentsUpdated(tr_variant* torrent_list, bool complete_list);
    void torrentsRemoved(tr_variant* torrent_list);
    void sessionCalled(Tag);
    void dataReadProgress();
    void dataSendProgress();
    void networkResponse(QNetworkReply::NetworkError code, QString const& message);
    void httpAuthenticationRequired();

private slots:
    void onDuplicatesTimer();

private:
    void start();

    void updateStats(tr_variant* args_dict);
    void updateInfo(tr_variant* args_dict);

    Tag torrentSetImpl(tr_variant* args);
    void sessionSet(tr_quark const key, QVariant const& value);
    void pumpRequests();
    void sendTorrentRequest(std::string_view request, torrent_ids_t const& torrent_ids);
    void refreshTorrents(torrent_ids_t const& ids, TorrentProperties props);
    std::set<std::string_view> const& getKeyNames(TorrentProperties props);

    static void updateStats(tr_variant* args_dict, tr_session_stats* stats);

    void addOptionalIds(tr_variant* args_dict, torrent_ids_t const& torrent_ids) const;

    QString const config_dir_;
    Prefs& prefs_;

    std::map<TorrentProperties, std::set<std::string_view>> names_;

    int64_t blocklist_size_ = -1;
    std::array<bool, NUM_PORT_TEST_IP_PROTOCOL> port_test_pending_ = {};
    tr_session* session_ = {};
    QStringList idle_json_;
    tr_session_stats stats_ = EmptyStats;
    tr_session_stats cumulative_stats_ = EmptyStats;
    QString session_version_;
    QString session_id_;
    bool is_definitely_local_session_ = true;
    RpcClient rpc_;
    torrent_ids_t const RecentlyActiveIDs = { -1 };

    std::map<QString, QString> duplicates_;
    QTimer duplicates_timer_;

    static auto constexpr EmptyStats = tr_session_stats{ TR_RATIO_NA, 0, 0, 0, 0, 0 };
};
