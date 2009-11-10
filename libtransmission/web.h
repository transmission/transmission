/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_HTTP_H
#define TR_HTTP_H

struct tr_address;
typedef struct tr_web tr_web;

tr_web*      tr_webInit( tr_session * session );

void         tr_webClose( tr_web ** );

void         tr_webSetInterface( tr_web * web, const struct tr_address * addr );

typedef void ( tr_web_done_func )( tr_session       * session,
                                   long               response_code,
                                   const void       * response,
                                   size_t             response_byte_count,
                                   void             * user_data );

const char * tr_webGetResponseStr( long response_code );

void         tr_webRun( tr_session        * session,
                        const char        * url,
                        const char        * range,
                        tr_web_done_func    done_func,
                        void              * done_func_user_data );

void tr_http_escape( struct evbuffer *out, const char *str, int len, int noslashes );

#endif
