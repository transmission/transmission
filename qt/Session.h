/*
 * This file Copyright (C) 2009-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

#include "RpcClient.h"
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
    Session(QString const& configDir, Prefs& prefs);
    virtual ~Session();

    void stop();
    void restart();

    QUrl const& getRemoteUrl() const
    {
        return myRpc.url();
    }

    tr_session_stats const& getStats() const
    {
        return myStats;
    }

    tr_session_stats const& getCumulativeStats() const
    {
        return myCumulativeStats;
    }

    QString const& sessionVersion() const
    {
        return mySessionVersion;
    }

    int64_t blocklistSize() const
    {
        return myBlocklistSize;
    }

    void setBlocklistSize(int64_t i);
    void updateBlocklist();
    void portTest();
    void copyMagnetLinkToClipboard(int torrentId);

    /** returns true if the transmission session is being run inside this client */
    bool isServer() const;

    /** returns true if isServer() is true or if the remote address is the localhost */
    bool isLocal() const;

    RpcResponseFuture exec(tr_quark method, tr_variant* args);
    RpcResponseFuture exec(char const* method, tr_variant* args);

    void torrentSet(torrent_ids_t const& ids, tr_quark const key, bool val);
    void torrentSet(torrent_ids_t const& ids, tr_quark const key, int val);
    void torrentSet(torrent_ids_t const& ids, tr_quark const key, double val);
    void torrentSet(torrent_ids_t const& ids, tr_quark const key, QList<int> const& val);
    void torrentSet(torrent_ids_t const& ids, tr_quark const key, QStringList const& val);
    void torrentSet(torrent_ids_t const& ids, tr_quark const key, QPair<int, QString> const& val);
    void torrentSetLocation(torrent_ids_t const& ids, QString const& path, bool doMove);
    void torrentRenamePath(torrent_ids_t const& ids, QString const& oldpath, QString const& newname);
    void addTorrent(AddData const& addme, tr_variant* top, bool trashOriginal);
    void initTorrents(torrent_ids_t const& ids = {});
    void pauseTorrents(torrent_ids_t const& torrentIds = {});
    void startTorrents(torrent_ids_t const& torrentIds = {});
    void startTorrentsNow(torrent_ids_t const& torrentIds = {});
    void refreshDetailInfo(torrent_ids_t const& torrentIds);
    void refreshActiveTorrents();
    void refreshAllTorrents();
    void addNewlyCreatedTorrent(QString const& filename, QString const& localPath);
    void verifyTorrents(torrent_ids_t const& torrentIds);
    void reannounceTorrents(torrent_ids_t const& torrentIds);
    void refreshExtraStats(torrent_ids_t const& ids);

public slots:
    void addTorrent(AddData const& addme);
    void launchWebInterface();
    void queueMoveBottom(torrent_ids_t const& torrentIds = {});
    void queueMoveDown(torrent_ids_t const& torrentIds = {});
    void queueMoveTop(torrent_ids_t const& torrentIds = {});
    void queueMoveUp(torrent_ids_t const& torrentIds = {});
    void refreshSessionInfo();
    void refreshSessionStats();
    void removeTorrents(torrent_ids_t const& torrentIds, bool deleteFiles = false);
    void updatePref(int key);

signals:
    void sourceChanged();
    void portTested(bool isOpen);
    void statsUpdated();
    void sessionUpdated();
    void blocklistUpdated(int);
    void torrentsUpdated(tr_variant* torrentList, bool completeList);
    void torrentsRemoved(tr_variant* torrentList);
    void dataReadProgress();
    void dataSendProgress();
    void networkResponse(QNetworkReply::NetworkError code, QString const& message);
    void httpAuthenticationRequired();

private:
    void start();

    void updateStats(tr_variant* args);
    void updateInfo(tr_variant* args);

    void sessionSet(tr_quark const key, QVariant const& variant);
    void pumpRequests();
    void sendTorrentRequest(char const* request, torrent_ids_t const& torrentIds);
    void refreshTorrents(torrent_ids_t const& torrentIds, Torrent::KeyList const& keys);

    static void updateStats(tr_variant* d, tr_session_stats* stats);

private:
    QString const myConfigDir;
    Prefs& myPrefs;

    int64_t myBlocklistSize;
    tr_session* mySession;
    QStringList myIdleJSON;
    tr_session_stats myStats;
    tr_session_stats myCumulativeStats;
    QString mySessionVersion;
    QString mySessionId;
    bool myIsDefinitelyLocalSession;
    RpcClient myRpc;
};
