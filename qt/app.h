/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef QTR_APP_H
#define QTR_APP_H

#include <QApplication>
#include <QSet>
#include <QTimer>

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
        MyApp( int& argc, char ** argv );
        virtual ~MyApp( );

    public:
        void raise( );
        bool notify( const QString& title, const QString& body ) const;

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

    private slots:
        void consentGiven( );
        void onSessionSourceChanged( );
        void refreshPref( int key );
        void refreshTorrents( );
        void torrentsAdded( QSet<int> );
        void torrentChanged( int );

    public slots:
        void addTorrent( const QString& );
        void addTorrent( const AddData& );

    private:
        void maybeUpdateBlocklist( );
};

#endif
