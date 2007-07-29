/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "transmission.h"
#include "http.h"
#include "net.h"
#include "trcompat.h"

#define HTTP_PORT               80      /* default http port 80 */
#define HTTP_TIMEOUT            60000   /* one minute http timeout */
#define HTTP_BUFSIZE            1500    /* 1.5K buffer size increment */
#define LF                      "\012"
#define CR                      "\015"
#define SP( cc )                ( ' ' == (cc)  || '\t' == (cc) )
#define NL( cc )                ( '\015' == (cc) || '\012' == (cc) )
#define NUM( cc )               ( '0' <= (cc) && '9' >= (cc) )
#define ALEN( aa )              ( (int)(sizeof( (aa) ) / sizeof( (aa)[0] ) ) )
#define SKIP( off, len, done ) \
    while( (off) < (len) && (done) ) { (off)++; };

static const char *
slice( const char * data, int * len, const char * delim );
static tr_tristate_t
sendrequest( tr_http_t * http );
static tr_tristate_t
receiveresponse( tr_http_t * http );
static int
checklength( tr_http_t * http );
static int
learnlength( tr_http_t * http );

#define EXPANDBUF( bs )         &(bs).buf, &(bs).used, &(bs).size
struct buf {
    char         * buf;
    int            size;
    int            used;
};

struct tr_http_s {
#define HTTP_STATE_CONSTRUCT    1
#define HTTP_STATE_RESOLVE      2
#define HTTP_STATE_CONNECT      3
#define HTTP_STATE_RECEIVE      4
#define HTTP_STATE_DONE         5
#define HTTP_STATE_ERROR        6
    char           state;
#define HTTP_LENGTH_UNKNOWN     1
#define HTTP_LENGTH_EOF         2
#define HTTP_LENGTH_FIXED       3
#define HTTP_LENGTH_CHUNKED     4
    char           lengthtype;
    tr_resolve_t * resolve;
    char         * host;
    int            port;
    int            sock;
    struct buf     header;
    struct buf     body;
    uint64_t       date;
    /*
      eof: unused
      fixed: lenptr is the start of the body, lenint is the body length
      chunked: lenptr is start of chunk (after length), lenint is chunk size
    */
    int            chunkoff;
    int            chunklen;
};

int
tr_httpRequestType( const char * data, int len, char ** method, char ** uri )
{
    const char * words[6];
    int          ii, ret;
    const char * end;

    /* find the end of the line */
    for( end = data; data + len > end; end++ )
    {
        if( NL( *data) )
        {
            break;
        }
    }

    /* find the first three "words" in the line */
    for( ii = 0; ALEN( words ) > ii && data < end; ii++ )
    {
        /* find the next space or non-space */
        while( data < end && ( ii % 2 ? !SP( *data ) : SP( *data ) ) )
        {
            data++;
        }

        /* save the beginning of the word */
        words[ii] = data;
    }

    /* check for missing words */
    if( ALEN( words) > ii )
    {
        return -1;
    }

    /* parse HTTP version */
    ret = -1;
    if( 8 <= words[5] - words[4] )
    {
        if( 0 == tr_strncasecmp( words[4], "HTTP/1.1", 8 ) )
        {
            ret = 11;
        }
        else if( 0 == tr_strncasecmp( words[4], "HTTP/1.0", 8 ) )
        {
            ret = 10;
        }
    }

    /* copy the method */
    if( 0 <= ret && NULL != method )
    {
        *method = tr_strndup( words[0], words[1] - words[0] );
        if( NULL == *method )
        {
            ret = -1;
        }
    }
    /* copy uri */
    if( 0 <= ret && NULL != uri )
    {
        *uri = tr_strndup( words[2], words[3] - words[2] );
        if( NULL == *uri )
        {
            free( *method );
            ret = -1;
        }
    }

    return ret;
}

int
tr_httpResponseCode( const char * data, int len )
{
    char code[4];
    int ret;

    /* check for the minimum legal length */
    if( 12 > len ||
    /* check for valid http version */
        0 != tr_strncasecmp( data, "HTTP/1.", 7 ) ||
        ( '1' != data[7] && '0' != data[7] ) ||
    /* there should be at least one space after the version */
        !SP( data[8] ) )
    {
        return -1;
    }

    /* skip any extra spaces */
    data += 9;
    len -= 9;
    while( 0 < len && SP( *data ) )
    {
        data++;
        len--;
    }

    /* check for a valid three-digit code */
    if( 3 > len || !NUM( data[0] ) || !NUM( data[1] ) || !NUM( data[2] ) ||
        ( 3 < len && NUM( data[3] ) ) )
    {
        return -1;
    }

    /* parse and return the code */
    memcpy( code, data, 3 );
    code[3] = '\0';
    ret = strtol( code, NULL, 10 );
    if( 100 > ret )
    {
        ret = -1;
    }

    return ret;
}

