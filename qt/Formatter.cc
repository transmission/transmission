/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_formatter

#include "Formatter.h"
#include "Speed.h"

/***
****  Constants
***/

namespace
{
 unsigned int speed_K;
 unsigned int mem_K;
 unsigned int size_K;
}

QString Formatter::unitStrings[3][5];

void
Formatter::initUnits ()
{
 speed_K = 1000;
 unitStrings[SPEED][B]  = tr ( "B/s");
 unitStrings[SPEED][KB] = tr ("kB/s");
 unitStrings[SPEED][MB] = tr ("MB/s");
 unitStrings[SPEED][GB] = tr ("GB/s");
 unitStrings[SPEED][TB] = tr ("TB/s");
 tr_formatter_speed_init (speed_K,
                          unitStrings[SPEED][KB].toUtf8().constData(),
                          unitStrings[SPEED][MB].toUtf8().constData(),
                          unitStrings[SPEED][GB].toUtf8().constData(),
                          unitStrings[SPEED][TB].toUtf8().constData());

  size_K = 1000;
  unitStrings[SIZE][B]  = tr ( "B");
  unitStrings[SIZE][KB] = tr ("kB");
  unitStrings[SIZE][MB] = tr ("MB");
  unitStrings[SIZE][GB] = tr ("GB");
  unitStrings[SIZE][TB] = tr ("TB");
  tr_formatter_size_init (size_K,
                          unitStrings[SIZE][KB].toUtf8().constData(),
                          unitStrings[SIZE][MB].toUtf8().constData(),
                          unitStrings[SIZE][GB].toUtf8().constData(),
                          unitStrings[SIZE][TB].toUtf8().constData());

  mem_K = 1024;
  unitStrings[MEM][B]  = tr (  "B");
  unitStrings[MEM][KB] = tr ("KiB");
  unitStrings[MEM][MB] = tr ("MiB");
  unitStrings[MEM][GB] = tr ("GiB");
  unitStrings[MEM][TB] = tr ("TiB");
  tr_formatter_mem_init (mem_K,
                         unitStrings[MEM][KB].toUtf8().constData(),
                         unitStrings[MEM][MB].toUtf8().constData(),
                         unitStrings[MEM][GB].toUtf8().constData(),
                         unitStrings[MEM][TB].toUtf8().constData());
}

/***
****
***/

double
Speed::KBps () const
{
  return _Bps / static_cast<double> (speed_K);
}

Speed
Speed::fromKBps (double KBps)
{
  return static_cast<int> (KBps * speed_K);
}

/***
****
***/

QString
Formatter::memToString (int64_t bytes)
{
  if (bytes < 0)
    return tr ("Unknown");

  if (!bytes)
    return tr ("None");

  char buf[128];
  tr_formatter_mem_B (buf, bytes, sizeof (buf));
  return QString::fromUtf8 (buf);
}

QString
Formatter::sizeToString (int64_t bytes)
{
  if (bytes < 0)
    return tr ("Unknown");

  if (!bytes)
    return tr ("None");

  char buf[128];
  tr_formatter_size_B (buf, bytes, sizeof (buf));
  return QString::fromUtf8 (buf);
}

QString
Formatter::speedToString (const Speed& speed)
{
  char buf[128];
  tr_formatter_speed_KBps (buf, speed.KBps (), sizeof (buf));
  return QString::fromUtf8 (buf);
}

QString
Formatter::uploadSpeedToString (const Speed& uploadSpeed)
{
  static const QChar uploadSymbol (0x25B4);

  return tr ("%1 %2").arg (speedToString (uploadSpeed)).arg (uploadSymbol);
}

QString
Formatter::downloadSpeedToString (const Speed& downloadSpeed)
{
  static const QChar downloadSymbol (0x25BE);

  return tr ("%1 %2").arg (speedToString (downloadSpeed)).arg (downloadSymbol);
}

QString
Formatter::percentToString (double x)
{
  char buf[128];
  return QString::fromUtf8 (tr_strpercent (buf, x, sizeof (buf)));
}

QString
Formatter::ratioToString (double ratio)
{
  char buf[128];
  return QString::fromUtf8 (tr_strratio (buf, sizeof (buf), ratio, "\xE2\x88\x9E"));
}

QString
Formatter::timeToString (int seconds)
{
  int days, hours, minutes;
  QString d, h, m, s;
  QString str;

  if (seconds < 0)
    seconds = 0;

  days = seconds / 86400;
  hours = (seconds % 86400) / 3600;
  minutes = (seconds % 3600) / 60;
  seconds %= 60;

  d = tr ("%Ln day(s)", 0, days);
  h = tr ("%Ln hour(s)", 0, hours);
  m = tr ("%Ln minute(s)", 0, minutes);
  s = tr ("%Ln second(s)", 0, seconds);

  if (days)
    {
      if (days >= 4 || !hours)
        str = d;
      else
        str = tr ("%1, %2").arg (d).arg (h);
    }
  else if (hours)
    {
      if (hours >= 4 || !minutes)
        str = h;
      else
        str = tr ("%1, %2").arg (h).arg (m);
    }
  else if (minutes)
    {
      if (minutes >= 4 || !seconds)
        str = m;
      else
        str = tr ("%1, %2").arg (m).arg (s);
    }
  else
    {
      str = s;
    }

  return str;
}
