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

#ifndef TR_EVENT_H

#include <stddef.h> /* size_t */
#include <inttypes.h> /* uint64_t */

/**
**/

void tr_eventInit( struct tr_handle * tr_handle );

void tr_eventClose( struct tr_handle * tr_handle );

/**
**/

struct event;
enum evhttp_cmd_type;
struct evhttp_request;
struct evhttp_connection;
struct bufferevent;

void tr_evhttp_make_request (struct tr_handle          * tr_handle,
                             struct evhttp_connection  * evcon,
                             struct evhttp_request     * req,
                             enum evhttp_cmd_type        type,
                             char                      * uri);

void tr_setBufferEventMode( struct tr_handle   * tr_handle,
                            struct bufferevent * bufferEvent,
                            short                mode_enable,
                            short                mode_disable );

int tr_amInEventThread( struct tr_handle * handle );

/**
***
**/

typedef struct tr_timer  tr_timer;

/**
 * Calls timer_func(user_data) after the specified interval.
 * The timer is freed if timer_func returns zero.
 * Otherwise, it's called again after the same interval.
 */
tr_timer* tr_timerNew( struct tr_handle  * handle,
                       int                 func( void * user_data ),
                       void              * user_data,
                       uint64_t            timeout_milliseconds );

/**
 * Frees a timer and sets the timer pointer to NULL.
 */
void tr_timerFree( tr_timer ** timer );

void tr_runInEventThread( struct tr_handle * handle,
                          void               func( void* ),
                          void             * user_data );

#endif
