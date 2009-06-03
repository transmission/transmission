/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
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

#include <QApplication>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QStyle>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include "qticonloader.h"
#include "utils.h"

#define KILOBYTE_FACTOR 1024.0
#define MEGABYTE_FACTOR ( 1024.0 * 1024.0 )
#define GIGABYTE_FACTOR ( 1024.0 * 1024.0 * 1024.0 )

QString
Utils :: sizeToString( double size )
{
    QString str;

    if( !size )
    {
        str = tr( "None" );
    }
    else if( size < 1024.0 )
    {
        const int i = (int)size;
        str = tr( "%Ln byte(s)", 0, i );
    }
    else
    {
        double displayed_size;

        if( size < (int64_t)MEGABYTE_FACTOR )
        {
            displayed_size = (double)size / KILOBYTE_FACTOR;
            str = tr( "%L1 KB" ).arg( displayed_size,  0, 'f', 1 );
        }
        else if( size < (int64_t)GIGABYTE_FACTOR )
        {
            displayed_size = (double)size / MEGABYTE_FACTOR;
            str = tr( "%L1 MB" ).arg( displayed_size,  0, 'f', 1 );
        }
        else
        {
            displayed_size = (double) size / GIGABYTE_FACTOR;
            str = tr( "%L1 GB" ).arg( displayed_size,  0, 'f', 1 );
        }
    }

    return str;
}

QString
Utils :: ratioToString( double ratio )
{
    QString buf;

    if( (int)ratio == TR_RATIO_NA )
        buf = tr( "None" );
    else if( (int)ratio == TR_RATIO_INF )
        buf = QString::fromUtf8( "\xE2\x88\x9E" );
    else if( ratio < 10.0 )
        buf.sprintf( "%'.2f", ratio );
    else if( ratio < 100.0 )
        buf.sprintf( "%'.1f", ratio );
    else
        buf.sprintf( "%'.0f", ratio );

    return buf;

}

QString
Utils :: timeToString( int seconds )
{
    int days, hours, minutes;
    QString d, h, m, s;
    QString str;

    if( seconds < 0 )
        seconds = 0;

    days = seconds / 86400;
    hours = ( seconds % 86400 ) / 3600;
    minutes = ( seconds % 3600 ) / 60;
    seconds %= 60;

    d = tr( "%Ln day(s)", 0, days );
    h = tr( "%Ln hour(s)", 0, hours );
    m = tr( "%Ln minute(s)", 0, minutes );
    s = tr( "%Ln second(s)", 0, seconds );

    if( days )
    {
        if( days >= 4 || !hours )
            str = d;
        else
            str = tr( "%1, %2" ).arg( d ).arg( h );
    }
    else if( hours )
    {
        if( hours >= 4 || !minutes )
            str = h;
        else
            str = tr( "%1, %2" ).arg( h ).arg( m );
    }
    else if( minutes )
    {
        if( minutes >= 4 || !seconds )
            str = m;
        else
            str = tr( "%1, %2" ).arg( m ).arg( s );
    }
    else
    {
        str = s;
    }

    return str;
}

QString
Utils :: speedToString( const Speed& speed )
{
    const double kbps( speed.kbps( ) );
    QString str;

    if( kbps < 1000.0 )  /* 0.0 KB to 999.9 KB */
        str = tr( "%L1 KB/s" ).arg( kbps, 0, 'f', 1 );
    else if( kbps < 102400.0 ) /* 0.98 MB to 99.99 MB */
        str = tr( "%L1 MB/s" ).arg( kbps/1024.0, 0, 'f', 2 );
    else // insane speeds
        str = tr( "%L1 GB/s" ).arg( kbps/(1024.0*1024.0), 0, 'f', 1 );

    return str;
}

void
Utils :: toStderr( const QString& str )
{
    std::cerr << str.toLatin1().constData() << std::endl;
}

const QIcon&
Utils :: guessMimeIcon( const QString& filename )
{
    enum { DISK, DOCUMENT, PICTURE, VIDEO, ARCHIVE, AUDIO, APP, TYPE_COUNT };
    static QIcon fallback;
    static QIcon fileIcons[TYPE_COUNT];
    static QSet<QString> suffixes[TYPE_COUNT];

    if( fileIcons[0].isNull( ) )
    {
        fallback = QApplication::style()->standardIcon( QStyle :: SP_FileIcon );

        fileIcons[DISK]= QtIconLoader :: icon( "media-optical", fallback );
        suffixes[DISK] << "iso";

        fileIcons[DOCUMENT] = QtIconLoader :: icon( "text-x-generic", fallback );
        suffixes[DOCUMENT] << "txt" << "doc" << "pdf" << "rtf" << "htm" << "html";

        fileIcons[PICTURE]  = QtIconLoader :: icon( "image-x-generic", fallback );
        suffixes[PICTURE] << "jpg" << "jpeg" << "png" << "gif" << "tiff" << "pcx";

        fileIcons[VIDEO] = QtIconLoader :: icon( "video-x-generic", fallback );
        suffixes[VIDEO] << "avi" << "mpeg" << "mp4" << "mkv" << "mov";

        fileIcons[ARCHIVE]  = QtIconLoader :: icon( "package-x-generic", fallback );
        suffixes[ARCHIVE] << "rar" << "zip" << "sft" << "tar" << "7z" << "cbz";

        fileIcons[AUDIO] = QtIconLoader :: icon( "audio-x-generic", fallback );
        suffixes[AUDIO] << "aiff" << "au" << "m3u" << "mp2" << "wav" << "mp3" << "ape" << "mid"
                        << "aac" << "ogg" << "ra" << "ram" << "flac" << "mpc" << "shn";

        fileIcons[APP] = QtIconLoader :: icon( "application-x-executable", fallback );
        suffixes[APP] << "exe";
    }

    QString suffix( QFileInfo( filename ).suffix( ).toLower( ) );

    for( int i=0; i<TYPE_COUNT; ++i )
        if( suffixes[i].contains( suffix ) )
            return fileIcons[i];

    return fallback;
}
