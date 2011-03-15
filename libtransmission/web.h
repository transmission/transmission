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

#ifndef TR_HTTP_H
#define TR_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

struct tr_address;

void tr_webInit( tr_session * session );

typedef enum
{
    TR_WEB_CLOSE_WHEN_IDLE,
    TR_WEB_CLOSE_NOW
}
tr_web_close_mode;

/**
 * This is a mechanism for adjusting your CURL* object to match
 * the host OS's platform-dependent settings.
 *
 * A use case for this function is to call curl_easy_setopt() on curl_pointer.
 *
 * Examples of curl_easy_setopt() can be found at
 * http://curl.haxx.se/libcurl/c/curl_easy_setopt.html()
 */
void tr_sessionSetWebConfigFunc( tr_session * session, void (*config)(tr_session * session, void * curl_pointer, const char * url ) );


void tr_webClose( tr_session * session, tr_web_close_mode close_mode );

typedef void ( tr_web_done_func )( tr_session       * session,
                                   tr_bool            timeout_flag,
                                   tr_bool            did_connect_flag,
                                   long               response_code,
                                   const void       * response,
                                   size_t             response_byte_count,
                                   void             * user_data );

const char * tr_webGetResponseStr( long response_code );

void tr_webRun( tr_session        * session,
                const char        * url,
                const char        * range,
                const char        * cookies,
                tr_web_done_func    done_func,
                void              * done_func_user_data );

struct evbuffer;

void tr_webRunWithBuffer( tr_session         * session,
                          const char         * url,
                          const char         * range,
                          const char         * cookies,
                          tr_web_done_func     done_func,
                          void               * done_func_user_data,
                          struct evbuffer    * buffer );

void tr_http_escape( struct evbuffer *out, const char *str, int len, tr_bool escape_slashes );

void tr_http_escape_sha1( char * out, const uint8_t * sha1_digest );

char* tr_http_unescape( const char * str, int len );

#ifdef __cplusplus
}
#endif

#endif
