/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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

struct tr_history_slice
{
    unsigned int n;
    time_t date;
};
typedef struct tr_recentHistory
{
    /* these are PRIVATE IMPLEMENTATION details included for composition only.
     * Don't access these directly! */
    int newest;
    int sliceCount;
    unsigned int precision;
    struct tr_history_slice * slices;
}
tr_recentHistory;

/**
 * @brief construct a new tr_recentHistory object
 * @param seconds how many seconds of history this object should remember
 * @param precision how precise the history should be, in seconds
 *        For a precision of 10 seconds and a history of 2 minutes, makes 12 bins.
 */
void tr_historyConstruct( tr_recentHistory *, unsigned int seconds, unsigned int precision );

/** @brief destruct an existing tr_recentHistory object. */
void tr_historyDestruct( tr_recentHistory * );

/**
 * @brief add a counter to the recent history object.
 * @param when the current time in sec, such as from tr_time()
 * @param n how many items to add to the history's counter
 */
void tr_historyAdd( tr_recentHistory *, time_t when, unsigned int n );

/**
 * @brief count how many events have occurred in the last N seconds.
 * @param when the current time in sec, such as from tr_time()
 * @param seconds how many seconds to count back through.
 */
unsigned int tr_historyGet( const tr_recentHistory *, time_t when, unsigned int seconds );

#endif
