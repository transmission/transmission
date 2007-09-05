/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef _TR_TIMER_H_
#define _TR_TIMER_H_

typedef struct timer_node * tr_timer_tag;

/**
 * Calls timer_func(user_data) after the specified interval.
 * The timer is freed if timer_func returns zero.
 * Otherwise, it's called again after the same interval.
 *
 * If free_func is non-NULL, free_func(user_data) is called
 * by the timer when it's freed (either from timer_func returning
 * zero or from a client call to tr_timerFree).  This is useful
 * if user_data has resources that need to be freed.
 */
tr_timer_tag  tr_timerNew( struct tr_handle_s  * handle,
                           int                  timer_func( void * user_data ),
                           void               * user_data,
                           void                 free_func( void * user_data ),
                           int                  timeout_milliseconds );

/**
 * Frees a timer and sets its tag to NULL.
 */
void tr_timerFree( tr_timer_tag * tag );


#endif
