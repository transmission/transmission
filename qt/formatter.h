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

class Formatter: public QObject
{
        Q_OBJECT

    public:

        Formatter( ) { }
        virtual ~Formatter( ) { }

    public:

        static QString memToString( double bytes );
        static QString sizeToString( double bytes );
        static QString speedToString( const Speed& speed );
        static QString percentToString( double x );
        static QString ratioToString( double ratio );
        static QString timeToString( int seconds );

    public:

        typedef enum { B, KB, MB, GB, TB } Size;
        typedef enum { SPEED, SIZE, MEM } Type;
        static QString unitStr( Type t, Size s ) { return unitStrings[t][s]; }
        static void initUnits( );

    private:

        static QString unitStrings[3][4];
};

#endif
