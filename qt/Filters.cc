/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include "Filters.h"

const QString FilterMode::names[NUM_MODES] =
{
  QStringLiteral ("show-all"),
  QStringLiteral ("show-active"),
  QStringLiteral ("show-downloading"),
  QStringLiteral ("show-seeding"),
  QStringLiteral ("show-paused"),
  QStringLiteral ("show-finished"),
  QStringLiteral ("show-verifying"),
  QStringLiteral ("show-error")
};

int
FilterMode::modeFromName (const QString& name)
{
  for (int i=0; i<NUM_MODES; ++i)
    if( names[i] == name )
      return i;

  return FilterMode().mode(); // use the default value
}

const QString SortMode::names[NUM_MODES] =
{
  QStringLiteral ("sort-by-activity"),
  QStringLiteral ("sort-by-age"),
  QStringLiteral ("sort-by-eta"),
  QStringLiteral ("sort-by-name"),
  QStringLiteral ("sort-by-progress"),
  QStringLiteral ("sort-by-queue"),
  QStringLiteral ("sort-by-ratio"),
  QStringLiteral ("sort-by-size"),
  QStringLiteral ("sort-by-state"),
  QStringLiteral ("sort-by-id")
};

int
SortMode::modeFromName (const QString& name)
{
  for (int i=0; i<NUM_MODES; ++i)
    if (names[i] == name)
      return i;

  return SortMode().mode(); // use the default value
}