char *
tr_httpParse( const char * data, int len, tr_http_header_t *headers )
{
    const char * body, * begin;
    int          ii, jj, full;

    /* find the end of the http headers */
    body = slice( data, &len, CR LF CR LF );
    if( NULL == body )
    {
        body = slice( data, &len, LF LF );
        if( NULL == body )
        {
            body = slice( data, &len, CR CR );
            if( NULL == body )
            {
                return NULL;
            }
        }
    }

    /* return if no headers were requested */
    if( NULL == headers || NULL == headers[0].name )
    {
        return (char*) body;
    }

    /* NULL out all the header's data pointers */
    for( ii = 0; NULL != headers[ii].name; ii++ )
    {
        headers[ii].data = NULL;
        headers[ii].len = 0;
    }

    /* skip the http request or response line */
    ii = 0;
    SKIP( ii, len, !NL( data[ii] ) );
    SKIP( ii, len, NL( data[ii] ) );

    /* find the requested headers */
    while(ii < len )
    {
        /* skip leading spaces and find the header name */
        SKIP( ii, len, SP( data[ii] ) );
        begin = data + ii;
        SKIP( ii, len, ':' != data[ii] && !NL( data[ii] ) );

        if( ':' == data[ii] )
        {
            full = 1;

            /* try to match the found header with one of the requested */
            for( jj = 0; NULL != headers[jj].name; jj++ )
            {
                if( NULL == headers[jj].data )
                {
                    full = 0;
                    if( 0 == tr_strncasecmp( headers[jj].name, begin,
                                             ( data + ii ) - begin ) )
                    {
                        ii++;
                        /* skip leading whitespace and save the header value */
                        SKIP( ii, len, SP( data[ii] ) );
                        headers[jj].data = data + ii;
                        SKIP( ii, len, !NL( data[ii] ) );
                        headers[jj].len = ( data + ii ) - headers[jj].data;
                        break;
                    }
                }
            }
            if( full )
            {
                break;
            }

            /* skip to the end of the header */
            SKIP( ii, len, !NL( data[ii] ) );
        }

        /* skip past the newline */
        SKIP( ii, len, NL( data[ii] ) );
    }

    return (char*)body;
}

static const char *
slice( const char * data, int * len, const char * delim )
{
    const char *body;
    int dlen;

    dlen = strlen( delim );
    body = tr_memmem( data, *len, delim, dlen );

    if( NULL != body )
    {
        *len = body - data;
        body += dlen;
    }

    return body;
}

int
tr_httpIsUrl( const char * url, int len )
{
    if( 0 > len )
    {
        len = strlen( url );
    }

    /* check for protocol */
    if( 7 > len || 0 != tr_strncasecmp( url, "http://", 7 ) )
    {
        return 0;
    }

    return 7;
}

int
tr_httpParseUrl( const char * url, int len,
                 char ** host, int * port, char ** path )
{
    const char * pathstart, * hostend;
    int          ii, colon, portnum;
    char         str[6];

    if( 0 > len )
    {
        len = strlen( url );
    }

    ii = tr_httpIsUrl( url, len );
    if( 0 >= ii )
    {
        return 1;
    }
    url += ii;
    len -= ii;

    /* find the hostname and port */
    colon = -1;
    for( ii = 0; len > ii && '/' != url[ii]; ii++ )
    {
        if( ':' == url[ii] )
        {
            colon = ii;
        }
    }
    hostend = url + ( 0 > colon ? ii : colon );
    pathstart = url + ii;

    /* parse the port number */
    portnum = HTTP_PORT;
    if( 0 <= colon )
    {
        colon++;
        memset( str, 0, sizeof( str ) );
        memcpy( str, url + colon, MIN( (int) sizeof( str) - 1, ii - colon ) );
        portnum = strtol( str, NULL, 10 );
        if( 0 >= portnum || 0xffff <= portnum )
        {
            tr_err( "Invalid port (%i)", portnum );
            return 1;
        }
    }

    if( NULL != host )
    {
        *host = tr_strndup( url, hostend - url );
    }
    if( NULL != port )
    {
        *port = portnum;
    }
    if( NULL != path )
    {
        if( 0 < len - ( pathstart - url ) )
        {
            *path = tr_strndup( pathstart, len - ( pathstart - url ) );
        }
        else
        {
            *path = strdup( "/" );
        }
    }

    return 0;
}

