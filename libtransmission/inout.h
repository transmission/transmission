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

#ifndef TR_IO_H
#define TR_IO_H 1

struct tr_torrent;

/**
 * Reads the block specified by the piece index, offset, and length.
 * @return 0 on success, TR_ERROR_ASSERT if the arguments are incorrect,
 * or TR_ERROR_IO_* otherwise.
 */
int tr_ioRead  ( struct tr_torrent*, int index, int begin, int len, uint8_t * );

/**
 * Writes the block specified by the piece index, offset, and length.
 * @return 0 on success, TR_ERROR_ASSERT if the arguments are incorrect,
 * or TR_ERROR_IO_* otherwise.
 */
tr_errno tr_ioWrite ( struct tr_torrent *, int index, int begin, int len, const uint8_t * );

/* hashes the specified piece and updates the completion accordingly. */
int tr_ioHash ( tr_torrent*, int piece );

/**
***
**/

typedef void (*tr_recheck_done_cb)( tr_torrent * tor );

void tr_ioRecheckAdd( tr_torrent          * tor,
                      tr_recheck_done_cb    recheck_done_cb );

void tr_ioRecheckRemove( tr_torrent * tor );

/**
***
**/

#endif
