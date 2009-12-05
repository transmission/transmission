/*
 * This file Copyright (C) 2007-2009 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
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

#ifndef _TR_PUBLISHER_H_
#define _TR_PUBLISHER_H_

struct tr_list;

/**
***  A lightweight implementation of the 'Observable' design pattern.
**/

typedef struct tr_publisher
{
    struct tr_list * list;
}
tr_publisher;

typedef void * tr_publisher_tag;

typedef void tr_delivery_func ( void * source,
                                void * event,
                                void * user_data );

/**
***  Observer API
**/

tr_publisher_tag tr_publisherSubscribe( tr_publisher   * publisher,
                                        tr_delivery_func delivery_func,
                                        void *           user_data );

void             tr_publisherUnsubscribe( tr_publisher   * publisher,
                                          tr_publisher_tag tag );

/**
***  Observable API
**/

extern const tr_publisher TR_PUBLISHER_INIT;

void             tr_publisherDestruct( tr_publisher * );

void             tr_publisherPublish( tr_publisher * publisher,
                                      void *           source,
                                      void *           event );

#endif
