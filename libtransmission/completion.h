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

struct tr_completion_s
{
    tr_torrent_t * tor;
    uint8_t      * blockBitfield;
    uint8_t      * blockDownloaders;
    int            blockCount;
    uint8_t      * pieceBitfield;
    int          * missingBlocks;
};

tr_completion_t * tr_cpInit( tr_torrent_t * );
void              tr_cpClose( tr_completion_t * );
void              tr_cpReset( tr_completion_t * );

/* General */
float             tr_cpCompletionAsFloat( tr_completion_t * );
static inline int tr_cpIsSeeding( tr_completion_t * cp )
{
    return ( cp->blockCount == cp->tor->blockCount );
}
uint64_t          tr_cpLeftBytes( tr_completion_t * );

/* Pieces */
int               tr_cpPieceIsComplete( tr_completion_t *, int piece );
uint8_t         * tr_cpPieceBitfield( tr_completion_t * );
void              tr_cpPieceAdd( tr_completion_t *, int piece );

/* Blocks */
void              tr_cpDownloaderAdd( tr_completion_t *, int block );
void              tr_cpDownloaderRem( tr_completion_t *, int block );
int               tr_cpBlockIsComplete( tr_completion_t *, int block );
void              tr_cpBlockAdd( tr_completion_t *, int block );
void              tr_cpBlockRem( tr_completion_t *, int block );
uint8_t         * tr_cpBlockBitfield( tr_completion_t * );
void              tr_cpBlockBitfieldSet( tr_completion_t *, uint8_t * );
float             tr_cpPercentBlocksInPiece( tr_completion_t * cp, int piece );
/* Missing = we don't have it and we are not getting it from any peer yet */
static inline int tr_cpMissingBlocksForPiece( tr_completion_t * cp, int piece )
{
    return cp->missingBlocks[piece];
}
int               tr_cpMissingBlockInPiece( tr_completion_t *, int piece );
int               tr_cpMostMissingBlockInPiece( tr_completion_t *, int piece,
                                                int * downloaders );
