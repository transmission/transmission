/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <cstdint> // int64_t

#include <QCoreApplication>
#include <QString>

#include "Typedefs.h"

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
        TB
    };

    enum Type
    {
        SPEED,
        SIZE,
        MEM
    };

public:
    static QString memToString(int64_t bytes);
    static QString sizeToString(int64_t bytes);
    static QString percentToString(double x);
    static QString ratioToString(double ratio);
    static QString timeToString(int seconds);
    static QString timeToString(seconds_t const& value);

    static QString storage(bytes_t const& value);
    static QString speed(KBps_t const& value);
    static QString speedUp(KBps_t const& value);
    static QString speedDown(KBps_t const& value);

    static QString unitStr(Type t, Size s)
    {
        return unitStrings[t][s];
    }

    static void initUnits();

private:
    static QString unitStrings[3][5];
};
