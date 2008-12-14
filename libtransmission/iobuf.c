/*
 * Copyright (c) 2002-2004 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Transmission modifications and new bugs by Charles Kerr.
 * Source: libevent's "patches-1.4" branch, svn revision 949
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* write */

#include "evutil.h"
#include "event.h"

#include "transmission.h"
#include "bandwidth.h"
#include "iobuf.h"
#include "session.h"
#include "utils.h"

#define MAGIC_NUMBER 235705

struct tr_iobuf
{
    struct event_base * ev_base;

    struct event ev_read;
    struct event ev_write;

    struct evbuffer * input;
    struct evbuffer * output;

    tr_iobuf_cb readcb;
    tr_iobuf_cb writecb;
    tr_iobuf_error_cb errorcb;
    void * cbarg;

    int magicNumber;

    int timeout_read;  /* in seconds */
    int timeout_write; /* in seconds */
    short enabled;     /* events that are currently enabled */

    tr_session * session;
    struct tr_bandwidth * bandwidth;
};

/***
****
***/

static int
isBuf( const struct tr_iobuf * iobuf )
{
    return ( iobuf != NULL ) && ( iobuf->magicNumber == MAGIC_NUMBER );
}

static int
tr_evbuffer_write( struct evbuffer *buffer, int fd, size_t maxlen )
{
    int n = MIN( EVBUFFER_LENGTH( buffer ), maxlen );

#ifdef WIN32
    n = send(fd, buffer->buffer, n,  0 );
#else
    n = write(fd, buffer->buffer, n );
#endif
    if( n == -1 )
        return -1;
    if (n == 0)
        return 0;
    evbuffer_drain( buffer, n );

    return n;
}

static int
tr_iobuf_add(struct event *ev, int timeout)
{
    struct timeval tv, *ptv = NULL;

    if (timeout) {
        evutil_timerclear(&tv);
        tv.tv_sec = timeout;
        ptv = &tv;
    }

    return event_add( ev, ptv );
}

static void
tr_iobuf_readcb( int fd, short event, void * arg )
{
    int res;
    short what = EVBUFFER_READ;
    struct tr_iobuf * b = arg;
    const size_t howmuch = tr_bandwidthClamp( b->bandwidth, TR_DOWN, b->session->so_rcvbuf );

    assert( isBuf( b ) );

    if( event == EV_TIMEOUT ) {
        what |= EVBUFFER_TIMEOUT;
        goto error;
    }

    /* if we don't have any bandwidth left, stop reading */
    if( howmuch < 1 ) {
        event_del( &b->ev_read );
        return;
    }

    res = evbuffer_read( b->input, fd, howmuch );
    if( res == -1 ) {
        if( errno == EAGAIN || errno == EINTR )
            goto reschedule;
        /* error case */
        what |= EVBUFFER_ERROR;
    } else if( res == 0 ) {
        /* eof case */
        what |= EVBUFFER_EOF;
    }

    if( res <= 0 )
        goto error;

    tr_iobuf_add( &b->ev_read, b->timeout_read );

    /* Invoke the user callback - must always be called last */
    if( b->readcb != NULL )
        ( *b->readcb )( b, res, b->cbarg );
    return;

 reschedule:
    tr_iobuf_add( &b->ev_read, b->timeout_read );
    return;

 error:
    (*b->errorcb)( b, what, b->cbarg );
}

static void
tr_iobuf_writecb( int fd, short event, void * arg )
{
    int res = 0;
    short what = EVBUFFER_WRITE;
    struct tr_iobuf * b = arg;
    size_t howmuch;

    assert( isBuf( b ) );

    if( event == EV_TIMEOUT ) {
        what |= EVBUFFER_TIMEOUT;
        goto error;
    }

    howmuch = MIN( (size_t)b->session->so_sndbuf, EVBUFFER_LENGTH( b->output ) );
    howmuch = tr_bandwidthClamp( b->bandwidth, TR_UP, howmuch );

    /* if we don't have any bandwidth left, stop writing */
    if( howmuch < 1 ) {
        event_del( &b->ev_write );
        return;
    }

    res = tr_evbuffer_write( b->output, fd, howmuch );
    if (res == -1) {
#ifndef WIN32
/*todo. evbuffer uses WriteFile when WIN32 is set. WIN32 system calls do not
 *set errno. thus this error checking is not portable*/
        if (errno == EAGAIN || errno == EINTR || errno == EINPROGRESS)
            goto reschedule;
        /* error case */
        what |= EVBUFFER_ERROR;

#else
        goto reschedule;
#endif

    } else if (res == 0) {
        /* eof case */
        what |= EVBUFFER_EOF;
    }
    if (res <= 0)
        goto error;

    if( EVBUFFER_LENGTH( b->output ) )
        tr_iobuf_add( &b->ev_write, b->timeout_write );

    if( b->writecb != NULL )
        (*b->writecb)( b, res, b->cbarg );

    return;

 reschedule:
    if( EVBUFFER_LENGTH( b->output ) )
        tr_iobuf_add( &b->ev_write, b->timeout_write );
    return;

 error:
    (*b->errorcb)( b, what, b->cbarg );
}


