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

QString Formatter::unitStrings[3][5] = {
    { {tr("B/s")}, {tr("kB/s")}, {tr("MB/s")}, {tr("GB/s")}, {tr("TB/s")}},
    { {tr("B")},   {tr("kB")},   {tr("MB")},   {tr("GB")},   {tr("TB")}},
    { {tr("B")},   {tr("KiB")},  {tr("MiB")},  {tr("GiB")},  {tr("TiB")}},
};

void
Formatter::initUnits ()
{

 speed_K = 1000;
 tr_formatter_speed_init (speed_K,
                          qPrintable(unitStrings[SPEED][KB]),
                          qPrintable(unitStrings[SPEED][MB]),
                          qPrintable(unitStrings[SPEED][GB]),
                          qPrintable(unitStrings[SPEED][TB]));

  size_K = 1000;
  tr_formatter_size_init (size_K,
                          qPrintable(unitStrings[SIZE][KB]),
                          qPrintable(unitStrings[SIZE][MB]),
                          qPrintable(unitStrings[SIZE][GB]),
                          qPrintable(unitStrings[SIZE][TB]));

  mem_K = 1024;
  tr_formatter_mem_init (mem_K,
                         qPrintable(unitStrings[MEM][KB]),
                         qPrintable(unitStrings[MEM][MB]),
                         qPrintable(unitStrings[MEM][GB]),
                         qPrintable(unitStrings[MEM][TB]));
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
        str = tr ("%1, %2").arg (d,h);
    }
  else if (hours)
    {
      if (hours >= 4 || !minutes)
        str = h;
      else
        str = tr ("%1, %2").arg (h,m);
    }
  else if (minutes)
    {
      if (minutes >= 4 || !seconds)
        str = m;
      else
        str = tr ("%1, %2").arg (m,s);
    }
  else
    {
      str = s;
    }

  return str;
}
