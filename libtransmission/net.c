/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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

int tr_netResolve( char * address, struct in_addr * addr )
{
    struct hostent * host;

    addr->s_addr = inet_addr( address );
    if( addr->s_addr != 0xFFFFFFFF )
    {
        return 0;
    }

    if( !( host = gethostbyname( address ) ) )
    {
        tr_err( "Could not resolve (%s)", address );
        return -1;
    }
    memcpy( addr, host->h_addr, host->h_length );

    return 0;
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

int tr_netBind( int * port )
{
    int s, i;
    struct sockaddr_in sock;
    int minPort, maxPort;

    s = createSocket();
    if( s < 0 )
    {
        return -1;
    }

    minPort = *port;
    maxPort = minPort + 1000;
    maxPort = MIN( maxPort, 65535 );

    for( i = minPort; i <= maxPort; i++ )
    {
        memset( &sock, 0, sizeof( sock ) );
        sock.sin_family      = AF_INET;
        sock.sin_addr.s_addr = INADDR_ANY;
        sock.sin_port        = htons( i );

        if( !bind( s, (struct sockaddr *) &sock,
                   sizeof( struct sockaddr_in ) ) )
        {
            break;
        }
    }

    if( i > maxPort )
    {
        tr_netClose( s );
        tr_err( "Could not bind any port from %d to %d",
                minPort, maxPort );
        return -1;
    }
   
    tr_inf( "Binded port %d", i );
    *port = i;
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
