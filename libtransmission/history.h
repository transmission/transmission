/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_RECENT_HISTORY_H
#define TR_RECENT_HISTORY_H

/**
 * A generic short-term memory object that remembers how many times
 * something happened over the last N seconds.
 *
 * For example, it could count how many are bytes transferred
 * to estimate the speed over the last N seconds.
 */

enum
{
  TR_RECENT_HISTORY_PERIOD_SEC = 60
};


typedef struct tr_recentHistory
{
  /* these are PRIVATE IMPLEMENTATION details included for composition only.
   * Don't access these directly! */

  int newest;

  struct {
    unsigned int n;
    time_t date;
  } slices[TR_RECENT_HISTORY_PERIOD_SEC];
}
tr_recentHistory;

/**
 * @brief add a counter to the recent history object.
 * @param when the current time in sec, such as from tr_time ()
 * @param n how many items to add to the history's counter
 */
void tr_historyAdd (tr_recentHistory *, time_t when, unsigned int n);

/**
 * @brief count how many events have occurred in the last N seconds.
 * @param when the current time in sec, such as from tr_time ()
 * @param seconds how many seconds to count back through.
 */
unsigned int tr_historyGet (const tr_recentHistory *, time_t when, unsigned int seconds);

#endif
