/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#ifndef _TR_NET_H_
#define _TR_NET_H_

#ifdef WIN32
    #include <stdint.h>
    #include <winsock2.h>
    typedef int socklen_t;
    typedef uint16_t tr_port_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    typedef in_port_t tr_port_t;
#endif

#ifdef SYS_BEOS
    #include <inttypes.h>
    typedef uint32_t socklen_t;
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

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

struct in_addr;
struct sockaddr_in;

/***********************************************************************
 * DNS resolution
 **********************************************************************/
int tr_netResolve( const char *, struct in_addr * );

typedef struct tr_resolve_s tr_resolve_t;
void           tr_netResolveThreadInit( void );
void           tr_netResolveThreadClose( void );
tr_resolve_t * tr_netResolveInit( const char * address );
tr_tristate_t  tr_netResolvePulse( tr_resolve_t *, struct in_addr * );
void           tr_netResolveClose( tr_resolve_t * );


/***********************************************************************
 * TCP and UDP sockets
 **********************************************************************/
int  tr_netOpenTCP  ( const struct in_addr * addr, tr_port_t port, int priority );
int  tr_netOpenUDP  ( const struct in_addr * addr, tr_port_t port, int priority );
int  tr_netMcastOpen( int port, const struct in_addr * addr );
int  tr_netBindTCP  ( int port );
int  tr_netBindUDP  ( int port );
int  tr_netAccept   ( int s, struct in_addr *, tr_port_t * );
void tr_netClose    ( int s );

#define TR_NET_BLOCK 0x80000000
#define TR_NET_CLOSE 0x40000000
int  tr_netSend    ( int s, const void * buf, int size );
#define tr_netRecv( s, buf, size ) tr_netRecvFrom( (s), (buf), (size), NULL )
int  tr_netRecvFrom( int s, uint8_t * buf, int size, struct sockaddr_in * );

void tr_netNtop( const struct in_addr * addr, char * buf, int len );

void tr_netInit ( void );


#define tr_addrcmp( aa, bb )    memcmp( ( void * )(aa), ( void * )(bb), 4)

#endif /* _TR_NET_H_ */