tr_http_t *
tr_httpClient( int method, const char * host, int port, const char * fmt, ... )
{
    tr_http_t    * http;
    va_list        ap1, ap2;
    char         * methodstr;

    http = malloc( sizeof( *http ) );
    if( NULL == http )
    {
        return NULL;
    }

    memset( http, 0, sizeof( *http ) );
    http->state      = HTTP_STATE_CONSTRUCT;
    http->lengthtype = HTTP_LENGTH_UNKNOWN;
    http->host       = strdup( host );
    http->port       = port;
    http->sock       = -1;

    if( NULL == http->host || NULL == fmt )
    {
        goto err;
    }

    switch( method )
    {
        case TR_HTTP_GET:
            methodstr = "GET";
            break;
        case TR_HTTP_POST:
            methodstr = "POST";
            break;
        case TR_HTTP_M_POST:
            methodstr = "M-POST";
            break;
        default:
            goto err;
    }
    if( tr_sprintf( EXPANDBUF( http->header ), "%s ", methodstr ) )
    {
        goto err;
    }

    va_start( ap1, fmt );
    va_start( ap2, fmt );
    if( tr_vsprintf( EXPANDBUF( http->header ), fmt, ap1, ap2 ) )
    {
        va_end( ap2 );
        va_end( ap1 );
        goto err;
    }
    va_end( ap2 );
    va_end( ap1 );

    if( tr_sprintf( EXPANDBUF( http->header ), " HTTP/1.1" CR LF
                    "Host: %s:%d" CR LF
                    "User-Agent: " TR_NAME "/" LONG_VERSION_STRING CR LF
                    "Connection: close" CR LF,
                    http->host, http->port ) )
    {
        goto err;
    }

    return http;

  err:
    tr_httpClose( http );
    return NULL;
}

tr_http_t *
tr_httpClientUrl( int method, const char * fmt, ... )
{
    char      * url, * host, * path;
    int         port;
    va_list     ap;
    tr_http_t * ret;

    va_start( ap, fmt );
    url = NULL;
    vasprintf( &url, fmt, ap );
    va_end( ap );

    if( tr_httpParseUrl( url, -1, &host, &port, &path ) )
    {
        tr_err( "Invalid HTTP URL: %s", url );
        free( url );
        return NULL;
    }
    free( url );

    ret = tr_httpClient( method, host, port, "%s", path );
    free( host );
    free( path );

    return ret;
}

void
tr_httpAddHeader( tr_http_t * http, const char * name, const char * value )
{
    if( HTTP_STATE_CONSTRUCT == http->state )
    {
        if( tr_sprintf( EXPANDBUF( http->header ),
                        "%s: %s" CR LF, name, value ) )
        {
            http->state = HTTP_STATE_ERROR;
        }
    }
    else
    {
        assert( HTTP_STATE_ERROR == http->state );
    }
}

void
tr_httpAddBody( tr_http_t * http , const char * fmt , ... )
{
    va_list ap1, ap2;

    if( HTTP_STATE_CONSTRUCT == http->state )
    {
        va_start( ap1, fmt );
        va_start( ap2, fmt );
        if( tr_vsprintf( EXPANDBUF( http->body ), fmt, ap1, ap2 ) )
        {
            http->state = HTTP_STATE_ERROR;
        }
        va_end( ap2 );
        va_end( ap1 );
    }
    else
    {
        assert( HTTP_STATE_ERROR == http->state );
    }
}

void
tr_httpGetHeaders( tr_http_t * http, const char ** buf, int * len )
{
    *buf = http->header.buf;
    *len = http->header.used;
}

void
tr_httpGetBody( tr_http_t * http, const char ** buf, int * len )
{
    *buf = http->body.buf;
    *len = http->body.used;
}

