/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_formatter

#include "Formatter.h"
#include "Speed.h"

/***
****  Constants
***/

namespace
{

enum Units { KILO = 1000, KIBI = 1024 };

auto constexpr SpeedBaseline = KILO;
auto constexpr SizeBaseline = KILO;
auto constexpr MemBaseline = KIBI;

} // namespace

QString Formatter::unit_strings[3][5];

void Formatter::initUnits()
{
    unit_strings[SPEED][B] = tr("B/s");
    unit_strings[SPEED][KB] = tr("kB/s");
    unit_strings[SPEED][MB] = tr("MB/s");
    unit_strings[SPEED][GB] = tr("GB/s");
    unit_strings[SPEED][TB] = tr("TB/s");
    tr_formatter_speed_init(SpeedBaseline, unit_strings[SPEED][KB].toUtf8().constData(),
        unit_strings[SPEED][MB].toUtf8().constData(),
        unit_strings[SPEED][GB].toUtf8().constData(), unit_strings[SPEED][TB].toUtf8().constData());

    unit_strings[SIZE][B] = tr("B");
    unit_strings[SIZE][KB] = tr("kB");
    unit_strings[SIZE][MB] = tr("MB");
    unit_strings[SIZE][GB] = tr("GB");
    unit_strings[SIZE][TB] = tr("TB");
    tr_formatter_size_init(SizeBaseline, unit_strings[SIZE][KB].toUtf8().constData(), unit_strings[SIZE][MB].toUtf8().constData(),
        unit_strings[SIZE][GB].toUtf8().constData(), unit_strings[SIZE][TB].toUtf8().constData());

    unit_strings[MEM][B] = tr("B");
    unit_strings[MEM][KB] = tr("KiB");
    unit_strings[MEM][MB] = tr("MiB");
    unit_strings[MEM][GB] = tr("GiB");
    unit_strings[MEM][TB] = tr("TiB");
    tr_formatter_mem_init(MemBaseline, unit_strings[MEM][KB].toUtf8().constData(), unit_strings[MEM][MB].toUtf8().constData(),
        unit_strings[MEM][GB].toUtf8().constData(), unit_strings[MEM][TB].toUtf8().constData());
}

/***
****
***/

double Speed::getKBps() const
{
    return getBps() / static_cast<double>(SpeedBaseline);
}

Speed Speed::fromKBps(double KBps)
{
    return Speed { static_cast<int>(KBps * SpeedBaseline) };
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

QString Formatter::speedToString(Speed const& speed)
{
    char buf[128];
    tr_formatter_speed_KBps(buf, speed.getKBps(), sizeof(buf));
    return QString::fromUtf8(buf);
}

QString Formatter::uploadSpeedToString(Speed const& upload_speed)
{
    static QChar constexpr UploadSymbol(0x25B4);

    return tr("%1 %2").arg(speedToString(upload_speed)).arg(UploadSymbol);
}

QString Formatter::downloadSpeedToString(Speed const& download_speed)
{
    static QChar constexpr DownloadSymbol(0x25BE);

    return tr("%1 %2").arg(speedToString(download_speed)).arg(DownloadSymbol);
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
