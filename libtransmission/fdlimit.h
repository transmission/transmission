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

#include "net.h"

/***********************************************************************
 * tr_fdInit
 ***********************************************************************
 * Detect the maximum number of open files and initializes things.
 **********************************************************************/
void tr_fdInit( int globalPeerLimit );

void tr_fdClose( void );

/**
 * Returns an fd to the specified filename.
 *
 * A small repository of open files is kept to avoid the overhead of continually
 * opening and closing the same files when writing piece data during download.
 * It's also used to ensure that only one client uses the file at a time.
 * Clients must check out a file to use it, then return it, like a library, when done.
 *
 * if write is nonzero and dirname(filename) doesn't exist, dirname is created.
 * if write is nonzero and filename doesn't exist, filename is created.
 * returns the fd if successful; otherwise, one of TR_ERROR_IO_*
 *
 * @see tr_fdFileReturn
 * @see tr_fdFileClose
 */
int tr_fdFileCheckout( const char * filename, int write );

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
void tr_fdFileClose( const char * filename );



/***********************************************************************
 * Sockets
 **********************************************************************/
int  tr_fdSocketCreate( int type, int priority );
int  tr_fdSocketAccept( int b, struct in_addr * addr, tr_port_t * port );
void tr_fdSocketClose( int s );

/***********************************************************************
 * tr_fdClose
 ***********************************************************************
 * Frees resources allocated by tr_fdInit.
 **********************************************************************/
void tr_fdClose( void );


void tr_fdSetPeerLimit( uint16_t n );

uint16_t tr_fdGetPeerLimit( void );