tr_tristate_t
tr_httpPulse( tr_http_t * http, const char ** data, int * len )
{
    struct in_addr addr;

    switch( http->state )
    {
        case HTTP_STATE_CONSTRUCT:
            if( tr_sprintf( EXPANDBUF( http->header ), "Content-length: %i"
                            CR LF CR LF, http->body.used ) )
            {
                goto err;
            }
            if( !tr_netResolve( http->host, &addr ) )
            {
                http->sock = tr_netOpenTCP( &addr, htons( http->port ), 1 );
                http->state = HTTP_STATE_CONNECT;
                break;
            }
            http->resolve = tr_netResolveInit( http->host );
            if( NULL == http->resolve )
            {
                goto err;
            }
            http->state = HTTP_STATE_RESOLVE;
            /* fallthrough */

        case HTTP_STATE_RESOLVE:
            switch( tr_netResolvePulse( http->resolve, &addr ) )
            {
                case TR_NET_WAIT:
                    return TR_NET_WAIT;
                case TR_NET_ERROR:
                    goto err;
                case TR_NET_OK:
                    tr_netResolveClose( http->resolve );
                    http->resolve = NULL;
                    http->sock = tr_netOpenTCP( &addr, htons( http->port ), 1 );
                    http->state = HTTP_STATE_CONNECT;
            }
            /* fallthrough */

        case HTTP_STATE_CONNECT:
            switch( sendrequest( http ) )
            {
                case TR_NET_WAIT:
                    return TR_NET_WAIT;
                case TR_NET_ERROR:
                    goto err;
                case TR_NET_OK:
                    http->state = HTTP_STATE_RECEIVE;
            }
            /* fallthrough */

        case HTTP_STATE_RECEIVE:
            switch( receiveresponse( http ) )
            {
                case TR_NET_WAIT:
                    return TR_NET_WAIT;
                case TR_NET_ERROR:
                    goto err;
                case TR_NET_OK:
                    goto ok;
            }
            break;

        case HTTP_STATE_DONE:
            goto ok;

        case HTTP_STATE_ERROR:
            goto err;
    }

    return TR_NET_WAIT;

  err:
    http->state = HTTP_STATE_ERROR;
    return TR_NET_ERROR;

  ok:
    http->state = HTTP_STATE_DONE;
    if( NULL != data )
    {
        *data = http->header.buf;
    }
    if( NULL != len )
    {
        *len = http->header.used;
    }
    return TR_NET_OK;
}

static tr_tristate_t
sendrequest( tr_http_t * http )
{
    struct buf * buf;

    if( !http->date )
         http->date = tr_date();

    if( 0 > http->sock || tr_date() > http->date + HTTP_TIMEOUT )
    {
        return TR_NET_ERROR;
    }

    buf = ( 0 < http->header.used ? &http->header : &http->body );
    while( 0 < buf->used )
    {
        const int ret = tr_netSend( http->sock, buf->buf, buf->used );
        if( ret & TR_NET_CLOSE ) return TR_NET_ERROR;
        if( ret & TR_NET_BLOCK ) return TR_NET_WAIT;
        buf->used = 0;
        buf = &http->body;
    }

    free( http->body.buf );
    http->body.buf = NULL;
    http->body.size = 0;
    http->date = 0;

    return TR_NET_OK;
}

static tr_tristate_t
receiveresponse( tr_http_t * http )
{
    int    ret, before;
    void * newbuf;

    if( 0 == http->date )
    {
        http->date = tr_date();
    }

    before = http->header.used;
    for(;;)
    {
        if( http->header.size - http->header.used < HTTP_BUFSIZE )
        {
            newbuf = realloc( http->header.buf,
                              http->header.size + HTTP_BUFSIZE );
            if( NULL == newbuf )
            {
                return TR_NET_ERROR;
            }
            http->header.buf = newbuf;
            http->header.size += HTTP_BUFSIZE;
        }

        ret = tr_netRecv( http->sock,
                          (uint8_t *) ( http->header.buf + http->header.used ),
                          http->header.size - http->header.used );
        if( ret & TR_NET_CLOSE )
        {
            checklength( http );
            return TR_NET_OK;
        }
        else if( ret & TR_NET_BLOCK )
        {
            break;
        }
        else
        {
            http->header.used += ret;
        }
    }

    if( before < http->header.used && checklength( http ) )
    {
        return TR_NET_OK;
    }

    if( tr_date() > HTTP_TIMEOUT + http->date )
    {
        return TR_NET_ERROR;
    }

    return TR_NET_WAIT;
}

