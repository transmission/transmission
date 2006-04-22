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


/***********************************************************************
 * DNS resolution
 **********************************************************************/
int tr_netResolve( char *, struct in_addr * );

#define TR_RESOLVE_WAIT  0
#define TR_RESOLVE_ERROR 1
#define TR_RESOLVE_OK    2
typedef struct tr_resolve_s tr_resolve_t;
void           tr_netResolveThreadInit();
void           tr_netResolveThreadClose();
tr_resolve_t * tr_netResolveInit( char * );
int            tr_netResolvePulse( tr_resolve_t *, struct in_addr * );
void           tr_netResolveClose( tr_resolve_t * );


/***********************************************************************
 * TCP sockets
 **********************************************************************/
int  tr_netOpen    ( struct in_addr addr, in_port_t port );
int  tr_netBind    ( int );
int  tr_netAccept  ( int s, struct in_addr *, in_port_t * );
void tr_netClose   ( int s );

#define TR_NET_BLOCK 0x80000000
#define TR_NET_CLOSE 0x40000000
int  tr_netSend    ( int s, uint8_t * buf, int size );
int  tr_netRecv    ( int s, uint8_t * buf, int size );

