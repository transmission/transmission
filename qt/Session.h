/*
 * This file Copyright (C) 2009-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

#include "RpcClient.h"
#include "Torrent.h"

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

    void torrentSet(QSet<int> const& ids, tr_quark const key, bool val);
    void torrentSet(QSet<int> const& ids, tr_quark const key, int val);
    void torrentSet(QSet<int> const& ids, tr_quark const key, double val);
    void torrentSet(QSet<int> const& ids, tr_quark const key, QList<int> const& val);
    void torrentSet(QSet<int> const& ids, tr_quark const key, QStringList const& val);
    void torrentSet(QSet<int> const& ids, tr_quark const key, QPair<int, QString> const& val);
    void torrentSetLocation(QSet<int> const& ids, QString const& path, bool doMove);
    void torrentRenamePath(QSet<int> const& ids, QString const& oldpath, QString const& newname);
    void addTorrent(AddData const& addme, tr_variant* top, bool trashOriginal);

public slots:
    void pauseTorrents(QSet<int> const& torrentIds = QSet<int>());
    void startTorrents(QSet<int> const& torrentIds = QSet<int>());
    void startTorrentsNow(QSet<int> const& torrentIds = QSet<int>());
    void queueMoveTop(QSet<int> const& torrentIds = QSet<int>());
    void queueMoveUp(QSet<int> const& torrentIds = QSet<int>());
    void queueMoveDown(QSet<int> const& torrentIds = QSet<int>());
    void queueMoveBottom(QSet<int> const& torrentIds = QSet<int>());
    void refreshSessionInfo();
    void refreshSessionStats();
    void refreshActiveTorrents();
    void refreshAllTorrents();
    void initTorrents(QSet<int> const& ids = QSet<int>());
    void addNewlyCreatedTorrent(QString const& filename, QString const& localPath);
    void addTorrent(AddData const& addme);
    void removeTorrents(QSet<int> const& torrentIds, bool deleteFiles = false);
    void verifyTorrents(QSet<int> const& torrentIds);
    void reannounceTorrents(QSet<int> const& torrentIds);
    void launchWebInterface();
    void updatePref(int key);

    /** request a refresh for statistics, including the ones only used by the properties dialog, for a specific torrent */
    void refreshExtraStats(QSet<int> const& ids);

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
    void sendTorrentRequest(char const* request, QSet<int> const& torrentIds);
    void refreshTorrents(QSet<int> const& torrentIds, Torrent::KeyList const& keys);

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
