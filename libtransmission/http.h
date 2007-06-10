/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef TR_HTTP_H
#define TR_HTTP_H 1

/*
  Parse an HTTP request header to find the method, uri, and version.
  The version will be 11, 10, or -1 on parse error.  The method and/or
  uri pointers may be NULL if the caller is not interested.
*/
int         tr_httpRequestType( const char * data, int len,
                            char ** method, char ** uri );

/* Return the HTTP status code for the response, or -1 for parse error */
int         tr_httpResponseCode( const char * data, int len );

#define TR_HTTP_STATUS_OK( st )             ( 200 <= (st) && 299 >= (st) )
#define TR_HTTP_STATUS_REDIRECT( st )       ( 300 <= (st) && 399 >= (st) )
#define TR_HTTP_STATUS_FAIL( st )           ( 400 <= (st) && 599 >= (st) )
#define TR_HTTP_STATUS_FAIL_CLIENT( st )    ( 400 <= (st) && 499 >= (st) )
#define TR_HTTP_STATUS_FAIL_SERVER( st )    ( 500 <= (st) && 599 >= (st) )

/*
  Parse an HTTP request or response, locating specified headers and
  the body.  The length of the body will be len - ( body - data ).
*/
typedef struct { const char * name; const char * data; int len; }
tr_http_header_t;
char *      tr_httpParse( const char * data, int len, tr_http_header_t *headers );

int         tr_httpIsUrl( const char *, int );
int         tr_httpParseUrl( const char *, int, char **, int *, char ** );

/* fetch a file via HTTP from a standard http:// url */
typedef struct tr_http_s tr_http_t;
#define TR_HTTP_GET             1
#define TR_HTTP_POST            2
#define TR_HTTP_M_POST          3
tr_http_t   * tr_httpClient( int, const char *, int, const char *, ... )
              PRINTF( 4, 5 );
tr_http_t   * tr_httpClientUrl( int, const char *, ... )
              PRINTF( 2, 3 );
/* only add headers or body before first pulse */
void          tr_httpAddHeader( tr_http_t *, const char *, const char * );
void          tr_httpAddBody( tr_http_t *, const char *, ... ) PRINTF( 2, 3 );
void          tr_httpGetRequest( tr_http_t *, const char **, int * );
tr_tristate_t tr_httpPulse( tr_http_t *, const char **, int * );
char        * tr_httpWhatsMyAddress( tr_http_t * );
void          tr_httpClose( tr_http_t * );

#endif
