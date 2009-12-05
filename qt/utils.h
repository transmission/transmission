/*
 * This file Copyright (C) 2009 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef QTR_UTILS
#define QTR_UTILS

#include <QString>
#include <QObject>
#include <QIcon>

#include "speed.h"

class Utils: public QObject
{
        Q_OBJECT

    public:
        Utils( ) { }
        virtual ~Utils( ) { }
    public:

        static QString sizeToString( double size );
        static QString speedToString( const Speed& speed );
        static QString ratioToString( double ratio );
        static QString timeToString( int seconds );
        static const QIcon& guessMimeIcon( const QString& filename );

        // meh
        static void toStderr( const QString& qstr );

};

#endif
