/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef TR_APP_SESSION_H
#define TR_APP_SESSION_H

#include <QObject>
#include <QSet>
#include <QBuffer>
#include <QFileInfoList>
#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QNetworkReply>

class QStringList;

class AddData;

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

extern "C"
{
  struct evbuffer;
  struct tr_variant;
}

class Prefs;

class FileAdded: public QObject
{
    Q_OBJECT

  public:
    FileAdded (int tag, const QString& name): myTag (tag), myName (name) {}
    ~FileAdded () {}
    void setFileToDelete (const QString& file) { myDelFile = file; }

  public slots:
    void executed (int64_t tag, const QString& result, tr_variant * arguments);

  private:
    const int64_t myTag;
    const QString myName;
    QString myDelFile;
};

class Session: public QObject
{
    Q_OBJECT

  public:
    Session (const char * configDir, Prefs& prefs);
    ~Session ();

  public:
    void stop ();
    void restart ();

  private:
    void start ();

  public:
    const QUrl& getRemoteUrl () const { return myUrl; }
    const tr_session_stats& getStats () const { return myStats; }
    const tr_session_stats& getCumulativeStats () const { return myCumulativeStats; }
    const QString& sessionVersion () const { return mySessionVersion; }

  public:
    int64_t blocklistSize () const { return myBlocklistSize; }
    void setBlocklistSize (int64_t i);
    void updateBlocklist ();
    void portTest ();
    void copyMagnetLinkToClipboard (int torrentId);

  public:

    /** returns true if the transmission session is being run inside this client */
    bool isServer () const;

    /** returns true if isServer () is true or if the remote address is the localhost */
    bool isLocal () const;

  private:
    void updateStats (tr_variant * args);
    void updateInfo (tr_variant * args);
    void parseResponse (const char * json, size_t len);
    static void localSessionCallback (tr_session *, evbuffer *, void *);

  public:
    void exec (const char * json);
    void exec (const tr_variant * request);

  public:
    int64_t getUniqueTag () { return nextUniqueTag++; }

  private:
    void sessionSet (const tr_quark key, const QVariant& variant);
    void pumpRequests ();
    void sendTorrentRequest (const char * request, const QSet<int>& torrentIds);
    static void updateStats (tr_variant * d, tr_session_stats * stats);
    void refreshTorrents (const QSet<int>& torrentIds);
    QNetworkAccessManager * networkAccessManager ();

  public:
    void torrentSet (const QSet<int>& ids, const tr_quark key, bool val);
    void torrentSet (const QSet<int>& ids, const tr_quark key, int val);
    void torrentSet (const QSet<int>& ids, const tr_quark key, double val);
    void torrentSet (const QSet<int>& ids, const tr_quark key, const QList<int>& val);
    void torrentSet (const QSet<int>& ids, const tr_quark key, const QStringList& val);
    void torrentSet (const QSet<int>& ids, const tr_quark key, const QPair<int,QString>& val);
    void torrentSetLocation (const QSet<int>& ids, const QString& path, bool doMove);
    void torrentRenamePath (const QSet<int>& ids, const QString& oldpath, const QString& newname);
    void addTorrent (const AddData& addme, tr_variant& top, bool trashOriginal);

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
    void removeTorrents (const QSet<int>& torrentIds, bool deleteFiles=false);
    void verifyTorrents (const QSet<int>& torrentIds);
    void reannounceTorrents (const QSet<int>& torrentIds);
    void launchWebInterface ();
    void updatePref (int key);

    /** request a refresh for statistics, including the ones only used by the properties dialog, for a specific torrent */
    void refreshExtraStats (const QSet<int>& ids);

  private slots:
    void onFinished (QNetworkReply * reply);
    void onResponseReceived (const QByteArray& json);

  signals:
    void responseReceived (const QByteArray& json);
    void executed (int64_t tag, const QString& result, tr_variant * arguments);
    void sourceChanged ();
    void portTested (bool isOpen);
    void statsUpdated ();
    void sessionUpdated ();
    void blocklistUpdated (int);
    void torrentsUpdated (tr_variant * torrentList, bool completeList);
    void torrentsRemoved (tr_variant * torrentList);
    void dataReadProgress ();
    void dataSendProgress ();
    void error (QNetworkReply::NetworkError);
    void errorMessage (const QString);
    void httpAuthenticationRequired ();

  private:
    int64_t nextUniqueTag;
    int64_t myBlocklistSize;
    Prefs& myPrefs;
    tr_session * mySession;
    QString myConfigDir;
    QString mySessionId;
    QStringList myIdleJSON;
    QUrl myUrl;
    QNetworkAccessManager * myNAM;
    tr_session_stats myStats;
    tr_session_stats myCumulativeStats;
    QString mySessionVersion;
};

#endif

