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

#include "utils.h"

/***
****
***/

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

        fileIcons[DISK]= QIcon::fromTheme( "media-optical", fallback );
        suffixes[DISK] << "iso";

        fileIcons[DOCUMENT] = QIcon::fromTheme( "text-x-generic", fallback );
        suffixes[DOCUMENT] << "abw" << "csv" << "doc" << "dvi" << "htm" << "html" << "ini" << "log"
                           << "odp" << "ods" << "odt" << "pdf" << "ppt" << "ps" << "rtf" << "tex"
                           << "txt" << "xml";

        fileIcons[PICTURE]  = QIcon::fromTheme( "image-x-generic", fallback );
        suffixes[PICTURE] << "bmp" << "gif" << "jpg" << "jpeg" << "pcx" << "png" << "psd" << "raw"
                          << "tga" << "tiff";

        fileIcons[VIDEO] = QIcon::fromTheme( "video-x-generic", fallback );
        suffixes[VIDEO] << "3gp" << "asf" << "avi" << "mov" << "mpeg" << "mpg" << "mp4" << "mkv"
                        << "mov" << "ogm" << "ogv" << "qt" << "rm" << "wmv";

        fileIcons[ARCHIVE]  = QIcon::fromTheme( "package-x-generic", fallback );
        suffixes[ARCHIVE] << "7z" << "ace" << "bz2" << "cbz" << "gz" << "gzip" << "lzma" << "rar"
                          << "sft" << "tar" << "zip";

        fileIcons[AUDIO] = QIcon::fromTheme( "audio-x-generic", fallback );
        suffixes[AUDIO] << "aac" << "ac3" << "aiff" << "ape" << "au" << "flac" << "m3u" << "m4a"
                        << "mid" << "midi" << "mp2" << "mp3" << "mpc" << "nsf" << "oga" << "ogg"
                        << "ra" << "ram" << "shn" << "voc" << "wav" << "wma";

        fileIcons[APP] = QIcon::fromTheme( "application-x-executable", fallback );
        suffixes[APP] << "bat" << "cmd" << "com" << "exe";
    }

    QString suffix( QFileInfo( filename ).suffix( ).toLower( ) );

    for( int i=0; i<TYPE_COUNT; ++i )
        if( suffixes[i].contains( suffix ) )
            return fileIcons[i];

    return fallback;
}
