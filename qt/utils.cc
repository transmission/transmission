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

#include <iostream>

#include <QApplication>
#include <QDataStream>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QObject>
#include <QSet>
#include <QStyle>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_formatter

#include "qticonloader.h"
#include "utils.h"

QString
Utils :: remoteFileChooser( QWidget * parent, const QString& title, const QString& myPath, bool dir, bool local )
{
    QString path;

    if( local )
    {
        if( dir )
            path = QFileDialog::getExistingDirectory( parent, title, myPath );
        else
            path = QFileDialog::getOpenFileName( parent, title, myPath );
    }
    else
        path = QInputDialog::getText( parent, title, tr( "Enter a location:" ), QLineEdit::Normal, myPath, NULL );

    return path;
}

QString
Utils :: sizeToString( double  bytes )
{
    if( !bytes )
        return tr( "None" );
    else {
        char buf[128];
        tr_formatter_size( buf, bytes, sizeof( buf ) );
        return buf;
    }
}

QString
Utils :: speedToString( const Speed& speed )
{
    if( speed.isZero( ) )
        return tr( "None" );
    else {
        char buf[128];
        tr_formatter_speed( buf, speed.bps( ), sizeof( buf ) );
        return buf;
    }
}

QString
Utils :: ratioToString( double ratio )
{
    QString buf;

    if( (int)ratio == TR_RATIO_NA )
        buf = tr( "None" );
    else if( (int)ratio == TR_RATIO_INF )
        buf = QString::fromUtf8( "\xE2\x88\x9E" );
    else
    {
        QStringList temp;

        temp = QString().sprintf( "%f", ratio ).split( "." );
        if( ratio < 100.0 )
        {
            if( ratio < 10.0 )
                temp[1].truncate( 2 );
            else
                temp[1].truncate( 1 );
            buf = temp.join( "." );
        }
        else
            buf = QString( temp[0] );
    }

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

void
Utils :: toStderr( const QString& str )
{
    std::cerr << qPrintable(str) << std::endl;
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
        suffixes[DOCUMENT] << "abw" << "csv" << "doc" << "dvi" << "htm" << "html" << "ini" << "log"
                           << "odp" << "ods" << "odt" << "pdf" << "ppt" << "ps" << "rtf" << "tex"
                           << "txt" << "xml";

        fileIcons[PICTURE]  = QtIconLoader :: icon( "image-x-generic", fallback );
        suffixes[PICTURE] << "bmp" << "gif" << "jpg" << "jpeg" << "pcx" << "png" << "psd" << "raw"
                          << "tga" << "tiff";

        fileIcons[VIDEO] = QtIconLoader :: icon( "video-x-generic", fallback );
        suffixes[VIDEO] << "3gp" << "asf" << "avi" << "mov" << "mpeg" << "mpg" << "mp4" << "mkv"
                        << "mov" << "ogm" << "ogv" << "qt" << "rm" << "wmv";

        fileIcons[ARCHIVE]  = QtIconLoader :: icon( "package-x-generic", fallback );
        suffixes[ARCHIVE] << "7z" << "ace" << "bz2" << "cbz" << "gz" << "gzip" << "lzma" << "rar"
                          << "sft" << "tar" << "zip";

        fileIcons[AUDIO] = QtIconLoader :: icon( "audio-x-generic", fallback );
        suffixes[AUDIO] << "aac" << "ac3" << "aiff" << "ape" << "au" << "flac" << "m3u" << "m4a"
                        << "mid" << "midi" << "mp2" << "mp3" << "mpc" << "nsf" << "oga" << "ogg"
                        << "ra" << "ram" << "shn" << "voc" << "wav" << "wma";

        fileIcons[APP] = QtIconLoader :: icon( "application-x-executable", fallback );
        suffixes[APP] << "bat" << "cmd" << "com" << "exe";
    }

    QString suffix( QFileInfo( filename ).suffix( ).toLower( ) );

    for( int i=0; i<TYPE_COUNT; ++i )
        if( suffixes[i].contains( suffix ) )
            return fileIcons[i];

    return fallback;
}
