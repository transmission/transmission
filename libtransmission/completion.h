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

#ifndef TR_COMPLETION_H
#define TR_COMPLETION_H

#include "transmission.h"

struct tr_bitfield;
typedef struct tr_completion tr_completion;

tr_completion  * tr_cpInit( tr_torrent * );
void             tr_cpClose( tr_completion * );

/* General */

cp_status_t      tr_cpGetStatus ( const tr_completion * );
uint64_t         tr_cpHaveTotal( const tr_completion * );
uint64_t         tr_cpHaveValid( const tr_completion * );
uint64_t         tr_cpLeftUntilComplete( const tr_completion * );
uint64_t         tr_cpLeftUntilDone( const tr_completion * );
float            tr_cpPercentComplete( const tr_completion * );
float            tr_cpPercentDone( const tr_completion * );
void             tr_cpInvalidateDND ( tr_completion * );

/* Pieces */
int              tr_cpPieceIsComplete( const tr_completion *, tr_piece_index_t piece );
void             tr_cpPieceAdd( tr_completion *, tr_piece_index_t piece );
void             tr_cpPieceRem( tr_completion *, tr_piece_index_t piece );

/* Blocks */
int              tr_cpBlockIsComplete( const tr_completion *, tr_block_index_t block );
void             tr_cpBlockAdd( tr_completion *, tr_block_index_t block );
void             tr_cpBlockBitfieldSet( tr_completion *, struct tr_bitfield * );
float            tr_cpPercentBlocksInPiece( const tr_completion * cp, tr_piece_index_t piece );
int              tr_cpMissingBlocksInPiece( const tr_completion * cp, tr_piece_index_t piece );


const struct tr_bitfield * tr_cpPieceBitfield( const tr_completion* );
const struct tr_bitfield * tr_cpBlockBitfield( const tr_completion * );

#endif
