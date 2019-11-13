/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QStringBuilder>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_formatter

#include "Formatter.h"

/***
****  Constants
***/

namespace
{

unsigned int speed_K;
unsigned int mem_K;
unsigned int size_K;

} // namespace

QString Formatter::unitStrings[3][5];

void Formatter::initUnits()
{
    speed_K = 1000;
    unitStrings[SPEED][B] = tr("B/s");
    unitStrings[SPEED][KB] = tr("kB/s");
    unitStrings[SPEED][MB] = tr("MB/s");
    unitStrings[SPEED][GB] = tr("GB/s");
    unitStrings[SPEED][TB] = tr("TB/s");
    tr_formatter_speed_init(speed_K, unitStrings[SPEED][KB].toUtf8().constData(), unitStrings[SPEED][MB].toUtf8().constData(),
        unitStrings[SPEED][GB].toUtf8().constData(), unitStrings[SPEED][TB].toUtf8().constData());

    size_K = 1000;
    unitStrings[SIZE][B] = tr("B");
    unitStrings[SIZE][KB] = tr("kB");
    unitStrings[SIZE][MB] = tr("MB");
    unitStrings[SIZE][GB] = tr("GB");
    unitStrings[SIZE][TB] = tr("TB");
    tr_formatter_size_init(size_K, unitStrings[SIZE][KB].toUtf8().constData(), unitStrings[SIZE][MB].toUtf8().constData(),
        unitStrings[SIZE][GB].toUtf8().constData(), unitStrings[SIZE][TB].toUtf8().constData());

    mem_K = 1024;
    unitStrings[MEM][B] = tr("B");
    unitStrings[MEM][KB] = tr("KiB");
    unitStrings[MEM][MB] = tr("MiB");
    unitStrings[MEM][GB] = tr("GiB");
    unitStrings[MEM][TB] = tr("TiB");
    tr_formatter_mem_init(mem_K, unitStrings[MEM][KB].toUtf8().constData(), unitStrings[MEM][MB].toUtf8().constData(),
        unitStrings[MEM][GB].toUtf8().constData(), unitStrings[MEM][TB].toUtf8().constData());
}

/***
****
***/

QString Formatter::memToString(int64_t bytes)
{
    if (bytes < 0)
    {
        return tr("Unknown");
    }

    if (bytes == 0)
    {
        return tr("None");
    }

    char buf[128];
    tr_formatter_mem_B(buf, bytes, sizeof(buf));
    return QString::fromUtf8(buf);
}

QString Formatter::storage(bytes_t const& val)
{
    return sizeToString(val.value());
}

QString Formatter::sizeToString(int64_t bytes)
{
    if (bytes < 0)
    {
        return tr("Unknown");
    }

    if (bytes == 0)
    {
        return tr("None");
    }

    char buf[128];
    tr_formatter_size_B(buf, bytes, sizeof(buf));
    return QString::fromUtf8(buf);
}

QString Formatter::speed(KBps_t const& val)
{
    char buf[128];
    tr_formatter_speed_KBps(buf, val.value(), sizeof(buf));
    return QString::fromUtf8(buf);
}

QString Formatter::speedUp(KBps_t const& value)
{
    static QChar const uploadSymbol(0x25B4);

    return tr("%1 %2").arg(speed(value)).arg(uploadSymbol);
}

QString Formatter::speedDown(KBps_t const& value)
{
    static QChar const downloadSymbol(0x25BE);

    return tr("%1 %2").arg(speed(value)).arg(downloadSymbol);
}

QString Formatter::percentToString(double x)
{
    char buf[128];
    return QString::fromUtf8(tr_strpercent(buf, x, sizeof(buf)));
}

QString Formatter::ratioToString(double ratio)
{
    char buf[128];
    return QString::fromUtf8(tr_strratio(buf, sizeof(buf), ratio, "\xE2\x88\x9E"));
}

QString Formatter::timeToString(int seconds)
{
    int days;
    int hours;
    int minutes;
    QString d;
    QString h;
    QString m;
    QString s;
    QString str;

    if (seconds < 0)
    {
        seconds = 0;
    }

    days = seconds / 86400;
    hours = (seconds % 86400) / 3600;
    minutes = (seconds % 3600) / 60;
    seconds %= 60;

    d = tr("%Ln day(s)", nullptr, days);
    h = tr("%Ln hour(s)", nullptr, hours);
    m = tr("%Ln minute(s)", nullptr, minutes);
    s = tr("%Ln second(s)", nullptr, seconds);

    if (days != 0)
    {
        if (days >= 4 || hours == 0)
        {
            str = d;
        }
        else
        {
            str = tr("%1, %2").arg(d).arg(h);
        }
    }
    else if (hours != 0)
    {
        if (hours >= 4 || minutes == 0)
        {
            str = h;
        }
        else
        {
            str = tr("%1, %2").arg(h).arg(m);
        }
    }
    else if (minutes != 0)
    {
        if (minutes >= 4 || seconds == 0)
        {
            str = m;
        }
        else
        {
            str = tr("%1, %2").arg(m).arg(s);
        }
    }
    else
    {
        str = s;
    }

    return str;
}
