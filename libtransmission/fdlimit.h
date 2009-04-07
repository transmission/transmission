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

#include "transmission.h"
#include "net.h"

void tr_fdInit( size_t openFileLimit,
                size_t globalPeerLimit );

FILE* tr_open_file_for_reading( const char * filename, tr_bool sequential );

/**
 * Returns an fd to the specified filename.
 *
 * A small pool of open files is kept to avoid the overhead of
 * continually opening and closing the same files when downloading
 * piece data.    It's also used to ensure only one caller can
 * write to the file at a time.  Callers check out a file, use it,
 * and then check it back in via tr_fdFileReturn() when done.
 *
 * - if `folder' doesn't exist, errno is set to ENOENT.
 * - if doWrite is true, subfolders in torrentFile are created if necessary.
 * - if doWrite is true, the target file is created if necessary.
 *
 * on success, a file descriptor >= 0 is returned.
 * on failure, a -1 is returned and errno is set.
 *
 * @see tr_fdFileReturn
 * @see tr_fdFileClose
 */
int  tr_fdFileCheckout( const char             * folder,
                        const char             * torrentFile,
                        tr_bool                  doWrite,
                        tr_preallocation_mode    preallocationMode,
                        uint64_t                 desiredFileSize );

/**
 * Returns an fd from tr_fdFileCheckout() so that other clients may borrow it.
 *
 * @see tr_fdFileCheckout
 * @see tr_fdFileClose
 */
void tr_fdFileReturn( int file );

/**
 * Closes a file that's being held by our file repository.
 *
 * If the file isn't checked out, it's closed immediately.
 * If the file is currently checked out, it will be closed upon its return.
 *
 * @see tr_fdFileCheckout
 * @see tr_fdFileReturn
 */
void     tr_fdFileClose( const char * filename );


/***********************************************************************
 * Sockets
 **********************************************************************/
int      tr_fdSocketCreate( int domain, int type );

int      tr_fdSocketAccept( int           b,
                            tr_address  * addr,
                            tr_port     * port );

void     tr_fdSocketClose( int s );

/***********************************************************************
 * tr_fdClose
 ***********************************************************************
 * Frees resources allocated by tr_fdInit.
 **********************************************************************/
void     tr_fdClose( void );


void     tr_fdSetPeerLimit( uint16_t n );

uint16_t tr_fdGetPeerLimit( void );

