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

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_formatter

#include "formatter.h"
#include "speed.h"

/***
****  Constants
***/

const int     Formatter :: speed_K     = 1000;
const QString Formatter :: speed_B_str = "B/s";
const QString Formatter :: speed_K_str = "kB/s";
const QString Formatter :: speed_M_str = "MB/s";
const QString Formatter :: speed_G_str = "GB/s";

const int     Formatter :: size_K     = 1000;
const QString Formatter :: size_B_str = "B";
const QString Formatter :: size_K_str = "kB";
const QString Formatter :: size_M_str = "MB";
const QString Formatter :: size_G_str = "GB";

const int     Formatter :: mem_K     = 1024;
const QString Formatter :: mem_B_str = "B";
const QString Formatter :: mem_K_str = "KiB";
const QString Formatter :: mem_M_str = "MiB";
const QString Formatter :: mem_G_str = "GiB";

/***
****
***/

Speed
Speed :: fromKBps( double KBps )
{
    return KBps * Formatter::speed_K;
}

/***
****
***/

QString
Formatter :: memToString( double bytes )
{
    if( !bytes )
        return tr( "None" );
    else {
        char buf[128];
        tr_formatter_mem_B( buf, bytes, sizeof( buf ) );
        return buf;
    }
}

QString
Formatter :: sizeToString( double bytes )
{
    if( !bytes )
        return tr( "None" );
    else {
        char buf[128];
        tr_formatter_size_B( buf, bytes, sizeof( buf ) );
        return buf;
    }
}

QString
Formatter :: speedToString( const Speed& speed )
{
    if( speed.isZero( ) )
        return tr( "None" );
    else {
        char buf[128];
        tr_formatter_speed_KBps( buf, speed.Bps( ), sizeof( buf ) );
        return buf;
    }
}

QString
Formatter :: percentToString( double x )
{
    char buf[128];
    return QString( tr_strpercent( buf, x, sizeof(buf) ) );
}

QString
Formatter :: ratioToString( double ratio )
{
    char buf[128];
    return QString::fromUtf8( tr_strratio( buf, sizeof(buf), ratio, "\xE2\x88\x9E" ) );
}

QString
Formatter :: timeToString( int seconds )
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