/*
 * Create a new buffered event object.
 *
 * The read callback is invoked whenever we read new data.
 * The write callback is invoked whenever the output buffer is drained.
 * The error callback is invoked on a write/read error or on EOF.
 *
 * Both read and write callbacks maybe NULL.  The error callback is not
 * allowed to be NULL and have to be provided always.
 */

struct tr_iobuf *
tr_iobuf_new( tr_session          * session,
              tr_bandwidth        * bandwidth,
              int                   fd,
              short                 event,
              tr_iobuf_cb           readcb,
              tr_iobuf_cb           writecb,
              tr_iobuf_error_cb     errorcb,
              void                * cbarg )
{
    struct tr_iobuf * b;

    b = tr_new0( struct tr_iobuf, 1 );
    b->magicNumber = MAGIC_NUMBER;
    b->session = session;
    b->bandwidth = bandwidth;
    b->input = evbuffer_new( );
    b->output = evbuffer_new( );

    event_set( &b->ev_read, fd, EV_READ, tr_iobuf_readcb, b );
    event_set( &b->ev_write, fd, EV_WRITE, tr_iobuf_writecb, b );

    tr_iobuf_setcb( b, readcb, writecb, errorcb, cbarg );
    tr_iobuf_enable( b, event );

    return b;
}

void
tr_iobuf_setcb( struct tr_iobuf    * b,
                tr_iobuf_cb          readcb,
                tr_iobuf_cb          writecb,
                tr_iobuf_error_cb    errorcb,
                void               * cbarg )
{
    assert( isBuf( b ) );

    b->readcb = readcb;
    b->writecb = writecb;
    b->errorcb = errorcb;
    b->cbarg = cbarg;
}

/* Closing the file descriptor is the responsibility of the caller */

void
tr_iobuf_free( struct tr_iobuf * b )
{
    assert( isBuf( b ) );

    b->magicNumber = 0xDEAD;
    event_del( &b->ev_read );
    event_del( &b->ev_write );
    evbuffer_free( b->input );
    evbuffer_free( b->output );
    tr_free( b );
}

int
tr_iobuf_enable(struct tr_iobuf * b, short event )
{
    assert( isBuf( b ) );

    if( event & EV_READ )
        if( tr_iobuf_add( &b->ev_read, b->timeout_read ) == -1 )
            return -1;

    if( event & EV_WRITE )
        if ( tr_iobuf_add( &b->ev_write, b->timeout_write ) == -1 )
            return -1;

    b->enabled |= event;
    return 0;
}

int
tr_iobuf_disable( struct tr_iobuf * b, short event )
{
    assert( isBuf( b ) );

    if( event & EV_READ )
        if( event_del( &b->ev_read ) == -1 )
            return -1;

    if( event & EV_WRITE )
        if( event_del( &b->ev_write ) == -1 )
            return -1;

    b->enabled &= ~event;
    return 0;
}

void
tr_iobuf_settimeout( struct tr_iobuf  * b,
                     int                timeout_read,
                     int                timeout_write )
{
    assert( isBuf( b ) );

    b->timeout_read = timeout_read;
    if( event_pending( &b->ev_read, EV_READ, NULL ) )
        tr_iobuf_add( &b->ev_read, timeout_read );

    b->timeout_write = timeout_write;
    if( event_pending( &b->ev_write, EV_WRITE, NULL ) )
        tr_iobuf_add( &b->ev_write, timeout_write );
}

void
tr_iobuf_set_bandwidth( struct tr_iobuf      * b,
                        struct tr_bandwidth  * bandwidth )
{
    assert( isBuf( b ) );

    b->bandwidth = bandwidth;
}

struct evbuffer*
tr_iobuf_input( struct tr_iobuf * b )
{
    assert( isBuf( b ) );

    return b->input;
}

struct evbuffer*
tr_iobuf_output( struct tr_iobuf * b )
{
    assert( isBuf( b ) );

    return b->output;
}
