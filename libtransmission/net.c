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

#include "transmission.h"

/***********************************************************************
 * DNS resolution
 **********************************************************************/

/***********************************************************************
 * tr_netResolve
 ***********************************************************************
 * Synchronous "resolution": only works with character strings
 * representing numbers expressed in the Internet standard `.' notation.
 * Returns a non-zero value if an error occurs.
 **********************************************************************/
int tr_netResolve( const char * address, struct in_addr * addr )
{
    addr->s_addr = inet_addr( address );
    return ( addr->s_addr == 0xFFFFFFFF );
}

/* TODO: Make this code reentrant */
static tr_thread_t  resolveThread;
static tr_lock_t    resolveLock;
static volatile int resolveDie;
static tr_resolve_t * resolveQueue;

static void resolveRelease ( tr_resolve_t * );
static void resolveFunc    ( void * );

struct tr_resolve_s
{
    tr_tristate_t  status;
    char           * address;
    struct in_addr addr;

    int            refcount;
    tr_resolve_t   * next;
};

/***********************************************************************
 * tr_netResolveThreadInit
 ***********************************************************************
 * Initializes the static variables used for resolution and launch the
 * gethostbyname thread.
 **********************************************************************/
void tr_netResolveThreadInit()
{
    resolveDie   = 0;
    resolveQueue = NULL;
    tr_lockInit( &resolveLock );
    tr_threadCreate( &resolveThread, resolveFunc, NULL );
}

/***********************************************************************
 * tr_netResolveThreadClose
 ***********************************************************************
 * Notices the gethostbyname thread that is should terminate. Doesn't
 * wait until it does, in case it is stuck in a resolution: we let it
 * die and clean itself up.
 **********************************************************************/
void tr_netResolveThreadClose()
{
    tr_lockLock( &resolveLock );
    resolveDie = 1;
    tr_lockUnlock( &resolveLock );
    tr_wait( 200 );
}

/***********************************************************************
 * tr_netResolveInit
 ***********************************************************************
 * Adds an address to the resolution queue.
 **********************************************************************/
tr_resolve_t * tr_netResolveInit( const char * address )
{
    tr_resolve_t * r;

    r           = malloc( sizeof( tr_resolve_t ) );
    r->status   = TR_WAIT;
    r->address  = strdup( address );
    r->refcount = 2;
    r->next     = NULL;

    tr_lockLock( &resolveLock );
    if( !resolveQueue )
    {
        resolveQueue = r;
    }
    else
    {
        tr_resolve_t * iter;
        for( iter = resolveQueue; iter->next; iter = iter->next );
        iter->next = r;
    }
    tr_lockUnlock( &resolveLock );

    return r;
}

/***********************************************************************
 * tr_netResolvePulse
 ***********************************************************************
 * Checks the current status of a resolution.
 **********************************************************************/
tr_tristate_t tr_netResolvePulse( tr_resolve_t * r, struct in_addr * addr )
{
    tr_tristate_t ret;

    tr_lockLock( &resolveLock );
    ret = r->status;
    if( ret == TR_OK )
    {
        *addr = r->addr;
    }
    tr_lockUnlock( &resolveLock );

    return ret;
}

/***********************************************************************
 * tr_netResolveClose
 ***********************************************************************
 * 
 **********************************************************************/
void tr_netResolveClose( tr_resolve_t * r )
{
    resolveRelease( r );
}

/***********************************************************************
 * resolveRelease
 ***********************************************************************
 * The allocated tr_resolve_t structures should be freed when
 * tr_netResolveClose was called *and* it was removed from the queue.
 * This can happen in any order, so we use a refcount to know we can
 * take it out.
 **********************************************************************/
static void resolveRelease( tr_resolve_t * r )
{
    if( --r->refcount < 1 )
    {
        free( r->address );
        free( r );
    }
}

/***********************************************************************
 * resolveFunc
 ***********************************************************************
 * Keeps waiting for addresses to resolve, and removes them from the
 * queue once resolution is done.
 **********************************************************************/
static void resolveFunc( void * arg UNUSED )
{
    tr_resolve_t * r;
    struct hostent * host;

    tr_dbg( "Resolve thread started" );

    tr_lockLock( &resolveLock );

    while( !resolveDie )
    {
        if( !( r = resolveQueue ) )
        {
            /* TODO: Use a condition wait */
            tr_lockUnlock( &resolveLock );
            tr_wait( 50 );
            tr_lockLock( &resolveLock );
            continue;
        }

        /* Blocking resolution */
        tr_lockUnlock( &resolveLock );
        host = gethostbyname( r->address );
        tr_lockLock( &resolveLock );

        if( host )
        {
            memcpy( &r->addr, host->h_addr, host->h_length );
            r->status = TR_OK;
        }
        else
        {
            r->status = TR_ERROR;
        }
        
        resolveQueue = r->next;
        resolveRelease( r );
    }

    /* Clean up  */
    tr_lockUnlock( &resolveLock );
    tr_lockClose( &resolveLock );
    while( ( r = resolveQueue ) )
    {
        resolveQueue = r->next;
        resolveRelease( r );
    }

    tr_dbg( "Resolve thread exited" );
}


/***********************************************************************
 * TCP sockets
 **********************************************************************/

