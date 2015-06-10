/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FORMATTER_H
#define QTR_FORMATTER_H

#include <stdint.h> // int64_t

#include <QString>
#include <QObject>
#include <QIcon>

class Speed;

class Formatter: public QObject
{
    Q_OBJECT

  public:

    Formatter() {}
    virtual ~Formatter() {}

  public:

    static QString memToString (int64_t bytes);
    static QString sizeToString (int64_t bytes);
    static QString speedToString (const Speed& speed);
    static QString percentToString (double x);
    static QString ratioToString (double ratio);
    static QString timeToString (int seconds);
    static QString uploadSpeedToString(const Speed& up);
    static QString downloadSpeedToString(const Speed& down);

  public:

    typedef enum { B, KB, MB, GB, TB } Size;
    typedef enum { SPEED, SIZE, MEM } Type;
    static QString unitStr (Type t, Size s) { return unitStrings[t][s]; }
    static void initUnits ();

  private:

    static QString unitStrings[3][5];
};

#endif // QTR_FORMATTER_H
