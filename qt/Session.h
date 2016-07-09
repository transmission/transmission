/*
 * This file Copyright (C) 2009-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
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

class Session: public QObject
{
    Q_OBJECT

  public:
    Session (const QString& configDir, Prefs& prefs);
    virtual ~Session ();

    void stop ();
    void restart ();

    const QUrl& getRemoteUrl () const { return myRpc.url (); }
    const tr_session_stats& getStats () const { return myStats; }
    const tr_session_stats& getCumulativeStats () const { return myCumulativeStats; }
    const QString& sessionVersion () const { return mySessionVersion; }

    int64_t blocklistSize () const { return myBlocklistSize; }
    void setBlocklistSize (int64_t i);
    void updateBlocklist ();
    void portTest ();
    void copyMagnetLinkToClipboard (int torrentId);

    /** returns true if the transmission session is being run inside this client */
    bool isServer () const;

    /** returns true if isServer () is true or if the remote address is the localhost */
    bool isLocal () const;

    RpcResponseFuture exec (tr_quark method, tr_variant * args);
    RpcResponseFuture exec (const char * method, tr_variant * args);

    void torrentSet (const QSet<int>& ids, const tr_quark key, bool val);
    void torrentSet (const QSet<int>& ids, const tr_quark key, int val);
    void torrentSet (const QSet<int>& ids, const tr_quark key, double val);
    void torrentSet (const QSet<int>& ids, const tr_quark key, const QList<int>& val);
    void torrentSet (const QSet<int>& ids, const tr_quark key, const QStringList& val);
    void torrentSet (const QSet<int>& ids, const tr_quark key, const QPair<int,QString>& val);
    void torrentSetLocation (const QSet<int>& ids, const QString& path, bool doMove);
    void torrentRenamePath (const QSet<int>& ids, const QString& oldpath, const QString& newname);
    void addTorrent (const AddData& addme, tr_variant * top, bool trashOriginal);

  public slots:
    void pauseTorrents (const QSet<int>& torrentIds = QSet<int> ());
    void startTorrents (const QSet<int>& torrentIds = QSet<int> ());
    void startTorrentsNow (const QSet<int>& torrentIds = QSet<int> ());
    void queueMoveTop (const QSet<int>& torrentIds = QSet<int> ());
    void queueMoveUp (const QSet<int>& torrentIds = QSet<int> ());
    void queueMoveDown (const QSet<int>& torrentIds = QSet<int> ());
    void queueMoveBottom (const QSet<int>& torrentIds = QSet<int> ());
    void refreshSessionInfo ();
    void refreshSessionStats ();
    void refreshActiveTorrents ();
    void refreshAllTorrents ();
    void initTorrents (const QSet<int>& ids = QSet<int> ());
    void addNewlyCreatedTorrent (const QString& filename, const QString& localPath);
    void addTorrent (const AddData& addme);
    void removeTorrents (const QSet<int>& torrentIds, bool deleteFiles = false);
    void verifyTorrents (const QSet<int>& torrentIds);
    void reannounceTorrents (const QSet<int>& torrentIds);
    void launchWebInterface ();
    void updatePref (int key);
  
    /** request a refresh for statistics, including the ones only used by the properties dialog, for a specific torrent */
    void refreshExtraStats (const QSet<int>& ids);

  signals:
    void sourceChanged ();
    void portTested (bool isOpen);
    void statsUpdated ();
    void sessionUpdated ();
    void blocklistUpdated (int);
    void torrentsUpdated (tr_variant * torrentList, bool completeList);
    void torrentsRemoved (tr_variant * torrentList);
    void dataReadProgress ();
    void dataSendProgress ();
    void networkResponse (QNetworkReply::NetworkError code, const QString& message);
    void httpAuthenticationRequired ();

  private:
    void start ();

    void updateStats (tr_variant * args);
    void updateInfo (tr_variant * args);

    void sessionSet (const tr_quark key, const QVariant& variant);
    void pumpRequests ();
    void sendTorrentRequest (const char * request, const QSet<int>& torrentIds);
    void refreshTorrents (const QSet<int>& torrentIds, const Torrent::KeyList& keys);

    static void updateStats (tr_variant * d, tr_session_stats * stats);

  private:
    QString const myConfigDir;
    Prefs& myPrefs;

    int64_t myBlocklistSize;
    tr_session * mySession;
    QStringList myIdleJSON;
    tr_session_stats myStats;
    tr_session_stats myCumulativeStats;
    QString mySessionVersion;
    RpcClient myRpc;
};

