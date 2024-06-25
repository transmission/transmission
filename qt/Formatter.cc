// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/values.h>

#include "Formatter.h"

#include <algorithm>

using namespace std::literals;
using namespace libtransmission::Values;

QString Formatter::memory_to_string(int64_t const bytes)
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

QString Formatter::storage_to_string(uint64_t const bytes)
{
    if (bytes == 0)
    {
        return tr("None");
    }

    return QString::fromStdString(Storage{ bytes, Storage::Units::Bytes }.to_string());
}

QString Formatter::storage_to_string(int64_t const bytes)
{
    if (bytes < 0)
    {
        return tr("Unknown");
    }

    return storage_to_string(static_cast<uint64_t>(bytes));
}

QString Formatter::time_to_string(int seconds)
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
