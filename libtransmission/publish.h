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

#ifndef _TR_PUBLISHER_H_
#define _TR_PUBLISHER_H_

/**
***  A lightweight implementation of the 'Observable' design pattern.
**/

typedef struct tr_publisher_s tr_publisher_t;

typedef void * tr_publisher_tag;

typedef void tr_delivery_func( void * source,
                               void * event,
                               void * user_data );

/**
***  Observer API
**/

tr_publisher_tag   tr_publisherSubscribe   ( tr_publisher_t    * publisher,
                                             tr_delivery_func    delivery_func,
                                             void              * user_data );

void               tr_publisherUnsubscribe ( tr_publisher_t    * publisher,
                                             tr_publisher_tag    tag );

/**
***  Observable API
**/

tr_publisher_t *   tr_publisherNew         ( void );

void               tr_publisherFree        ( tr_publisher_t    * publisher );

void               tr_publisherPublish     ( tr_publisher_t    * publisher,
                                             void              * source,
                                             void              * event );

#endif
