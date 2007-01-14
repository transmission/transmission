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

#ifndef TR_IO_H
#define TR_IO_H 1

typedef struct tr_io_s tr_io_t;

void      tr_ioLoadResume  ( tr_torrent_t * );

tr_io_t * tr_ioInit        ( tr_torrent_t * );

/***********************************************************************
 * tr_ioRead, tr_ioWrite
 ***********************************************************************
 * Reads or writes the block specified by the piece index, the offset in
 * that piece and the size of the block. Returns 0 if successful, 
 * TR_ERROR_ASSERT if the parameters are incorrect, one of the
 * TR_ERROR_IO_* otherwise.
 **********************************************************************/
int tr_ioRead  ( tr_io_t *, int index, int begin, int len, uint8_t * );
int tr_ioWrite ( tr_io_t *, int index, int begin, int len, uint8_t * );

/***********************************************************************
 * tr_ioHash
 ***********************************************************************
 * Hashes the specified piece and updates the completion accordingly.
 **********************************************************************/
int tr_ioHash ( tr_io_t *, int piece );

/***********************************************************************
 * tr_ioSync
 ***********************************************************************
 * Flush all data on disc by closing all files, and update the cache
 * file.
 **********************************************************************/
void tr_ioSync( tr_io_t * );

void      tr_ioClose       ( tr_io_t * );

#endif
