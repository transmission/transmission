/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005 Transmission authors and contributors
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

#ifndef TR_COMPLETION_H
#define TR_COMPLETION_H

#include "transmission.h"

typedef struct tr_completion_s tr_completion_t;

tr_completion_t     * tr_cpInit( tr_torrent_t * );
void                  tr_cpClose( tr_completion_t * );
void                  tr_cpReset( tr_completion_t * );

/* General */

cp_status_t           tr_cpGetStatus ( const tr_completion_t * );
uint64_t              tr_cpDownloadedValid( const tr_completion_t * );
uint64_t              tr_cpLeftUntilComplete( const tr_completion_t * );
uint64_t              tr_cpLeftUntilDone( const tr_completion_t * );
float                 tr_cpPercentComplete( const tr_completion_t * );
float                 tr_cpPercentDone( const tr_completion_t * );
void                  tr_cpInvalidateDND ( tr_completion_t * );

/* Pieces */
int                   tr_cpPieceIsComplete( const tr_completion_t *, int piece );
void                  tr_cpPieceAdd( tr_completion_t *, int piece );
void                  tr_cpPieceRem( tr_completion_t *, int piece );

/* Blocks */
void                  tr_cpDownloaderAdd( tr_completion_t *, int block );
void                  tr_cpDownloaderRem( tr_completion_t *, int block );
int                   tr_cpBlockIsComplete( const tr_completion_t *, int block );
void                  tr_cpBlockAdd( tr_completion_t *, int block );
void                  tr_cpBlockBitfieldSet( tr_completion_t *, struct tr_bitfield_s * );
float                 tr_cpPercentBlocksInPiece( const tr_completion_t * cp, int piece );
/* Missing = we don't have it and we are not getting it from any peer yet */
int                   tr_cpMissingBlocksForPiece( const tr_completion_t * cp, int piece );
int                   tr_cpMissingBlockInPiece( const tr_completion_t *, int piece );

const struct tr_bitfield_s * tr_cpPieceBitfield( const tr_completion_t* );
const struct tr_bitfield_s * tr_cpBlockBitfield( const tr_completion_t * );

#endif
