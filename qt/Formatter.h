// This file Copyright © 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstdint> // int64_t

#include <QCoreApplication> // Q_DECLARE_TR_FUNCTIONS
#include <QString>

class Speed;

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

    static Formatter& get();

    QString memToString(int64_t bytes) const;
    QString sizeToString(int64_t bytes) const;
    QString sizeToString(uint64_t bytes) const;
    QString speedToString(Speed const& speed) const;
    QString percentToString(double x) const;
    QString ratioToString(double ratio) const;
    QString timeToString(int seconds) const;
    QString uploadSpeedToString(Speed const& up) const;
    QString downloadSpeedToString(Speed const& down) const;
    QString unitStr(Type t, Size s) const;

protected:
    Formatter();

private:
    std::array<std::array<QString, Formatter::NUM_SIZES>, Formatter::NUM_TYPES> const UnitStrings;
};
