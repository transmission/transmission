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

#ifndef QTR_UNITS
#define QTR_UNITS

#include <QString>
#include <QObject>
#include <QIcon>

class Speed;

class Units: public QObject
{
        Q_OBJECT

    public:

        Units( ) { }
        virtual ~Units( ) { }

    public:

        static QString memToString( double bytes );
        static QString sizeToString( double bytes );
        static QString speedToString( const Speed& speed );
        static QString percentToString( double x );
        static QString ratioToString( double ratio );
        static QString timeToString( int seconds );

    public:

        static const int speed_K;
        static const QString speed_B_str;
        static const QString speed_K_str;
        static const QString speed_M_str;
        static const QString speed_G_str;

        static const int size_K;
        static const QString size_B_str;
        static const QString size_K_str;
        static const QString size_M_str;
        static const QString size_G_str;

        static const int mem_K;
        static const QString mem_B_str;
        static const QString mem_K_str;
        static const QString mem_M_str;
        static const QString mem_G_str;
};

#endif
