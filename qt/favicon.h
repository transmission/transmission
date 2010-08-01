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

#ifndef FAVICON_CACHE_H
#define FAVICON_CACHE_H

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

#include <QMap>
#include <QString>
#include <QObject>
#include <QPixmap>

class Favicons: public QObject
{
        Q_OBJECT;

    public:

        static QString getHost( const QUrl& url );

    public:

        Favicons();
        virtual ~Favicons();

        /* returns a cached pixmap, or a NULL pixmap if there's no match in the cache */
        QPixmap find( const QUrl& url );

        /* returns a cached pixmap, or a NULL pixmap if there's no match in the cache */
        QPixmap findFromHost( const QString& host );

        /* this will emit a signal when (if) the icon becomes ready */
        void add( const QUrl& url );

    signals:

        void pixmapReady( const QString& host );

    private:

        QNetworkAccessManager * myNAM;
        QMap<QString,QPixmap> myPixmaps;

        QString getCacheDir( );
        void ensureCacheDirHasBeenScanned( );

    private slots:

        void onRequestFinished( QNetworkReply * reply );
};

#endif
