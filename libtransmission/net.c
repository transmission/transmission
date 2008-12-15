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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>

#ifdef WIN32
 #include <winsock2.h> /* inet_addr */
#else
 #include <arpa/inet.h> /* inet_addr */
 #include <netdb.h>
 #include <fcntl.h>
#endif

#include <evutil.h>

#include "transmission.h"
#include "fdlimit.h"
#include "natpmp.h"
#include "net.h"
#include "peer-io.h"
#include "platform.h"
#include "utils.h"

const tr_address tr_in6addr_any = { TR_AF_INET6, { IN6ADDR_ANY_INIT } }; 
const tr_address tr_inaddr_any = { TR_AF_INET, 
    { { { { INADDR_ANY, 0x00, 0x00, 0x00 } } } } }; 


void
tr_netInit( void )
{
    static int initialized = FALSE;

    if( !initialized )
    {
#ifdef WIN32
        WSADATA wsaData;
        WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
#endif
        initialized = TRUE;
    }
}


const char * 
tr_ntop( const tr_address * src, char * dst, int size ) 
{ 
    if( src->type == TR_AF_INET ) 
        return inet_ntop( AF_INET, &src->addr, dst, size ); 
    else 
        return inet_ntop( AF_INET6, &src->addr, dst, size ); 
} 

/* 
 * Non-threadsafe version of tr_ntop, which uses a static memory area for a buffer. 
 * This function is suitable to be called from libTransmission's networking code, 
 * which is single-threaded. 
 */ 
const char * 
tr_ntop_non_ts( const tr_address * src ) 
{ 
    static char buf[INET6_ADDRSTRLEN]; 
    return tr_ntop( src, buf, sizeof( buf ) ); 
} 

tr_address * 
tr_pton( const char * src, tr_address * dst ) 
{ 
    int retval = inet_pton( AF_INET, src, &dst->addr ); 
    if( retval < 0 ) 
        return NULL; 
    else if( retval == 0 ) 
        retval = inet_pton( AF_INET6, src, &dst->addr ); 
    else
    { 
        dst->type = TR_AF_INET; 
        return dst; 
    } 

    if( retval < 1 ) 
        return NULL; 
    dst->type = TR_AF_INET6; 
    return dst; 
}

void
tr_normalizeV4Mapped( tr_address * const addr )
{
    if( addr->type == TR_AF_INET6 && IN6_IS_ADDR_V4MAPPED( &addr->addr.addr6 ) )
    {
        addr->type = TR_AF_INET;
        memcpy( &addr->addr.addr4.s_addr, addr->addr.addr6.s6_addr + 12, 4 );
    }
}

/* 
 * Compare two tr_address structures. 
 * Returns: 
 * <0 if a < b 
 * >0 if a > b 
 * 0  if a == b 
 */ 
int 
tr_compareAddresses( const tr_address * a, const tr_address * b) 
{ 
    int retval; 
    int addrlen; 

    /* IPv6 addresses are always "greater than" IPv4 */ 
    if( a->type == TR_AF_INET && b->type == TR_AF_INET6 ) 
        return 1; 
    if( a->type == TR_AF_INET6 && b->type == TR_AF_INET ) 
        return -1; 

    if( a->type == TR_AF_INET ) 
        addrlen = sizeof( struct in_addr ); 
    else 
        addrlen = sizeof( struct in6_addr ); 
    retval = memcmp( &a->addr, &b->addr, addrlen ); 
    if( retval == 0 ) 
        return 0; 
     
    return retval; 
} 

/***********************************************************************
 * Socket list housekeeping
 **********************************************************************/
struct tr_socketList
{
    int             socket;
    tr_address      addr;
    tr_socketList * next;
};

tr_socketList *
tr_socketListAppend( tr_socketList * const head,
                     const tr_address * const addr )
{
    tr_socketList * tmp;
    assert( head );
    for( tmp = head; tmp->next; tmp = tmp->next );
    tmp->next = tr_socketListNew( addr );
    return tmp->next;
}

tr_socketList *
tr_socketListNew( const tr_address * const addr )
{
    tr_socketList * tmp;
    tmp = tr_new( tr_socketList, 1 );
    tmp->socket = -1;
    tmp->addr = *addr;
    tmp->next = NULL;
    return tmp;
}

void
tr_socketListFree( tr_socketList * const head )
{
    assert( head );
    if( head->next )
        tr_socketListFree( head->next );
    tr_free( head );
}

void
tr_socketListRemove( tr_socketList * const head,
                     tr_socketList * const el)
{
    tr_socketList * tmp;
    assert( head );
    assert( el );
    for( tmp = head; tmp->next && tmp->next != el; tmp = tmp->next );
    tmp->next = el->next;
    el->next = NULL;
    tr_socketListFree(el);
}

void
tr_socketListTruncate( tr_socketList * const head,
                       tr_socketList * const start )
{
    tr_socketList * tmp;
    assert( head );
    assert( start );
    for( tmp = head; tmp->next && tmp->next != start; tmp = tmp->next );
    tr_socketListFree( start );
    tmp->next = NULL;
}

int
tr_socketListGetSocket( const tr_socketList * const el )
{
    assert( el );
    return el->socket;
}

const tr_address *
tr_socketListGetAddress( const tr_socketList * const el )
{
    assert( el );
    return &el->addr;
}

