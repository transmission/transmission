/******************************************************************************
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

static int createSocket()
{
    int s;

    s = socket( AF_INET, SOCK_STREAM, 0 );
    if( s < 0 )
    {
        tr_err( "Could not create socket (%s)", strerror( errno ) );
        return -1;
    }

    return makeSocketNonBlocking( s );
}

struct tr_resolve_s
{
    int            status;
    char           * address;
    struct in_addr addr;

    tr_lock_t      lock;
    tr_thread_t    thread;
    int            orphan;
};

/* Hem, global variable. Initialized from tr_init(). */
tr_lock_t gethostbynameLock;

static void resolveFunc( void * _r )
{
    tr_resolve_t * r = _r;
    struct hostent * host;

    tr_lockLock( &r->lock );

    r->addr.s_addr = inet_addr( r->address );
    if( r->addr.s_addr != 0xFFFFFFFF )
    {
        /* This was an IP address, no resolving required */
        r->status = TR_RESOLVE_OK;
        goto resolveDone;
    }

    tr_lockLock( &gethostbynameLock );
    tr_lockUnlock( &r->lock );
    host = gethostbyname( r->address );
    tr_lockLock( &r->lock );
    if( host )
    {
        memcpy( &r->addr, host->h_addr, host->h_length );
        r->status = TR_RESOLVE_OK;
    }
    else
    {
        r->status = TR_RESOLVE_ERROR;
    }
    tr_lockUnlock( &gethostbynameLock );

resolveDone:
    if( r->orphan )
    {
        /* tr_netResolveClose was closed already. Free memory */
        tr_lockUnlock( &r->lock );
        tr_lockClose( &r->lock );
        free( r );
    }
    else
    {
        tr_lockUnlock( &r->lock );
    }
}

tr_resolve_t * tr_netResolveInit( char * address )
{
    tr_resolve_t * r = malloc( sizeof( tr_resolve_t ) );

    r->status  = TR_RESOLVE_WAIT;
    r->address = address;

    tr_lockInit( &r->lock );
    tr_threadCreate( &r->thread, resolveFunc, r );
    r->orphan = 0;

    return r;
}

int tr_netResolvePulse( tr_resolve_t * r, struct in_addr * addr )
{
    int ret;

    tr_lockLock( &r->lock );
    ret = r->status;
    if( ret == TR_RESOLVE_OK )
    {
        *addr = r->addr;
    }
    tr_lockUnlock( &r->lock );

    return ret;
}

void tr_netResolveClose( tr_resolve_t * r )
{
    tr_lockLock( &r->lock );
    if( r->status == TR_RESOLVE_WAIT )
    {
        /* Let the thread die */
        r->orphan = 1;
        tr_lockUnlock( &r->lock );
        return;
    }
    tr_lockUnlock( &r->lock );

    /* Clean up */
    tr_threadJoin( &r->thread );
    tr_lockClose( &r->lock );
    free( r );
}

/* Blocking version */
int tr_netResolve( char * address, struct in_addr * addr )
{
    addr->s_addr = inet_addr( address );
    return ( addr->s_addr == 0xFFFFFFFF );
}

int tr_netOpen( struct in_addr addr, in_port_t port )
{
    int s;
    struct sockaddr_in sock;

    s = createSocket();
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

int tr_netBind( int port )
{
    int s;
    struct sockaddr_in sock;
#ifdef SO_REUSEADDR
    int optval;
#endif

    s = createSocket();
    if( s < 0 )
    {
        return -1;
    }

#ifdef SO_REUSEADDR
    optval = 1;
    setsockopt( s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof( optval ) );
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
   
    tr_inf( "Binded port %d", port );
    listen( s, 5 );

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

int tr_netRecv( int s, uint8_t * buf, int size )
{
    int ret;

    ret = recv( s, buf, size, 0 );
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
