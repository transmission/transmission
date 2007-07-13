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


/***********************************************************************
 * DNS resolution
 **********************************************************************/
int tr_netResolve( const char *, struct in_addr * );

typedef struct tr_resolve_s tr_resolve_t;
void           tr_netResolveThreadInit();
void           tr_netResolveThreadClose();
tr_resolve_t * tr_netResolveInit( const char * );
tr_tristate_t  tr_netResolvePulse( tr_resolve_t *, struct in_addr * );
void           tr_netResolveClose( tr_resolve_t * );


/***********************************************************************
 * TCP and UDP sockets
 **********************************************************************/
#define tr_netOpenTCP( addr, port, priority ) \
    tr_netOpen( (addr), (port), SOCK_STREAM, (priority) )
#define tr_netOpenUDP( addr, port, priority ) \
    tr_netOpen( (addr), (port), SOCK_DGRAM, (priority) )
int  tr_netOpen    ( struct in_addr addr, in_port_t port, int type,
                     int priority );
int  tr_netMcastOpen( int port, struct in_addr addr );
#define tr_netBindTCP( port ) tr_netBind( (port), SOCK_STREAM )
#define tr_netBindUDP( port ) tr_netBind( (port), SOCK_DGRAM )
int  tr_netBind    ( int port, int type );
int  tr_netAccept  ( int s, struct in_addr *, in_port_t * );
void tr_netClose   ( int s );

#define TR_NET_BLOCK 0x80000000
#define TR_NET_CLOSE 0x40000000
int  tr_netSend    ( int s, const void * buf, int size );
#define tr_netRecv( s, buf, size ) tr_netRecvFrom( (s), (buf), (size), NULL )
int  tr_netRecvFrom( int s, uint8_t * buf, int size, struct sockaddr_in * );

void tr_netNtop( const struct in_addr * addr, char * buf, int len );


#define tr_addrcmp( aa, bb )    memcmp( ( void * )(aa), ( void * )(bb), 4)
