// This file Copyright © 2012-2023 Mnemosyne LLC.
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
    enum Size
    {
        B,
        KB,
        MB,
        GB,
        TB,

        NUM_SIZES
    };

    enum Type
    {
        SPEED,
        SIZE,
        MEM,

        NUM_TYPES
    };

    static constexpr int SpeedBase = 1000;
    static constexpr int SizeBase = 1000;
    static constexpr int MemBase = 1024;

    [[nodiscard]] static Formatter& get();

    [[nodiscard]] QString memToString(int64_t bytes) const;
    [[nodiscard]] QString sizeToString(int64_t bytes) const;
    [[nodiscard]] QString sizeToString(uint64_t bytes) const;
    [[nodiscard]] QString timeToString(int seconds) const;
    [[nodiscard]] QString unitStr(Type t, Size s) const;

    [[nodiscard]] auto speedToString(Speed const& speed) const
    {
        return QString::fromStdString(tr_formatter_speed_KBps(speed.getKBps()));
    }

    [[nodiscard]] auto uploadSpeedToString(Speed const& upload_speed) const
    {
        static auto constexpr UploadSymbol = QChar{ 0x25B4 };

        return tr("%1 %2").arg(speedToString(upload_speed)).arg(UploadSymbol);
    }

    [[nodiscard]] auto downloadSpeedToString(Speed const& download_speed) const
    {
        static auto constexpr DownloadSymbol = QChar{ 0x25BE };

        return tr("%1 %2").arg(speedToString(download_speed)).arg(DownloadSymbol);
    }

    [[nodiscard]] auto percentToString(double x) const
    {
        return QString::fromStdString(tr_strpercent(x));
    }

    [[nodiscard]] auto ratioToString(double ratio) const
    {
        static auto constexpr InfinitySymbol = "\xE2\x88\x9E";

        return QString::fromStdString(tr_strratio(ratio, InfinitySymbol));
    }

protected:
    Formatter();

private:
    std::array<std::array<QString, Formatter::NUM_SIZES>, Formatter::NUM_TYPES> const UnitStrings;
};
