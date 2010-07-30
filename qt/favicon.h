/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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
