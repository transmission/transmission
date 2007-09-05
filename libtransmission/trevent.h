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

#include <stddef.h> /* for size_t */

/**
**/

void tr_eventInit( struct tr_handle_s * tr_handle );

void tr_eventClose( struct tr_handle_s * tr_handle );

/**
**/

struct event;
enum evhttp_cmd_type;
struct evhttp_request;
struct evhttp_connection;
struct bufferevent;

void  tr_event_add( struct tr_handle_s  * tr_handle,
                    struct event      * event,
                    struct timeval    * interval );

void  tr_event_del( struct tr_handle_s  * tr_handle,
                    struct event      * event );

void tr_evhttp_make_request (struct tr_handle_s          * tr_handle,
                             struct evhttp_connection  * evcon,
                             struct evhttp_request     * req,
                             enum evhttp_cmd_type        type,
                             char                      * uri);

void tr_bufferevent_write( struct tr_handle_s    * tr_handle,
                           struct bufferevent  * bufferEvent,
                           const void          * buf,
                           size_t                buflen );

void tr_setBufferEventMode( struct tr_handle_s   * tr_handle,
                            struct bufferevent * bufferEvent,
                            short                mode_enable,
                            short                mode_disable );

#endif