static int makeSocketNonBlocking( int s )
{
    int flags;

#ifdef SYS_BEOS
    flags = 1;
    if( setsockopt( s, SOL_SOCKET, SO_NONBLOCK,
                    &flags, sizeof( int ) ) < 0 )
#else
    if( ( flags = fcntl( s, F_GETFL, 0 ) ) < 0 ||
        fcntl( s, F_SETFL, flags | O_NONBLOCK ) < 0 )
#endif
    {
        tr_err( "Could not set socket to non-blocking mode (%s)",
                strerror( errno ) );
        tr_netClose( s );
        return -1;
    }

    return s;
}

static int createSocket( int type )
{
    int s;

    s = socket( AF_INET, type, 0 );
    if( s < 0 )
    {
        tr_err( "Could not create socket (%s)", strerror( errno ) );
        return -1;
    }

    return makeSocketNonBlocking( s );
}

int tr_netOpen( struct in_addr addr, in_port_t port, int type )
{
    int s;
    struct sockaddr_in sock;

    s = createSocket( type );
    if( s < 0 )
    {
        return -1;
    }

    memset( &sock, 0, sizeof( sock ) );
    sock.sin_family      = AF_INET;
    sock.sin_addr.s_addr = addr.s_addr;
    sock.sin_port        = port;

    if( connect( s, (struct sockaddr *) &sock,
                 sizeof( struct sockaddr_in ) ) < 0 &&
        errno != EINPROGRESS )
    {
        tr_err( "Could not connect socket (%s)", strerror( errno ) );
        tr_netClose( s );
        return -1;
    }

    return s;
}

#ifdef IP_ADD_MEMBERSHIP
int tr_netMcastOpen( int port, struct in_addr addr )
{
    int fd;
    struct ip_mreq req;

    fd = tr_netBindUDP( port );
    if( 0 > fd )
    {
        return -1;
    }

    memset( &req, 0, sizeof( req ) );
    req.imr_multiaddr.s_addr = addr.s_addr;
    req.imr_interface.s_addr = htonl( INADDR_ANY );
    if( setsockopt( fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &req, sizeof ( req ) ) )
    {
        tr_err( "Could not join multicast group (%s)", strerror( errno ) );
        tr_netClose( fd );
        return -1;
    }

    return fd;
}
#else /* IP_ADD_MEMBERSHIP */
int tr_netMcastOpen( int port UNUSED, struct in_addr addr UNUSED )
{
    return -1;
}
#endif /* IP_ADD_MEMBERSHIP */

int tr_netBind( int port, int type )
{
    int s;
    struct sockaddr_in sock;
#if defined( SO_REUSEADDR ) || defined( SO_REUSEPORT )
    int optval;
#endif

    s = createSocket( type );
    if( s < 0 )
    {
        return -1;
    }

#ifdef SO_REUSEADDR
    optval = 1;
    setsockopt( s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof( optval ) );
#endif

#ifdef SO_REUSEPORT
    if( SOCK_DGRAM == type )
    {
        optval = 1;
        setsockopt( s, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof( optval ) );
    }
#endif

    memset( &sock, 0, sizeof( sock ) );
    sock.sin_family      = AF_INET;
    sock.sin_addr.s_addr = INADDR_ANY;
    sock.sin_port        = htons( port );

    if( bind( s, (struct sockaddr *) &sock,
               sizeof( struct sockaddr_in ) ) )
    {
        tr_err( "Could not bind port %d", port );
        tr_netClose( s );
        return -1;
    }

    return s;
}

int tr_netAccept( int s, struct in_addr * addr, in_port_t * port )
{
    int t;
    unsigned len;
    struct sockaddr_in sock;

    len = sizeof( sock );
    t   = accept( s, (struct sockaddr *) &sock, &len );

    if( t < 0 )
    {
        return -1;
    }
    
    *addr = sock.sin_addr;
    *port = sock.sin_port;

    return makeSocketNonBlocking( t );
}

int tr_netSend( int s, uint8_t * buf, int size )
{
    int ret;

    ret = send( s, buf, size, 0 );
    if( ret < 0 )
    {
        if( errno == ENOTCONN || errno == EAGAIN || errno == EWOULDBLOCK )
        {
            ret = TR_NET_BLOCK;
        }
        else
        {
            ret = TR_NET_CLOSE;
        }
    }

    return ret;
}

int tr_netRecvFrom( int s, uint8_t * buf, int size, struct sockaddr_in * addr )
{
    socklen_t len;
    int       ret;

    len = ( NULL == addr ? 0 : sizeof( *addr ) );
    ret = recvfrom( s, buf, size, 0, ( struct sockaddr * ) addr, &len );
    if( ret < 0 )
    {
        if( errno == EAGAIN || errno == EWOULDBLOCK )
        {
            ret = TR_NET_BLOCK;
        }
        else
        {
            ret = TR_NET_CLOSE;
        }
    }
    if( !ret )
    {
        ret = TR_NET_CLOSE;
    }

    return ret;
}

void tr_netClose( int s )
{
#ifdef BEOS_NETSERVER
    closesocket( s );
#else
    close( s );
#endif
}

void tr_netNtop( const struct in_addr * addr, char * buf, int len )
{
    const uint8_t * cast;

    cast = (const uint8_t *)addr;
    snprintf( buf, len, "%hhu.%hhu.%hhu.%hhu",
              cast[0], cast[1], cast[2], cast[3] );
}
