/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#ifndef TR_HTTP_H
#define TR_HTTP_H

struct tr_handle;
typedef struct tr_web tr_web;

tr_web* tr_webInit( tr_handle * session );

typedef void (tr_web_done_func)( tr_handle    * session,
                                  long          response_code,
                                  const void  * response,
                                  size_t        response_byte_count,
                                  void        * user_data );
                               
void tr_webRun( tr_handle          * session,
                const char         * url,
                tr_web_done_func     done_func,
                void               * done_func_user_data );



#endif