static int
checklength( tr_http_t * http )
{
    char * buf;
    int    num, ii, len, lastnum;

    switch( http->lengthtype )
    {
        case HTTP_LENGTH_UNKNOWN:
            if( learnlength( http ) )
            {
                return checklength( http );
            }
            break;

        case HTTP_LENGTH_EOF:
            break;

        case HTTP_LENGTH_FIXED:
            if( http->header.used >= http->chunkoff + http->chunklen )
            {
                http->header.used = http->chunkoff + http->chunklen;
                return 1;
            }
            break;

        case HTTP_LENGTH_CHUNKED:
            buf     = http->header.buf;
            lastnum = -1;
            while( http->header.used > http->chunkoff + http->chunklen )
            {
                num = http->chunkoff + http->chunklen;
                if( lastnum == num )
                {
                    /* ugh, some trackers send Transfer-encoding: chunked
                       and then don't encode the body */
                    http->lengthtype = HTTP_LENGTH_EOF;
                    return checklength( http );
                }
                lastnum = num;
                while(  http->header.used > num && NL( buf[num] ) )
                {
                    num++;
                }
                ii = num;
                while( http->header.used > ii && !NL( buf[ii] ) )
                {
                    ii++;
                }
                if( http->header.used > ii )
                {
                    /* strtol should stop at the newline */
                    len = strtol( buf + num, NULL, 16 );
                    if( 0 == len )
                    {
                        /* XXX should handle invalid length
                               differently than 0 length chunk */
                        http->header.used = http->chunkoff + http->chunklen;
                        return 1;
                    }
                    if( http->header.used > ii + 1 )
                    {
                        ii += ( 0 == memcmp( buf + ii, CR LF, 2 ) ? 2 : 1 );
                        if( http->header.used > ii )
                        {
                            memmove( buf + http->chunkoff + http->chunklen,
                                     buf + ii, http->header.used - ii );
                        }
                        http->header.used -=
                            ii - ( http->chunkoff + http->chunklen );
                        http->chunkoff += http->chunklen;
                        http->chunklen = len;
                    }
                }
            }
            break;
    }

    return 0;
}

static int
learnlength( tr_http_t * http )
{
    tr_http_header_t hdr[] = {
        { "Content-Length",    NULL, 0 },
        /*
          XXX this probably doesn't handle multiple encodings correctly
          http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.41
        */
        { "Transfer-Encoding", NULL, 0 },
        { NULL,                NULL, 0 }
    };
    const char * body;
    char       * duped;

    body = tr_httpParse( http->header.buf, http->header.used, hdr );
    if( NULL != body )
    {
        if( 0 < hdr[1].len &&
            0 == tr_strncasecmp( "chunked", hdr[1].data, hdr[1].len ) )
        {
            http->lengthtype = HTTP_LENGTH_CHUNKED;
            http->chunkoff = body - http->header.buf;
            http->chunklen = 0;
        }
        else if( 0 < hdr[0].len )
        {
            http->lengthtype = HTTP_LENGTH_FIXED;
            http->chunkoff = body - http->header.buf;
            duped = tr_strndup( hdr[0].data, hdr[0].len );
            http->chunklen = strtol( duped, NULL, 10 );
            free( duped );
        }
        else
        {
            http->lengthtype = HTTP_LENGTH_EOF;
        }
        return 1;
    }

    return 0;
}

char *
tr_httpWhatsMyAddress( tr_http_t * http )
{
    struct sockaddr_in sin;
    socklen_t          size;
    char               buf[INET_ADDRSTRLEN];

    if( 0 > http->sock )
    {
        return NULL;
    }

    size = sizeof( sin );
    if( 0 > getsockname( http->sock, (struct sockaddr *) &sin, &size ) )
    {
        return NULL;
    }

    tr_netNtop( &sin.sin_addr, buf, sizeof( buf ) );

    return strdup( buf );
}

void
tr_httpClose( tr_http_t * http )
{
    if( NULL != http->resolve )
    {
        tr_netResolveClose( http->resolve );
    }
    free( http->host );
    if( 0 <= http->sock )
    {
        tr_netClose( http->sock );
    }
    free( http->header.buf );
    free( http->body.buf );
    free( http );
}
