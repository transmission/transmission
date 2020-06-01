/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <array>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_formatter

#include "Formatter.h"
#include "Speed.h"

/***
****  Constants
***/

namespace
{

auto constexpr SpeedBase = 1000;
auto constexpr SizeBase = 1000;
auto constexpr MemBase = 1024;

} // namespace

std::array<std::array<QString, Formatter::NUM_SIZES>, Formatter::NUM_TYPES> Formatter::unit_strings{};

void Formatter::initUnits()
{
    unit_strings[SPEED][B] = tr("B/s");
    unit_strings[SPEED][KB] = tr("kB/s");
    unit_strings[SPEED][MB] = tr("MB/s");
    unit_strings[SPEED][GB] = tr("GB/s");
    unit_strings[SPEED][TB] = tr("TB/s");
    tr_formatter_speed_init(SpeedBase, unit_strings[SPEED][KB].toUtf8().constData(),
        unit_strings[SPEED][MB].toUtf8().constData(),
        unit_strings[SPEED][GB].toUtf8().constData(), unit_strings[SPEED][TB].toUtf8().constData());

    unit_strings[SIZE][B] = tr("B");
    unit_strings[SIZE][KB] = tr("kB");
    unit_strings[SIZE][MB] = tr("MB");
    unit_strings[SIZE][GB] = tr("GB");
    unit_strings[SIZE][TB] = tr("TB");
    tr_formatter_size_init(SizeBase, unit_strings[SIZE][KB].toUtf8().constData(),
        unit_strings[SIZE][MB].toUtf8().constData(),
        unit_strings[SIZE][GB].toUtf8().constData(), unit_strings[SIZE][TB].toUtf8().constData());

    unit_strings[MEM][B] = tr("B");
    unit_strings[MEM][KB] = tr("KiB");
    unit_strings[MEM][MB] = tr("MiB");
    unit_strings[MEM][GB] = tr("GiB");
    unit_strings[MEM][TB] = tr("TiB");
    tr_formatter_mem_init(MemBase, unit_strings[MEM][KB].toUtf8().constData(), unit_strings[MEM][MB].toUtf8().constData(),
        unit_strings[MEM][GB].toUtf8().constData(), unit_strings[MEM][TB].toUtf8().constData());
}

/***
****
***/

double Speed::getKBps() const
{
    return getBps() / static_cast<double>(SpeedBase);
}

Speed Speed::fromKBps(double KBps)
{
    return Speed{ static_cast<int>(KBps * SpeedBase) };
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

    auto buf = std::array<char, 128>{};
    tr_formatter_mem_B(buf.data(), bytes, buf.size());
    return QString::fromUtf8(buf.data());
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

    auto buf = std::array<char, 128>{};
    tr_formatter_size_B(buf.data(), bytes, buf.size());
    return QString::fromUtf8(buf.data());
}

QString Formatter::speedToString(Speed const& speed)
{
    auto buf = std::array<char, 128>{};
    tr_formatter_speed_KBps(buf.data(), speed.getKBps(), buf.size());
    return QString::fromUtf8(buf.data());
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
    auto buf = std::array<char, 128>{};
    return QString::fromUtf8(tr_strpercent(buf.data(), x, buf.size()));
}

QString Formatter::ratioToString(double ratio)
{
    auto buf = std::array<char, 128>{};
    return QString::fromUtf8(tr_strratio(buf.data(), buf.size(), ratio, "\xE2\x88\x9E"));
}

QString Formatter::timeToString(int seconds)
{
    seconds = std::max(seconds, 0);
    auto const days = seconds / 86400;
    auto const hours = (seconds % 86400) / 3600;
    auto const minutes = (seconds % 3600) / 60;
    seconds %= 60;

    auto const d = tr("%Ln day(s)", nullptr, days);
    auto const h = tr("%Ln hour(s)", nullptr, hours);
    auto const m = tr("%Ln minute(s)", nullptr, minutes);
    auto const s = tr("%Ln second(s)", nullptr, seconds);

    QString str;

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
