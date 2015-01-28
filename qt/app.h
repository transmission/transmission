/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_APP_H
#define QTR_APP_H

#include <QApplication>
#include <QSet>
#include <QTimer>
#include <QTranslator>

#include "favicon.h"

class AddData;
class Prefs;
class Session;
class TorrentModel;
class TrMainWindow;
class WatchDir;

class MyApp: public QApplication
{
    Q_OBJECT

  public:
    MyApp (int& argc, char ** argv);
    virtual ~MyApp ();

  public:
    void raise ();
    bool notifyApp (const QString& title, const QString& body) const;

  public:
    Favicons favicons;

  private:
    Prefs * myPrefs;
    Session * mySession;
    TorrentModel * myModel;
    TrMainWindow * myWindow;
    WatchDir * myWatchDir;
    QTimer myModelTimer;
    QTimer myStatsTimer;
    QTimer mySessionTimer;
    time_t myLastFullUpdateTime;
    QTranslator qtTranslator;
    QTranslator appTranslator;

  private slots:
    void consentGiven (int result);
    void onSessionSourceChanged ();
    void refreshPref (int key);
    void refreshTorrents ();
    void onTorrentsAdded (const QSet<int>& torrents);
    void onTorrentCompleted (int);
    void onNewTorrentChanged (int);

  public slots:
    void addTorrent (const QString&);
    void addTorrent (const AddData&);

  private:
    void maybeUpdateBlocklist ();

    void quitLater ();
};

#undef qApp
#define qApp static_cast<MyApp*> (MyApp::instance ())

#endif
