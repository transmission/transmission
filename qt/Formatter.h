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

    [[nodiscard]] static QString memory_to_string(int64_t bytes);

    [[nodiscard]] static auto percent_to_string(double x)
    {
        return QString::fromStdString(tr_strpercent(x));
    }

    [[nodiscard]] static QString ratio_to_string(double ratio);

    [[nodiscard]] static QString storage_to_string(int64_t bytes);
    [[nodiscard]] static QString storage_to_string(uint64_t bytes);

    [[nodiscard]] static QString time_to_string(int seconds);
};
