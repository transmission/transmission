/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
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
typedef struct tr_recentHistory tr_recentHistory;

/**
 * @brief create a new tr_recentHistory object.
 * @param seconds how many seconds of history this object should remember
 * @param precision how precise the history should be, in fractions of a second.
 *        For a precision of 1/20th of a second, use a precision of 20.
 */
tr_recentHistory * tr_historyNew( int seconds, int precision );

/** @brief destroy an existing tr_recentHistory object. */
void tr_historyFree( tr_recentHistory * );

/**
 * @brief add a counter to the recent history object.
 * @param when the current time in msec, such as from tr_date()
 * @param n how many items to add to the history's counter
 */
void tr_historyAdd( tr_recentHistory *, uint64_t when, double n );

/**
 * @brief count how many events have occurred in the last N seconds.
 * @param when the current time in msec, such as from tr_date()
 * @param seconds how many seconds to count back through.
 */
double tr_historyGet( const tr_recentHistory *, uint64_t when, int seconds );

#endif
