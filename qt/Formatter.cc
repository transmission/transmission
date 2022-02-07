// This file Copyright © 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_formatter

#include "Formatter.h"
#include "Speed.h"

#include <algorithm>
#include <array>

Formatter& Formatter::get()
{
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static auto& singleton = *new Formatter();
    return singleton;
}

Formatter::Formatter()
    : UnitStrings{ {
          { tr("B/s"), tr("kB/s"), tr("MB/s"), tr("GB/s"), tr("TB/s") }, // SPEED
          { tr("B"), tr("kB"), tr("MB"), tr("GB"), tr("TB") }, // SIZE
          { tr("B"), tr("KiB"), tr("MiB"), tr("GiB"), tr("TiB") } // MEM
      } }
{
    auto const& speed = UnitStrings[SPEED];
    tr_formatter_speed_init(
        SpeedBase,
        speed[KB].toUtf8().constData(),
        speed[MB].toUtf8().constData(),
        speed[GB].toUtf8().constData(),
        speed[TB].toUtf8().constData());

    auto const& size = UnitStrings[SIZE];
    tr_formatter_size_init(
        SizeBase,
        size[KB].toUtf8().constData(),
        size[MB].toUtf8().constData(),
        size[GB].toUtf8().constData(),
        size[TB].toUtf8().constData());

    auto const& mem = UnitStrings[MEM];
    tr_formatter_mem_init(
        MemBase,
        mem[KB].toUtf8().constData(),
        mem[MB].toUtf8().constData(),
        mem[GB].toUtf8().constData(),
        mem[TB].toUtf8().constData());
}

QString Formatter::unitStr(Type t, Size s) const
{
    return UnitStrings[t][s];
}

QString Formatter::memToString(int64_t bytes) const
{
    if (bytes < 0)
    {
        return tr("Unknown");
    }

    if (bytes == 0)
    {
        return tr("None");
    }

    return QString::fromStdString(tr_formatter_mem_B(bytes));
}

QString Formatter::sizeToString(uint64_t bytes) const
{
    if (bytes == 0)
    {
        return tr("None");
    }

    return QString::fromStdString(tr_formatter_size_B(bytes));
}

QString Formatter::sizeToString(int64_t bytes) const
{
    if (bytes < 0)
    {
        return tr("Unknown");
    }

    return Formatter::sizeToString(static_cast<uint64_t>(bytes));
}

QString Formatter::speedToString(Speed const& speed) const
{
    return QString::fromStdString(tr_formatter_speed_KBps(speed.getKBps()));
}

QString Formatter::uploadSpeedToString(Speed const& upload_speed) const
{
    static QChar constexpr UploadSymbol(0x25B4);

    return tr("%1 %2").arg(speedToString(upload_speed)).arg(UploadSymbol);
}

QString Formatter::downloadSpeedToString(Speed const& download_speed) const
{
    static QChar constexpr DownloadSymbol(0x25BE);

    return tr("%1 %2").arg(speedToString(download_speed)).arg(DownloadSymbol);
}

QString Formatter::percentToString(double x) const
{
    return QString::fromStdString(tr_strpercent(x));
}

QString Formatter::ratioToString(double ratio) const
{
    return QString::fromStdString(tr_strratio(ratio, "\xE2\x88\x9E"));
}

QString Formatter::timeToString(int seconds) const
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

/***
****
***/

double Speed::getKBps() const
{
    return getBps() / static_cast<double>(Formatter::SpeedBase);
}

Speed Speed::fromKBps(double KBps)
{
    return Speed{ static_cast<int>(KBps * Formatter::SpeedBase) };
}
