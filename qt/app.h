/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
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
        QTranslator qtTranslator;
        QTranslator appTranslator;

    private slots:
        void consentGiven( );
        void onSessionSourceChanged( );
        void refreshPref( int key );
        void refreshTorrents( );
        void onTorrentsAdded( QSet<int> );
        void onTorrentCompleted( int );
        void onNewTorrentChanged( int );

    public slots:
        void addTorrent( const QString& );
        void addTorrent( const AddData& );

    private:
        void maybeUpdateBlocklist( );
};

#endif
