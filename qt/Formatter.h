// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstdint> // int64_t

#include <QCoreApplication> // Q_DECLARE_TR_FUNCTIONS
#include <QString>

#include <libtransmission/utils.h>

#include "Speed.h"

class Formatter
{
    Q_DECLARE_TR_FUNCTIONS(Formatter)

public:
    Formatter() = delete;

    [[nodiscard]] static QString memToString(int64_t bytes);
    [[nodiscard]] static QString sizeToString(int64_t bytes);
    [[nodiscard]] static QString sizeToString(uint64_t bytes);
    [[nodiscard]] static QString timeToString(int seconds);

    [[nodiscard]] static auto percentToString(double x)
    {
        return QString::fromStdString(tr_strpercent(x));
    }

    [[nodiscard]] static auto ratioToString(double ratio)
    {
        static auto constexpr InfinitySymbol = "\xE2\x88\x9E";

        return QString::fromStdString(tr_strratio(ratio, InfinitySymbol));
    }
};
