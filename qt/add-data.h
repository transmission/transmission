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

#ifndef ADD_DATA_H
#define ADD_DATA_H

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QString>
#include <QUrl>

class AddData
{
    public:

        enum { NONE, MAGNET, URL, FILENAME, METAINFO };
        int type;

        QByteArray metainfo;
        QString filename;
        QString magnet;
        QUrl url;

    public:

        int set( const QString& );
        AddData( const QString& str ) { set(str); }
        AddData( ): type(NONE) { }

        QByteArray toBase64( ) const;

        QString readableName( ) const;

    public:

        static bool isSupported( const QString& str ) { return AddData(str).type != NONE; }
};

#endif
