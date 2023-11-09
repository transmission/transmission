// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>

#include <libtransmission/values.h>

#include "Formatter.h"
#include "Speed.h"

#include <algorithm>

using namespace std::literals;

using namespace libtransmission::Values;

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
    namespace Values = libtransmission::Values;

    auto const& speed = UnitStrings[SPEED];
    Values::Config::Speed = { Values::Config::Base::Kilo, "B"sv,
                              speed[KB].toStdString(),    speed[MB].toStdString(),
                              speed[GB].toStdString(),    speed[TB].toStdString() };

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

    return QString::fromStdString(Memory{ bytes, Memory::Units::Bytes }.to_string());
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

QString Formatter::timeToString(int seconds) const
{
    seconds = std::max(seconds, 0);

    if (seconds < 60)
    {
        return tr("%Ln second(s)", nullptr, seconds);
    }

    auto const minutes = seconds / 60;

    if (minutes < 60)
    {
        return tr("%Ln minute(s)", nullptr, minutes);
    }

    auto const hours = minutes / 60;

    if (hours < 24)
    {
        return tr("%Ln hour(s)", nullptr, hours);
    }

    auto const days = hours / 24;

    return tr("%Ln day(s)", nullptr, days);
}
