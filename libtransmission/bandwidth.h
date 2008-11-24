/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_BANDWIDTH_H
#define TR_BANDWIDTH_H

typedef struct tr_bandwidth tr_bandwidth;

/**
***
**/

tr_bandwidth* tr_bandwidthNew           ( tr_session          * session );

void          tr_bandwidthFree          ( tr_bandwidth        * bandwidth );

/**
***
**/

void          tr_bandwidthSetLimited    ( tr_bandwidth        * bandwidth,
                                          size_t                byteCount );

void          tr_bandwidthSetUnlimited  ( tr_bandwidth        * bandwidth );

size_t        tr_bandwidthClamp         ( const tr_bandwidth  * bandwidth,
                                          size_t                byteCount );

/**
***
**/

double        tr_bandwidthGetRawSpeed   ( const tr_bandwidth  * bandwidth );

double        tr_bandwidthGetPieceSpeed ( const tr_bandwidth  * bandwidth );

void          tr_bandwidthUsed          ( tr_bandwidth        * bandwidth,
                                          size_t                byteCount,
                                          int                   isPieceData );

#endif
