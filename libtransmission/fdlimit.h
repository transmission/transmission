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

#include "net.h"

/***********************************************************************
 * tr_fdInit
 ***********************************************************************
 * Detect the maximum number of open files and initializes things.
 **********************************************************************/
void tr_fdInit( void );

void tr_fdClose( void );

/***********************************************************************
 * tr_fdFileOpen
 ***********************************************************************
 * If it isn't open already, tries to open the file 'name' in the
 * directory 'folder'. If 'name' itself contains '/'s, required
 * subfolders are created. The file is open read-write if 'write' is 1
 * (created if necessary), read-only if 0.
 * Returns the file descriptor if successful, otherwise returns
 * one of the TR_ERROR_IO_*.
 **********************************************************************/
int tr_fdFileOpen( const char * filename, int write );

/***********************************************************************
 * tr_fdFileRelease
 ***********************************************************************
 * Indicates that the file whose descriptor is 'file' is unused at the
 * moment and can safely be closed.
 **********************************************************************/
void tr_fdFileRelease( int file );

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

