/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef _TR_NET_H_
#define _TR_NET_H_

#ifdef WIN32
 #include <inttypes.h>
 #include <winsock2.h>
 typedef int socklen_t;
#else
 #include <sys/types.h>
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
#endif

#ifdef WIN32
 #define ECONNREFUSED WSAECONNREFUSED
 #define ECONNRESET   WSAECONNRESET
 #define EHOSTUNREACH WSAEHOSTUNREACH
 #define EINPROGRESS  WSAEINPROGRESS
 #define ENOTCONN     WSAENOTCONN
 #define EWOULDBLOCK  WSAEWOULDBLOCK
 #define sockerrno WSAGetLastError( )
#else
 #include <errno.h>
 #define sockerrno errno
#endif

struct tr_session;

typedef enum tr_address_type
{
    TR_AF_INET,
    TR_AF_INET6
} tr_address_type;

typedef struct tr_address
{
    tr_address_type type;
    union {
        /* The order here is important for tr_in{,6}addr_any initialization,
         * since we can't use C99 designated initializers */
        struct in6_addr addr6;
        struct in_addr  addr4;
    } addr;
} tr_address;

extern const tr_address tr_inaddr_any;
extern const tr_address tr_in6addr_any;

const char *tr_ntop( const tr_address * src,
                     char * dst,
                     int size );
const char *tr_ntop_non_ts( const tr_address * src );
tr_address *tr_pton( const char * src,
                     tr_address * dst );
int tr_compareAddresses( const tr_address * a,
                         const tr_address * b);
void tr_normalizeV4Mapped( tr_address * const addr );

void tr_suspectAddress( const tr_address * a, const char * source );
tr_bool tr_isAddress( const tr_address * a );

typedef struct tr_net_af_support
{
    tr_bool has_inet6;
    tr_bool needs_inet4;
} tr_net_af_support;

tr_net_af_support tr_net_getAFSupport( tr_port );

/***********************************************************************
 * Socket list housekeeping
 **********************************************************************/
typedef struct tr_socketList tr_socketList;
tr_socketList *tr_socketListAppend( tr_socketList * const head,
                                    const tr_address * const addr );
tr_socketList *tr_socketListNew( const tr_address * const addr );
void tr_socketListFree( tr_socketList * const head );
void tr_socketListRemove( tr_socketList * const head,
                          tr_socketList * const el);
void tr_socketListTruncate( tr_socketList * const head,
                            tr_socketList * const start );
int tr_socketListGetSocket( const tr_socketList * const el );
const tr_address *tr_socketListGetAddress( const tr_socketList * const el );
void tr_socketListForEach( tr_socketList * const head,
                           void ( * cb ) ( int * const,
                                           tr_address * const,
                                           void * const ),
                           void * const userData);

/***********************************************************************
 * Sockets
 **********************************************************************/
int  tr_netOpenTCP( tr_session       * session,
                    const tr_address * addr,
                    tr_port            port );

int  tr_netBindTCP( const tr_address * addr,
                    tr_port            port,
                    tr_bool            suppressMsgs );

int  tr_netAccept( tr_session * session,
                   int          bound,
                   tr_address * setme_addr,
                   tr_port    * setme_port );

int  tr_netSetTOS( int s,
                   int tos );

void tr_netClose( int s );

void tr_netInit( void );

#endif /* _TR_NET_H_ */