void
tr_socketListForEach( tr_socketList * const head,
                      void ( * cb ) ( int * const,
                                      tr_address * const,
                                      void * const),
                      void * const userData )
{
    tr_socketList * tmp;
    for( tmp = head; tmp; tmp = tmp->next )
        cb( &tmp->socket, &tmp->addr, userData );
}

/***********************************************************************
 * TCP sockets
 **********************************************************************/

int
tr_netSetTOS( int s,
              int tos )
{
#ifdef IP_TOS
    return setsockopt( s, IPPROTO_IP, IP_TOS, (char*)&tos, sizeof( tos ) );
#else
    return 0;
#endif
}

static int
makeSocketNonBlocking( int fd )
{
    if( fd >= 0 )
    {
        if( evutil_make_socket_nonblocking( fd ) )
        {
            int tmperrno;
            tr_err( _( "Couldn't create socket: %s" ),
                   tr_strerror( sockerrno ) );
            tmperrno = sockerrno;
            tr_netClose( fd );
            fd = -tmperrno;
        }
    }

    return fd;
}

static int
createSocket( int domain, int type )
{
    return makeSocketNonBlocking( tr_fdSocketCreate( domain, type ) );
}

static void
setSndBuf( tr_session * session UNUSED, int fd UNUSED )
{
#if 0
    if( fd >= 0 )
    {
        const int sndbuf = session->so_sndbuf;
        const int rcvbuf = session->so_rcvbuf;
        setsockopt( fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof( sndbuf ) );
        setsockopt( fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof( rcvbuf ) );
    }
#endif
}

static socklen_t
setup_sockaddr( const tr_address        * addr,
                tr_port                   port,
                struct sockaddr_storage * sockaddr)
{
    struct sockaddr_in  sock4;
    struct sockaddr_in6 sock6;
    if( addr->type == TR_AF_INET )
    {
        memset( &sock4, 0, sizeof( sock4 ) );
        sock4.sin_family      = AF_INET;
        sock4.sin_addr.s_addr = addr->addr.addr4.s_addr;
        sock4.sin_port        = port;
        memcpy( sockaddr, &sock4, sizeof( sock4 ) );
        return sizeof( struct sockaddr_in );
    }
    else
    {
        memset( &sock6, 0, sizeof( sock6 ) );
        sock6.sin6_family = AF_INET6;
        sock6.sin6_port = port;
        sock6.sin6_flowinfo = 0;
        sock6.sin6_addr = addr->addr.addr6;
        memcpy( sockaddr, &sock6, sizeof( sock6 ) );
        return sizeof( struct sockaddr_in6 );
    }
}

int
tr_netOpenTCP( tr_session        * session,
               const tr_address  * addr,
               tr_port             port )
{
    int                     s;
    struct sockaddr_storage sock;
    const int               type = SOCK_STREAM;
    socklen_t               addrlen;

    if( ( s = createSocket( ( addr->type == TR_AF_INET ? AF_INET : AF_INET6 ),
                            type ) ) < 0 )
        return s;

    setSndBuf( session, s );

    addrlen = setup_sockaddr( addr, port, &sock );

    if( ( connect( s, (struct sockaddr *) &sock,
                  addrlen ) < 0 )
#ifdef WIN32
      && ( sockerrno != WSAEWOULDBLOCK )
#endif
      && ( sockerrno != EINPROGRESS ) )
    {
        int tmperrno;
        tr_err( _( "Couldn't connect socket %d to %s, port %d (errno %d - %s)" ),
               s, tr_ntop_non_ts( addr ), (int)port, sockerrno, tr_strerror( sockerrno ) );
        tmperrno = sockerrno;
        tr_netClose( s );
        s = -tmperrno;
    }

    tr_deepLog( __FILE__, __LINE__, NULL, "New OUTGOING connection %d (%s)",
               s, tr_peerIoAddrStr( addr, port ) );

    return s;
}

int
tr_netBindTCP( const tr_address * addr, tr_port port, tr_bool suppressMsgs )
{
    int                     s;
    struct sockaddr_storage sock;
    const int               type = SOCK_STREAM;
    int                     addrlen;

#if defined( SO_REUSEADDR ) || defined( SO_REUSEPORT )
    int                optval;
#endif

    if( ( s = createSocket( ( addr->type == TR_AF_INET ? AF_INET : AF_INET6 ),
                            type ) ) < 0 )
        return s;

#ifdef SO_REUSEADDR
    optval = 1;
    setsockopt( s, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof( optval ) );
#endif

    addrlen = setup_sockaddr( addr, htons( port ), &sock );

    if( bind( s, (struct sockaddr *) &sock,
             addrlen ) )
    {
        int tmperrno;
        if( !suppressMsgs )
            tr_err( _( "Couldn't bind port %d on %s: %s" ), port,
                    tr_ntop_non_ts( addr ), tr_strerror( sockerrno ) );
        tmperrno = sockerrno;
        tr_netClose( s );
        return -tmperrno;
    }
    if( !suppressMsgs )
        tr_dbg(  "Bound socket %d to port %d on %s",
                 s, port, tr_ntop_non_ts( addr ) );
    return s;
}

int
tr_netAccept( tr_session  * session,
              int           b,
              tr_address  * addr,
              tr_port     * port )
{
    int fd = makeSocketNonBlocking( tr_fdSocketAccept( b, addr, port ) );
    setSndBuf( session, fd );
    return fd;
}

void
tr_netClose( int s )
{
    tr_fdSocketClose( s );
}
