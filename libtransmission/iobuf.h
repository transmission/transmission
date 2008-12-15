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

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_EVBUFFER_H
#define TR_EVBUFFER_H

/**
 * tr_iobuf is an i/o convenience wrapper similar to libevent's "bufferevent".
 * it provides two evbuffers, one for writing and one for reading, and handles
 * pumping them to/from the given fd.
 *
 * this class differs from bufferevent in two major ways:
 * 1. the action callbacks include the number of bytes transferred
 * 2. the up/down speeds are directly constrained by our `bandwidth' object
 * 3. the implementation is hidden in the .c file
 *
 * 4. a late addition nasty hack to read/write on demand, called from
 *    bandwidth. this actually seems to make a lot of this class redundant
 *    and probably should be refactored.
 */
struct tr_iobuf;

struct evbuffer;
struct tr_bandwidth;
struct tr_session;

/** @brief returns the input evbuffer to that we can read from */
struct evbuffer* tr_iobuf_input( struct tr_iobuf * iobuf );

/** @brief returns the output evbuffer that we can write to */
struct evbuffer* tr_iobuf_output( struct tr_iobuf * iobuf );

/** @brief prototype for the callbacks invoked when bytes have been read or written
    @see tr_iobuf_new
    @see tr_iobuf_setcb */
typedef void (*tr_iobuf_cb)( struct tr_iobuf*, size_t bytes_transferred, void* );

/** @brief prototype for the callback invoked on error
    @see tr_iobuf_new
    @see tr_iobuf_setcb */
typedef void (*tr_iobuf_error_cb)( struct tr_iobuf*, short what, void* );

/** @brief create a new tr_iobuf object. */
struct tr_iobuf* tr_iobuf_new( tr_session           * session,
                               struct tr_bandwidth  * bandwidth, 
                               int                    fd,
                               short                  event,
                               tr_iobuf_cb            readcb,
                               tr_iobuf_cb            writecb,
                               tr_iobuf_error_cb      errorcb,
                               void                 * cbarg );

/** @brief destroy a tr_iobuf object. */
void tr_iobuf_free( struct tr_iobuf * iobuf );

/** @brief change the number of seconds it takes for a read/write to timeout */
void tr_iobuf_settimeout( struct tr_iobuf  * iobuf,
                          int                timeout_read,
                          int                timeout_write );

/** @brief set the bandwidth object that limits this iobuf's read/write speeds */
void tr_iobuf_set_bandwidth( struct tr_iobuf      * iobuf,
                             struct tr_bandwidth  * bandwidth );

/** @brief change the callbacks invoked by this iobuf on error and bytes transferred */
void tr_iobuf_setcb( struct tr_iobuf    * iobuf,
                     tr_iobuf_cb          readcb,
                     tr_iobuf_cb          writecb,
                     tr_iobuf_error_cb    errorcb,
                     void               * cbarg );

/** @brief tell the iobuf to poll for certain states.
    @brief event may be EV_READ, EV_WRITE, or EV_READ|EV_WRITE */
int tr_iobuf_enable( struct tr_iobuf * iobuf, short event );

/** @brief tell the iobuf to stop polling for certain states.
    @brief event may be EV_READ, EV_WRITE, or EV_READ|EV_WRITE */
int tr_iobuf_disable( struct tr_iobuf * iobuf, short event );

int tr_iobuf_flush_output_buffer( struct tr_iobuf * iobuf, size_t max );

int tr_iobuf_tryread( struct tr_iobuf * iobuf, size_t max );

#endif
