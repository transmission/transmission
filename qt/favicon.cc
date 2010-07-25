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

#include <iostream>

#include <QDesktopServices>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "favicon.h"

/***
****
***/

QString
Favicons :: getCacheDir( )
{
    const QString base = QDesktopServices::storageLocation( QDesktopServices::CacheLocation );
    return QDir( base ).absoluteFilePath( "favicons" );
};


Favicons :: Favicons( )
{
    myNAM = new QNetworkAccessManager( );
    connect( myNAM, SIGNAL(finished(QNetworkReply*)), this, SLOT(onRequestFinished(QNetworkReply*)) );
}

Favicons :: ~Favicons( )
{
    delete myNAM;
}

/***
****
***/

void
Favicons :: ensureCacheDirHasBeenScanned( )
{
    static bool hasBeenScanned = false;

    if( !hasBeenScanned )
    {
        hasBeenScanned = true;
   
        QDir cacheDir( getCacheDir( ) );
        cacheDir.mkpath( cacheDir.absolutePath( ) );
        QStringList files = cacheDir.entryList( QDir::Files|QDir::Readable );
        foreach( QString file, files ) {
            QPixmap pixmap;
            pixmap.load( cacheDir.absoluteFilePath( file ) );
            if( !pixmap.isNull( ) )
                myPixmaps.insert( file, pixmap );
        }
    }
}

QString
Favicons :: getHost( const QUrl& url )
{
    QString host = url.host( );
    const int first_dot = host.indexOf( '.' );
    const int last_dot = host.lastIndexOf( '.' );

    if( ( first_dot != -1 ) && ( last_dot != -1 ) && ( first_dot != last_dot ) )
        host.remove( 0, first_dot + 1 );

    return host;
}

QPixmap
Favicons :: find( const QUrl& url )
{
    ensureCacheDirHasBeenScanned( );

    return myPixmaps[ getHost(url) ];
}

void
Favicons :: add( const QUrl& url_in )
{
    ensureCacheDirHasBeenScanned( );

    const QString host = getHost(url_in);
    if( !myPixmaps.contains(host) && !myPending.contains(host) )
    {
        const int IMAGE_TYPES = 4;
        const QString image_types[IMAGE_TYPES] = { "ico", "png", "gif", "jpg" };

        myPending.append( host ); 
        for( int i=0; i<IMAGE_TYPES; ++i )
        {
            QString url( "http://" + host + "/favicon." + image_types[i]);
            myNAM->get( QNetworkRequest( url ) );
        }
    }
}

void
Favicons :: onRequestFinished( QNetworkReply * reply )
{
    const QString host = reply->url().host();

    myPending.removeAll( host );

    const QByteArray content = reply->readAll( );

    QPixmap pixmap;

    if( !reply->error( ) )
        pixmap.loadFromData( content );

    if( !pixmap.isNull( ) )
    {
        // save it in memory...
        myPixmaps.insert( host, pixmap );

        // save it on disk...
        QDir cacheDir( getCacheDir( ) );
        cacheDir.mkpath( cacheDir.absolutePath( ) );
        QFile file( cacheDir.absoluteFilePath( host ) );
        file.open( QIODevice::WriteOnly );
        file.write( content );
        file.close( );

        // notify listeners
        emit pixmapReady( host );
    }
}
